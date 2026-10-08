#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Authoring package identity and path regressions."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('generator_package_tool', ROOT / 'tools/map-generators/package.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class PackageToolTest(unittest.TestCase):
    def author(self, root, source):
        (root / 'manifest.json').write_text(json.dumps({'entry': 'generator.js'}))
        (root / 'generator.js').write_bytes(source)

    def test_frozen_modules_preserve_windows_and_mixed_line_endings(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = b'// Windows\r\nexport function generate(){}\r// retained\n'
            self.author(root, source)
            packed = json.loads(tool.pack(root))
            self.assertEqual(packed['modules']['generator.js'].encode('utf-8'), source)

    def test_authoring_directory_and_module_symlinks_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            authored = root / 'authored'
            authored.mkdir()
            self.author(authored, b'export function generate(){}')
            link = root / 'linked'
            link.symlink_to(authored, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'symlinks'):
                tool.pack(link)
            (authored / 'extra.js').symlink_to(authored / 'generator.js')
            with self.assertRaisesRegex(ValueError, 'symlinks'):
                tool.pack(authored)

    def test_encoded_container_limit_includes_json_escaping(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            # Raw source fits, but every quote becomes two bytes in portable JSON.
            self.author(root, b'"' * (tool.PACKAGE_BYTES // 2 + 1))
            with self.assertRaisesRegex(ValueError, 'runtime limits'):
                tool.pack(root)


if __name__ == '__main__':
    unittest.main()
