#!/usr/bin/env python3
"""The readiness probe must use the requested runtime and close native handles."""
import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import MagicMock, call, patch

SPEC = importlib.util.spec_from_file_location('script_support',
    Path(__file__).resolve().parents[1] / 'content/data/fonts/check_script_support.py')
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


class ScriptSupportTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def test_prefix_never_substitutes_sdl_extension_for_core(self):
        library = self.root / 'lib'
        library.mkdir()
        for name in ('libSDL3_image.dylib', 'libSDL3_net.dylib', 'libSDL3_ttf.dylib'):
            (library / name).touch()
        with patch.dict(PROBE.os.environ, {'GLOB2_SDL3_PREFIX': str(self.root)}):
            with self.assertRaises(RuntimeError):
                PROBE.library_path('SDL3', None)
            core = library / 'libSDL3.0.dylib'
            core.touch()
            self.assertEqual(PROBE.library_path('SDL3', None), str(core.resolve()))

    def test_prefix_accepts_versioned_linux_library(self):
        library = self.root / 'lib'
        library.mkdir()
        core = library / 'libSDL3.so.0.4.16'
        core.touch()
        with patch.dict(PROBE.os.environ, {'GLOB2_SDL3_PREFIX': str(self.root)}):
            self.assertEqual(PROBE.library_path('SDL3', None), str(core.resolve()))

    def mock_runtime(self):
        font = self.root / 'font.ttf'
        fallback = self.root / 'fallback.ttf'
        probes = self.root / 'probes.json'
        font.write_bytes(b'font')
        fallback.write_bytes(b'fallback')
        probes.write_text(json.dumps({'sw': 'Kazi'}))
        args = argparse.Namespace(font=font, fallback_font=[fallback], probes=probes,
            locales=None, library='ttf', sdl_library='core', render_directory=None, size=13)
        core, ttf = MagicMock(), MagicMock()
        core.SDL_GetError.return_value = b'simulated native failure'
        ttf.TTF_Init.return_value = True
        ttf.TTF_OpenFont.side_effect = [11, 22]
        ttf.TTF_AddFallbackFont.return_value = True
        ttf.TTF_FontHasGlyph.return_value = True
        return args, core, ttf

    def test_failed_fallback_attachment_closes_both_fonts_and_quits(self):
        args, core, ttf = self.mock_runtime()
        ttf.TTF_AddFallbackFont.return_value = False
        with patch.object(PROBE.ctypes, 'CDLL', side_effect=[core, ttf]):
            with self.assertRaisesRegex(RuntimeError, 'simulated native failure'):
                PROBE.probe(args)
        self.assertEqual(ttf.TTF_CloseFont.call_args_list, [call(11), call(22)])
        ttf.TTF_Quit.assert_called_once()

    def test_failed_fallback_open_closes_only_primary_font(self):
        args, core, ttf = self.mock_runtime()
        ttf.TTF_OpenFont.side_effect = [11, None]
        with patch.object(PROBE.ctypes, 'CDLL', side_effect=[core, ttf]):
            with self.assertRaises(RuntimeError):
                PROBE.probe(args)
        ttf.TTF_CloseFont.assert_called_once_with(11)
        ttf.TTF_Quit.assert_called_once()

    def test_failed_save_destroys_surface_and_closes_fonts(self):
        args, core, ttf = self.mock_runtime()
        args.render_directory = self.root / 'renders'
        ttf.TTF_RenderText_Blended.return_value = 33
        core.SDL_SaveBMP.return_value = False
        with patch.object(PROBE.ctypes, 'CDLL', side_effect=[core, ttf]):
            with self.assertRaises(RuntimeError):
                PROBE.probe(args)
        core.SDL_DestroySurface.assert_called_once_with(33)
        self.assertEqual(ttf.TTF_CloseFont.call_args_list, [call(11), call(22)])
        ttf.TTF_Quit.assert_called_once()


if __name__ == '__main__':
    unittest.main()
