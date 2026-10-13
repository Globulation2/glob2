# SPDX-License-Identifier: GPL-3.0-or-later
"""The restricted source is data, never a cross-dialect code escape hatch."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('preparation_rules', ROOT / 'tools/shaders/gradient_preparation_rules.py')
rules = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rules)


class PreparationRuleSourceTest(unittest.TestCase):
    def source(self):
        return {'version': 1, 'functions': [{'name': 'seed', 'parameters': ['value'], 'expression': 'value'}]}

    def test_committed_outputs_match_restricted_source(self):
        rules.verify()
        source = json.loads(rules.SOURCE.read_text())
        self.assertEqual(rules.outputs(source), rules.outputs(copy.deepcopy(source)))
        self.assertEqual(len(rules.outputs(source)), 3)

    def test_rejects_code_and_nonportable_identifiers(self):
        for name in ('', 'éclair', 'uint', 'class', 'static_assert', 'vec4', 'gl_position', '_internal', 'a__b', 'x); return 0; //', 7, None):
            with self.subTest(name=name):
                source = self.source()
                source['functions'][0]['name'] = name
                with self.assertRaises(ValueError):
                    rules.outputs(source)
                source = self.source()
                source['functions'][0]['parameters'] = [name]
                with self.assertRaises(ValueError):
                    rules.outputs(source)

    def test_rejects_invalid_expression_language(self):
        for expression in (True, False, -1, 2**32, {}, [], ['call', 'value'], ['shift', 'value', 32],
                           ['shift', 'value', -1], ['shift', 'value', 'value'], ['or', 'value'],
                           ['select', 'value', 1], ['not', 'unknown'], [['or'], 1, 2]):
            with self.subTest(expression=expression):
                source = self.source()
                source['functions'][0]['expression'] = expression
                with self.assertRaises(ValueError):
                    rules.outputs(source)

    def test_rejects_invalid_or_ambiguous_schema(self):
        cases = [None, {}, {'version': True, 'functions': []}, {'version': 1, 'functions': {}},
                 {'version': 1, 'functions': []}, {'version': 1, 'functions': [None]}]
        source = self.source(); source['extra'] = 1; cases.append(source)
        source = self.source(); source['functions'][0]['parameters'] = ['value', 'value']; cases.append(source)
        source = self.source(); source['functions'][0]['parameters'] = ['seed']; cases.append(source)
        source = self.source(); source['functions'].append(copy.deepcopy(source['functions'][0])); cases.append(source)
        source = self.source(); source['functions'][0]['extra'] = 1; cases.append(source)
        for source in cases:
            with self.subTest(source=source):
                with self.assertRaises(ValueError):
                    rules.outputs(source)

    def test_accepts_uint32_boundaries_and_only_constant_shifts(self):
        source = self.source()
        source['functions'][0]['expression'] = ['select', 'value', ['shift', 1, 31], 2**32-1]
        for content in rules.outputs(source).values():
            self.assertIn('4294967295u', content)
            self.assertIn('1u << 31u', content)
            self.assertIn('value != 0u', content)


if __name__ == '__main__':
    unittest.main()
