#!/usr/bin/env python3
"""Contracts for tools/refresh_sim_fixtures.py."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import refresh_sim_fixtures as refresh  # noqa: E402


class MergeRowsTest(unittest.TestCase):
    TABLE = '# header\nlinux-x86_64 0 1 a\nmacos-arm64 0 1 b\nmacos-arm64 0 2 c\n'

    def test_replaces_only_the_named_platform(self):
        merged = refresh.merge_rows(self.TABLE, 'macos-arm64', 'macos-arm64 0 1 z\nlinux-x86_64 0 1 ignored\n')
        self.assertEqual(merged, '# header\nlinux-x86_64 0 1 a\nmacos-arm64 0 1 z\n')

    def test_adds_a_new_platform_and_rejects_empty_rows(self):
        merged = refresh.merge_rows(self.TABLE, 'windows-x86_64', 'windows-x86_64 0 1 w\n')
        self.assertTrue(merged.endswith('windows-x86_64 0 1 w\n'))
        with self.assertRaises(ValueError):
            refresh.merge_rows(self.TABLE, 'macos-arm64', '# nothing\n')


if __name__ == '__main__':
    unittest.main()
