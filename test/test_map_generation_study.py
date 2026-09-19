#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Structured generator-study regressions; no native build is required."""
import json
import importlib.util
import contextlib
import io
import tempfile
from types import SimpleNamespace
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools import map_generation_study as study


class StudyTests(unittest.TestCase):
    def invoke(self, status='completed', code=0, malformed=False, timeout=False, change=None):
        directories = []
        def native(command, **kwargs):
            directory = Path(command[command.index('--output-dir') + 1])
            directories.append(directory)
            self.assertIn('width=7', command)
            self.assertIn('height=9', command)
            if timeout:
                raise subprocess.TimeoutExpired(command, kwargs['timeout'])
            report = dict(generation={'outcome': {'success': True}},
                          terrain={k: {'percent': 0} for k in ('grass', 'sand', 'water')},
                          resources={'types': {k: {'coverage': {'tiles': 0}}
                              for k in ('wheat', 'wood', 'stone', 'algae', 'cherry', 'orange', 'prune')}},
                          space={'build_sites_4x4': 10, 'land_regions': {'components': 1}},
                          fertility={'all_tiles': {'mean': 1}})
            if change: change(report)
            result = dict(schema_version=1, status=status, diagnostic='native detail', map_report=report)
            (directory / 'result.json').write_text('not JSON' if malformed else json.dumps(result))
            # Misleading console output must not determine success.
            return subprocess.CompletedProcess(command, code, '[complete]', 'failure')
        with patch.object(study.subprocess, 'run', side_effect=native):
            result = study.generate_map('glob2', 59, 12, 128, 512, 6)
        self.assertTrue(directories)
        self.assertFalse(directories[0].exists(), 'temporary profile leaked')
        return result

    def test_result_categories_and_cleanup(self):
        self.assertEqual(self.invoke()['category'], 'completed')
        self.assertEqual(self.invoke('generation_failed', 4)['category'], 'refused')
        self.assertEqual(self.invoke('invalid_request', 2)['category'], 'refused')
        self.assertEqual(self.invoke('completed', -11)['category'], 'execution_error')
        self.assertEqual(self.invoke(malformed=True)['category'], 'execution_error')
        self.assertEqual(self.invoke(timeout=True)['category'], 'execution_error')

    def test_malformed_completed_reports_are_execution_errors(self):
        mutations = [lambda r: r['terrain'].clear(),
                     lambda r: r['resources']['types'].pop('wheat'),
                     lambda r: r['space'].update(build_sites_4x4='many'),
                     lambda r: r['fertility']['all_tiles'].update(mean=float('nan')),
                     lambda r: r['generation']['outcome'].update(success=False)]
        for mutation in mutations:
            self.assertEqual(self.invoke(change=mutation)['category'], 'execution_error')

    def test_refusal_rates_exclude_execution_errors(self):
        path = Path(__file__).resolve().parents[1] / '.agents/skills/glob2-map-design/scripts/refusal_sweep.py'
        spec = importlib.util.spec_from_file_location('refusal_sweep', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        errors = [{'category': 'execution_error'}] * 9
        self.assertIsNone(module.refusal_rate(errors))
        self.assertEqual(module.refusal_rate(errors + [{'category': 'refused'}]), 1)
        self.assertEqual(module.refusal_rate(errors + [{'category': 'completed'}]), 0)

    def test_subjects_and_extrema_survive(self):
        records = [dict(key='food', subject=0, value=0), dict(key='food', subject=1, value=100),
                   dict(key='variant', subject=None, value='wet')]
        metrics, raw = study.report_metrics({'generation': {'telemetry': {'records': records}}})
        self.assertEqual(metrics['tel:food'], 50)
        self.assertEqual(metrics['tel-min:food'], 0)
        self.assertEqual(metrics['tel-max:food'], 100)
        self.assertEqual(raw, records)

    def test_missing_measurement_is_not_a_zero(self):
        path = Path(__file__).resolve().parents[1] / '.agents/skills/glob2-map-design/scripts/control_study.py'
        spec = importlib.util.spec_from_file_location('control_study', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        rows = [dict(ok=True, study='baseline', control='', value=0, w=256, h=256,
                     m={'optional': 100, 'common': 2}),
                dict(ok=True, study='ablation', control='x', value=1, w=256, h=256,
                     m={'common': 2})]
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()) as output:
            Path(directory, 'ablation.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in rows))
            module.report(SimpleNamespace(out=directory), {'x': [0, 1]}, {'x': 0})
        self.assertNotIn('optional', output.getvalue())

    def test_catalog_and_dimensions(self):
        catalog = {'generators': [{'method': 59, 'id': 'even-ground', 'controls': []}]}
        self.assertEqual(study.generator_definition(catalog, '59'), catalog['generators'][0])
        self.assertEqual(study.generator_definition(catalog, 'even-ground'), catalog['generators'][0])
        with self.assertRaises(ValueError): study.generator_definition(catalog, 'missing')
        for size in (0, 127, -128):
            with self.assertRaises(ValueError): study.dimension_exponent(size)
        self.assertEqual(study.dimension_exponent(512), 9)


if __name__ == '__main__':
    unittest.main()
