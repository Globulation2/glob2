import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import zipfile


SCRIPT = Path(__file__).resolve().parents[2] / 'platforms/web' / 'firefox_capture.py'
SPEC = importlib.util.spec_from_file_location('firefox_capture', SCRIPT)
capture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(capture)


class CaptureAdapterTests(unittest.TestCase):
    def test_unknown_screenshot_source_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Unrecognized Firefox screenshot'):
            capture.patch_handler(b'changed upstream implementation')

    def test_only_capture_body_changes_and_clip_scale_encoding_remain(self):
        source = b"header\nasync ['Page.screenshot']({clip}) {\nconst scale=2;\n    let snapshot;\nlegacy capture\n    if (mimeType === 'image/jpeg') encode(quality);\n}\nfooter"
        patched = capture.patch_handler(source, capture.digest(source)).decode()
        self.assertIn('bounds.left + rect.x, bounds.top + rect.y, rect.width, rect.height', patched)
        self.assertIn('ctx.scale(scale, scale)', patched)
        self.assertIn('DRAWWINDOW_USE_WIDGET_LAYERS', patched)
        self.assertIn('require the foreground tab', patched)
        self.assertIn('require a visible viewport clip', patched)
        self.assertIn("if (mimeType === 'image/jpeg') encode(quality);", patched)
        self.assertTrue(patched.startswith('header\n'))
        self.assertTrue(patched.endswith('footer'))
        self.assertNotIn('legacy capture', patched)

    def test_platform_is_explicit(self):
        with mock.patch.object(capture.platform, 'system', return_value='Darwin'):
            with self.assertRaisesRegex(ValueError, 'headed Linux'):
                capture.prepare('/unused', '/unused')

    def test_version_mismatch_is_rejected_before_copy(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'application.ini').write_text('[App]\nVersion=unexpected\n')
            with mock.patch.object(capture.platform, 'system', return_value='Linux'):
                with self.assertRaisesRegex(ValueError, 'Unrecognized Firefox version'):
                    capture.prepare(root, root / 'output')
            self.assertFalse((root / 'output').exists())

    @unittest.skipUnless(os.name == 'posix', 'file locking and Unix browser permissions')
    def test_private_copy_is_immutable_and_preserves_other_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source'
            source.mkdir()
            (source / 'application.ini').write_text('[App]\nVersion=155.0\n')
            executable = source / 'firefox'
            executable.write_bytes(b'browser executable')
            executable.chmod(0o755)
            with zipfile.ZipFile(source / 'omni.ja', 'w') as archive:
                archive.writestr(capture.MODULE, b'original module')
                archive.writestr('other-file', b'unchanged data')
            original = (source / 'omni.ja').read_bytes()
            with mock.patch.object(capture.platform, 'system', return_value='Linux'), mock.patch.object(capture, 'patch_handler', return_value=b'compositor adapter'):
                result = capture.prepare(source, root / 'output')
                self.assertEqual((source / 'omni.ja').read_bytes(), original)
                self.assertEqual(result.read_bytes(), executable.read_bytes())
                self.assertEqual(result.stat().st_mode, executable.stat().st_mode)
                with zipfile.ZipFile(result.parent / 'omni.ja') as archive:
                    self.assertEqual(archive.read(capture.MODULE), b'compositor adapter')
                    self.assertEqual(archive.read('other-file'), b'unchanged data')
                with mock.patch.object(capture.shutil, 'copytree', side_effect=AssertionError('existing browser must not be replaced')):
                    self.assertEqual(capture.prepare(source, root / 'output'), result)
                manifest = result.parent.parent / 'manifest.json'
                manifest.write_text(json.dumps({'unexpected': 'identity'}))
                with self.assertRaisesRegex(ValueError, 'cache identity mismatch'):
                    capture.prepare(source, root / 'output')


if __name__ == '__main__':
    unittest.main()
