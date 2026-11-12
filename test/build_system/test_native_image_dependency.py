"""Decoder cache identities follow linked dependency bytes and build options."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'buildsys'))
import native_image_dependency as lean
from build_layout import build_identity, default_directory


class NativeImageTests(unittest.TestCase):
    def test_linux_dependency_update_invalidates_cache_without_unrelated_libraries(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / 'libcodec.so'
            library.write_bytes(b'codec')
            unrelated = root / 'libunrelated.so'
            unrelated.write_bytes(b'unrelated')
            def pkg(command, **kwargs):
                return {'--modversion': '1', '--variable=libdir': str(root),
                        '--libs-only-l': '-lcodec', '--libs-only-L': ''}[command[1]]
            with patch.object(lean.subprocess, 'check_output', side_effect=pkg):
                baseline = lean.dependencies()
                unrelated.write_bytes(b'updated')
                self.assertEqual(baseline, lean.dependencies())
                library.write_bytes(b'new codec')
                self.assertNotEqual(baseline, lean.dependencies())

    def test_explicit_compiler_wrappers_reach_the_real_cmake_driver(self):
        options = lean.compiler_options('ccache gcc', 'ccache "/compiler with spaces/g++"')
        self.assertIn('-DCMAKE_C_COMPILER=gcc', options)
        self.assertIn('-DCMAKE_C_COMPILER_LAUNCHER=ccache', options)
        self.assertIn('-DCMAKE_CXX_COMPILER=/compiler with spaces/g++', options)
        self.assertIn('-DCMAKE_CXX_COMPILER_LAUNCHER=ccache', options)

    def test_cached_decoder_acquires_the_supported_shared_lease(self):
        with tempfile.TemporaryDirectory() as temporary:
            location = Path(temporary)
            with patch.object(lean, 'dependencies', return_value={}), \
                 patch.object(lean.subprocess, 'check_output', return_value='tool version'), \
                 patch.object(lean, 'cache', return_value=location), \
                 patch.object(lean, 'verified', return_value=True), \
                 patch.object(lean, 'hold', wraps=lean.hold) as hold:
                self.assertEqual(lean.ensure(location), location / 'prefix')
                hold.assert_called_once_with(location)

    def test_private_decoder_cannot_share_ordinary_build_outputs(self):
        baseline = build_identity({'release': '1'}, host='linux')
        candidate = build_identity({'release': '1', 'lean_images': '1'}, host='linux')
        self.assertNotEqual(default_directory(baseline), default_directory(candidate))
        for arguments, host in (({'lean_images': '1'}, 'linux'),
                                ({'lean_images': '1', 'release': '1'}, 'darwin'),
                                ({'lean_images': '1', 'release': '1', 'role': 'relay'}, 'linux')):
            with self.assertRaisesRegex(ValueError, 'lean_images'):
                build_identity(arguments, host=host)


if __name__ == '__main__':
    unittest.main()
