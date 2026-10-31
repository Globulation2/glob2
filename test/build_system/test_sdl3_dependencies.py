"""The pinned source SDK must apply reviewed fixes or fail explicitly."""
import importlib.util
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
import urllib.error
from unittest import mock

spec = importlib.util.spec_from_file_location(
    'sdl3_dependencies', Path(__file__).resolve().parents[2] / 'scons/sdl3_dependencies.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DownloadTests(unittest.TestCase):
    def setUp(self):
        self.payload = b'pinned dependency bytes'
        self.versions = {'dependency': dict(archive='dependency.tar.gz',
            url='https://example.invalid/dependency.tar.gz',
            sha256=hashlib.sha256(self.payload).hexdigest())}

    def test_dns_and_server_failure_retry_exact_url_then_verify_bytes(self):
        failures = [urllib.error.URLError('DNS unavailable'),
                    urllib.error.HTTPError(self.versions['dependency']['url'], 503,
                                           'unavailable', {}, None), io.BytesIO(self.payload)]
        with tempfile.TemporaryDirectory() as temporary, \
                mock.patch.object(module.urllib.request, 'urlopen', side_effect=failures) as request, \
                mock.patch.object(module.time, 'sleep') as sleep:
            module.download(temporary, self.versions)
            self.assertEqual((Path(temporary) / 'dependency.tar.gz').read_bytes(), self.payload)
            self.assertEqual(request.call_args_list,
                             [mock.call(self.versions['dependency']['url'], timeout=60)] * 3)
            self.assertEqual(sleep.call_args_list, [mock.call(1), mock.call(2)])
            self.assertFalse(list(Path(temporary).glob('*.download')))

    def test_exhausted_transient_failure_never_installs_archive(self):
        with tempfile.TemporaryDirectory() as temporary, \
                mock.patch.object(module.urllib.request, 'urlopen',
                                  side_effect=urllib.error.URLError('DNS unavailable')) as request, \
                mock.patch.object(module.time, 'sleep') as sleep:
            with self.assertRaises(urllib.error.URLError):
                module.download(temporary, self.versions)
            self.assertEqual(request.call_count, 3)
            self.assertEqual(sleep.call_count, 2)
            self.assertEqual(list(Path(temporary).iterdir()), [])

    def test_permanent_http_failure_is_not_retried(self):
        with tempfile.TemporaryDirectory() as temporary, \
                mock.patch.object(module.urllib.request, 'urlopen',
                    side_effect=urllib.error.HTTPError('https://example.invalid', 404,
                                                       'missing', {}, None)) as request, \
                mock.patch.object(module.time, 'sleep') as sleep:
            with self.assertRaises(urllib.error.HTTPError):
                module.download(temporary, self.versions)
            self.assertEqual(request.call_count, 1)
            sleep.assert_not_called()

    def test_invalid_final_bytes_are_not_installed_or_retried(self):
        with tempfile.TemporaryDirectory() as temporary, \
                mock.patch.object(module.urllib.request, 'urlopen',
                                  return_value=io.BytesIO(b'corrupt')) as request, \
                mock.patch.object(module.time, 'sleep') as sleep:
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                module.download(temporary, self.versions)
            self.assertEqual(request.call_count, 1)
            sleep.assert_not_called()
            self.assertEqual(list(Path(temporary).iterdir()), [])

    def test_existing_archive_is_verified_without_network(self):
        with tempfile.TemporaryDirectory() as temporary, \
                mock.patch.object(module.urllib.request, 'urlopen') as request:
            archive = Path(temporary) / 'dependency.tar.gz'
            archive.write_bytes(self.payload)
            module.download(temporary, self.versions)
            archive.write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                module.download(temporary, self.versions)
            request.assert_not_called()


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


class TtfKerningPatchTests(unittest.TestCase):
    ARCHIVE_NAME = 'SDL3_ttf-3.2.2.tar.gz'

    def test_the_kerning_patch_is_part_of_every_pinned_build(self):
        names = [patch.name for patch in module.TTF_PATCHES]
        self.assertEqual(names, ['kerning-moves-pen.patch'])
        portfile = (Path(module.LOCK).parent / 'vcpkg-ports/sdl3-ttf/portfile.cmake').read_text()
        self.assertIn('kerning-moves-pen.patch', portfile)
        self.assertEqual(module.json.loads(module.LOCK.read_text())['SDL_ttf']['archive'], self.ARCHIVE_NAME)

    def test_the_kerning_patch_applies_to_the_pinned_archive(self):
        # The archive is in any work directory sdl3_dependencies.py downloaded into.
        candidates = [Path(__file__).resolve().parents[2] / 'build' / name / 'sources' / self.ARCHIVE_NAME
                      for name in ('sdl3', 'sdl3-ci')]
        candidates.append(Path.home() / '.cache/glob2-sdl3/sources' / self.ARCHIVE_NAME)
        archive = next((path for path in candidates if path.is_file()), None)
        if archive is None:
            self.skipTest('the pinned SDL3_ttf archive has not been downloaded')
        import tarfile
        with tempfile.TemporaryDirectory() as temporary:
            with tarfile.open(archive) as package:
                package.extractall(temporary, filter='data')
            source = Path(temporary) / 'SDL3_ttf-3.2.2'
            module.apply_source_patches(source, module.TTF_PATCHES)
            text = (source / 'src/SDL_ttf.c').read_text()
            self.assertIn('positions->pos[positions->len - 2].x_advance += (int)pen_adjust;', text)
            self.assertNotIn('pos->x_offset += delta.x;', text)


class ShapingBuildTests(unittest.TestCase):
    def test_emscripten_c_and_cxx_agree_on_wasm_longjmp(self):
        # HarfBuzz is C++, unlike the previous SDL dependencies. A C-only
        # SUPPORT_LONGJMP flag leaves C++ at Emscripten SjLj, incompatible with
        # -fwasm-exceptions before HarfBuzz can compile.
        for threaded in (False, True):
            with self.subTest(threaded=threaded), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                lock = root / 'sdl3-versions.json'
                lock.write_text(json.dumps({'harfbuzz': {'archive': 'harfbuzz-10.1.0.tar.xz'}}))
                (root / 'sdl3-vendored.json').write_text('{}')
                work = root / 'work'
                (work / 'harfbuzz-10.1.0').mkdir(parents=True)
                with mock.patch.object(module, 'LOCK', lock), \
                        mock.patch.object(module, 'download'), \
                        mock.patch.object(module.subprocess, 'run') as run:
                    module.build(root / 'prefix', work, emscripten='/sdk', threaded=threaded)
                configure = next(call.args[0] for call in run.call_args_list
                                 if isinstance(call.args[0], list) and call.args[0][0] == '/sdk/emcmake')
                cflags = next(arg.split('=', 1)[1] for arg in configure if arg.startswith('-DCMAKE_C_FLAGS='))
                cxxflags = next(arg.split('=', 1)[1] for arg in configure if arg.startswith('-DCMAKE_CXX_FLAGS='))
                self.assertIn('-sSUPPORT_LONGJMP=wasm', cflags.split())
                self.assertIn('-sSUPPORT_LONGJMP=wasm', cxxflags.split())
                self.assertIn('-fwasm-exceptions', cxxflags.split())
                self.assertEqual('-pthread' in cflags.split(), threaded)
                self.assertEqual('-pthread' in cxxflags.split(), threaded)
                self.assertIn('-DSDLTTF_HARFBUZZ=ON', configure)

    def test_shaper_is_built_before_sdl_ttf(self):
        versions = json.loads(module.LOCK.read_text())
        self.assertLess(list(versions).index('harfbuzz'), list(versions).index('SDL_ttf'))
        self.assertEqual(versions['harfbuzz']['version'], '10.1.0')


if __name__ == '__main__':
    unittest.main()
