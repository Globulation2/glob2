"""The authored tutorial must cover the shipped source and native text table."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class TouchTutorialCatalogTests(unittest.TestCase):
    def test_generated_catalog_and_all_seven_languages_are_current(self):
        result = subprocess.run(['python3', 'tools/tutorial/build_catalog.py', '--check'],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('116 messages', result.stdout)
        self.assertIn('8 source revisions', result.stdout)
        self.assertIn('7 languages', result.stdout)
