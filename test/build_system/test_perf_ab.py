"""Contracts for tools/perf_ab.py."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class PerfAbTest(unittest.TestCase):
    def test_alternating_pairs_produce_a_table(self):
        with tempfile.TemporaryDirectory() as directory:
            builds = {}
            for name, value, opt in (('base', 2.0, 2), ('cand', 1.5, 2)):
                build = Path(directory) / name
                build.mkdir()
                (build / 'identity.json').write_text(json.dumps({'mode': 'perf', 'opt': opt}))
                (build / 'value').write_text(str(value))
                builds[name] = build
            log = Path(directory) / 'order'
            command = [sys.executable, '-c',
                       f"import sys,pathlib; b=pathlib.Path(sys.argv[1]); open({str(log)!r},'a').write(b.name+'\\n');"
                       "print('ms=' + (b/'value').read_text())", '{build}']
            result = subprocess.run([sys.executable, str(ROOT / 'tools/perf_ab.py'), '--baseline', str(builds['base']),
                                     '--candidate', str(builds['cand']), '--pairs', '3', '--warmup', '0',
                                     '--metric', r'ms=([0-9.]+)', '--metric-name', 'ms', '--name', 'fake', '--', *command],
                                    capture_output=True, text=True, check=True)
            self.assertIn('| fake | ms | 2 (', result.stdout)
            self.assertIn('| -25.0 | 3 | perf-screen O2 |', result.stdout)
            self.assertEqual(log.read_text().split(), ['base', 'cand', 'cand', 'base', 'base', 'cand'])


if __name__ == '__main__':
    unittest.main()
