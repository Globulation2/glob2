"""Decoder cache identities follow linked dependency bytes and build options."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scons'))
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
                                ({'lean_images': '1', 'release': '1', 'server': '1'}, 'linux')):
            with self.assertRaisesRegex(ValueError, 'lean_images'):
                build_identity(arguments, host=host)

@unittest.skipUnless(__import__('shutil').which('scons'), 'SCons unavailable')
class LinuxRuntimeInstallTests(unittest.TestCase):
    def test_real_install_graph_preserves_one_canonical_library_and_aliases(self):
        import subprocess
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            prefix = root / 'prefix'
            (prefix / 'lib').mkdir(parents=True)
            canonical = prefix / 'lib/libSDL2_image.so.0.800.12'
            canonical.write_bytes(b'decoder')
            (prefix / 'lib/libSDL2_image.so.0').symlink_to(canonical.name)
            (prefix / 'lib/libSDL2_image.so').symlink_to(canonical.name)
            (root / 'SConstruct').write_text(
                'import sys\nsys.path.insert(0, '+repr(str(Path(__file__).resolve().parents[2] / 'scons'))+')\n'
                'from install_image_runtime import install_runtime\n'
                'env=Environment(tools=[], BINDIR='+repr(str(root / 'stage/usr/bin'))+')\n'
                'install_runtime(env, '+repr(str(prefix))+')\n')
            subprocess.run(['scons', '-Q', 'install'], cwd=root, check=True, capture_output=True)
            installed = root / 'stage/usr/lib/glob2'
            self.assertEqual((installed / canonical.name).read_bytes(), b'decoder')
            self.assertTrue((installed / 'libSDL2_image.so.0').is_symlink())
            self.assertEqual((installed / 'libSDL2_image.so').resolve(), (installed / canonical.name).resolve())


if __name__ == '__main__':
    unittest.main()
