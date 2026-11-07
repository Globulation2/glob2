"""Contracts for tools/inline_guard.py."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import inline_guard  # noqa: E402

LOOP = 'int sum(Map& map) { int total = 0; for (int i = 0; i < 64; ++i) total += map.cell(i); return total; }\n'


@unittest.skipUnless(shutil.which('c++') and shutil.which('nm'), 'needs a C++ compiler and nm')
class InlineGuardTest(unittest.TestCase):
    def build(self, directory: Path, name: str, accessor: str) -> Path:
        build = directory / name
        source = directory / f'{name}.cpp'
        source.write_text(f'struct Map {{ int cells[64]; {accessor} }};\n{LOOP}')
        target = build / 'src/map/gradient/Loop.o'
        target.parent.mkdir(parents=True)
        subprocess.run(['c++', '-O2', '-c', str(source), '-o', str(target)], check=True)
        return build

    def test_an_accessor_moved_out_of_line_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            inline = self.build(directory, 'base', 'int cell(int i) const { return cells[i]; }')
            out_of_line = self.build(directory, 'cand', 'int cell(int i) const;')
            report = inline_guard.compare(inline, out_of_line)
            self.assertEqual(report['new_calls'], {'src/map/gradient/Loop.o': ['Map::cell(int) const']})
            self.assertEqual(inline_guard.compare(inline, inline)['new_calls'], {})
            self.assertEqual(inline_guard.compare(out_of_line, inline)['new_calls'], {})


if __name__ == '__main__':
    unittest.main()
