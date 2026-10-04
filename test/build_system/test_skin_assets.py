"""The shipped colony paint layout stays attached across all source animations."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ColonySkinAssetsTest(unittest.TestCase):
    def test_installed_meshes_match_their_sources_and_paint_contract(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / 'tools/skins/test_export.py'),
             str(ROOT / 'data/skins/colony-v1')],
            # Every posed triangle now receives deformation checks; allow the
            # complete asset set to finish on slower or concurrently loaded hosts.
            cwd=ROOT, text=True, capture_output=True, timeout=180,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
