"""The pinned source SDK must apply reviewed fixes or fail explicitly."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    'sdl3_dependencies', Path(__file__).resolve().parents[2] / 'scons/sdl3_dependencies.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SourcePatchTests(unittest.TestCase):
    def test_patches_apply_once_and_reject_changed_sources(self):
        checkout = Path(__file__).resolve().parents[2] / 'build/test-profiles'
        checkout.mkdir(parents=True, exist_ok=True)
        for parent in (None, checkout):
            for newline in (b'\n', b'\r\n'):
                with self.subTest(parent=parent, newline=newline):
                    with tempfile.TemporaryDirectory(dir=parent) as temporary:
                        root = Path(temporary)
                        source = root / 'sdk'
                        source.mkdir()
                        implementation = source / 'window.c'
                        implementation.write_bytes(b'original\n')
                        patch = root / 'fix.patch'
                        diff = (b'diff --git a/window.c b/window.c\n'
                                b'--- a/window.c\n+++ b/window.c\n'
                                b'@@ -1 +1 @@\n-original\n+fixed\n')
                        patch.write_bytes(diff.replace(b'\n', newline))
                        config = root / 'gitconfig'
                        config.write_bytes(b'[core]\n    autocrlf = true\n    eol = crlf\n')
                        with mock.patch.dict(os.environ, GIT_CONFIG_GLOBAL=str(config),
                                             GIT_DIR=str(root / 'unrelated.git'),
                                             GIT_WORK_TREE=str(root), GIT_INDEX_FILE=str(root / 'index')):
                            module.apply_source_patches(source, [patch])
                            self.assertEqual(implementation.read_bytes(), b'fixed\n')
                            module.apply_source_patches(source, [patch])
                            self.assertEqual(implementation.read_bytes(), b'fixed\n')
                            implementation.write_bytes(b'unexpected\n')
                            with self.assertRaisesRegex(RuntimeError, 'does not apply'):
                                module.apply_source_patches(source, [patch])
                            self.assertEqual(implementation.read_bytes(), b'unexpected\n')


if __name__ == '__main__':
    unittest.main()
