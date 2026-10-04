"""The pinned source SDK must apply reviewed fixes or fail explicitly."""
import importlib.util
import json
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


if __name__ == '__main__':
    unittest.main()
