"""The player API header is generated from one maintained contract."""
import importlib.util
from pathlib import Path
import unittest
import os
import sys
import tempfile
import types
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('hive_api', ROOT / 'buildsys/hive_api.py')
api = importlib.util.module_from_spec(spec)
spec.loader.exec_module(api)


class HiveAPITests(unittest.TestCase):
    def test_header_retains_the_exact_player_contract(self):
        contract = (ROOT / 'services/online/packages/hive/src/commander-api.txt').read_text()
        header = api.render(contract)
        player = api.player_contract(contract)
        self.assertIn('R"GLOB2_API(' + player + ')GLOB2_API"', header)
        self.assertIn('A step returns', player)
        self.assertIn('ctx.game.units', player)
        self.assertNotIn('new UUID', player)
        self.assertNotIn('paid follow-up', player)
        self.assertIn('new UUID', contract)  # Online clients retain their control contract.

    def test_missing_section_markers_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'separate online management'):
            api.render('unstructured contract')

    def test_bad_delimiter_is_rejected(self):
        with self.assertRaises(ValueError):
            api.render(')GLOB2_API"\n## Online program management\nOnline\n## Player API\nPlayer')


if __name__ == '__main__':
    unittest.main()
