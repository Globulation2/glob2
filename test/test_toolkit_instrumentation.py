#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Development-only AST regressions; requires the pinned clang Python bindings."""
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'toolkit_instrumentation', ROOT / 'tools/map-generators/instrument_toolkit_budget.py')
tool = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = tool
spec.loader.exec_module(tool)

try:
    from clang import cindex
except ImportError:
    cindex = None


@unittest.skipIf(cindex is None, 'requires development-only clang Python bindings')
class InstrumentationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cindex.Config.set_library_file(os.getenv(
            'LIBCLANG_PATH', '/usr/lib/x86_64-linux-gnu/libclang-18.so.1'))

    def instrument(self, source):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'input.cpp'
            path.write_bytes(source)
            (root / 'GenerationWork.h').write_text(
                'namespace MapGeneration { inline void generationCheckpoint() {} }\n')
            unit = cindex.Index.create().parse(str(path), args=['-std=c++20', '-I' + str(root)])
            errors = [str(d) for d in unit.diagnostics if d.severity >= cindex.Diagnostic.Error]
            self.assertEqual(errors, [])
            planner = tool.Instrumentation(cindex, lambda filename: filename == str(path))
            planner.walk(unit.cursor)
            changed = planner.changed_sources()
            # Planning must leave the original source untouched.
            self.assertEqual(path.read_bytes(), source)
            return changed.get(str(path), source)

    def test_long_comments_do_not_duplicate_a_checkpoint(self):
        source = (b'#include "GenerationWork.h"\nvoid test(int n) { while(n) { /*' +
                  b'long comment ' * 40 +
                  b'*/ ::MapGeneration::generationCheckpoint(); --n; } }\n')
        self.assertEqual(self.instrument(source), source)

    def test_byte_offsets_and_nested_single_statement_loops(self):
        source = ('#include <vector>\n// café\n'
                  'void test(std::vector<int>& values) { '
                  'for(int i=0;i<2;++i) while(values[i]) --values[i]; }\n').encode()
        changed = self.instrument(source)
        self.assertIn('// café'.encode(), changed)
        self.assertEqual(changed.count(b'generationCheckpoint();'), 2)
        self.assertEqual(changed.count(b'values.at(i)'), 2)
        # The result must parse, and a second pass must be a no-op.
        self.assertEqual(self.instrument(changed), changed)

    def test_conflicting_edits_are_rejected_before_writing(self):
        planner = tool.Instrumentation(cindex, lambda _: True)
        planner.add_edit('example.cpp', 10, tool.Edit(b'first', 1))
        with self.assertRaisesRegex(ValueError, 'Conflicting edits'):
            planner.add_edit('example.cpp', 10, tool.Edit(b'second', 1))


if __name__ == '__main__':
    unittest.main()
