#!/usr/bin/env python3
"""Check bundled font coverage through the game's SDL3_ttf library."""
import ctypes
import ctypes.util
import json
import os
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FontCoverageTest(unittest.TestCase):
    def test_catalog_characters(self):
        prefix = os.environ.get('GLOB2_SDL3_PREFIX')
        candidates = list((Path(prefix) / 'lib').glob('*SDL3_ttf*')) if prefix else []
        library = str(next((p for p in candidates if p.suffix in ('.dylib', '.so')), '')) or ctypes.util.find_library('SDL3_ttf')
        self.assertIsNotNone(library, 'SDL3_ttf is required for this test')
        if prefix:
            core = next((p for p in (Path(prefix) / "lib").glob("*SDL3.*")
                         if p.suffix in (".dylib", ".so")), None)
            if core:
                ctypes.CDLL(str(core), mode=ctypes.RTLD_GLOBAL)
        ttf = ctypes.CDLL(library)
        ttf.TTF_OpenFont.argtypes = [ctypes.c_char_p, ctypes.c_float]
        ttf.TTF_OpenFont.restype = ctypes.c_void_p
        ttf.TTF_CloseFont.argtypes = [ctypes.c_void_p]
        # SDL3_ttf accepts Unicode code points.
        ttf.TTF_FontHasGlyph.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        ttf.TTF_FontHasGlyph.restype = ctypes.c_bool
        ttf.TTF_Init.restype = ctypes.c_bool
        self.assertTrue(ttf.TTF_Init())
        font_paths = [ROOT / 'data/fonts/sans.ttf', *sorted((ROOT / 'data/fonts/scripts').glob('*.ttf'))]
        fonts = []
        try:
            for path in font_paths:
                font = ttf.TTF_OpenFont(str(path).encode(), 13)
                self.assertTrue(font, 'Could not load bundled ' + path.name)
                fonts.append(font)
            for path in sorted((ROOT / 'data').glob('texts.*.txt')):
                if path.name in ('texts.keys.txt', 'texts.list.txt', 'texts.incomplete.txt'):
                    continue
                values = path.read_text(encoding='utf-8').splitlines()[1::2]
                characters = set(''.join(values))
                # Formatting controls have no visible glyph of their own.
                characters -= set('\u200b\u200c\u200d\u200e\u200f')
                missing = sorted(c for c in characters if ord(c) > 32 and (
                    ord(c) > 0xFFFF or not any(ttf.TTF_FontHasGlyph(font, ord(c)) for font in fonts)))
                with self.subTest(catalog=path.name):
                    self.assertEqual(missing, [], 'Missing glyphs: ' + repr(missing))
            # Filipino must work before the browser downloads supplemental fonts,
            # and without silently relying on another script's fallback font.
            native = (ROOT / 'data/texts.fil.txt').read_text(encoding='utf-8').splitlines()[1::2]
            online = json.loads((ROOT / 'platform/packages/i18n/locales/fil.json').read_text(encoding='utf-8'))
            values = native + [text for value in online.values()
                               for text in (value.values() if isinstance(value, dict) else [value])]
            characters = set(''.join(values))
            for path in (ROOT / 'data/fonts/sans.ttf', ROOT / 'browser/assets/sans-core.ttf'):
                font = ttf.TTF_OpenFont(str(path).encode(), 13)
                self.assertTrue(font, 'Could not load Filipino font ' + path.name)
                fonts.append(font)
                with self.subTest(locale='fil', font=path.name):
                    # Both catalog surfaces must render with the shipped startup subset.
                    required = characters
                    missing = sorted(c for c in required if ord(c) > 32 and not ttf.TTF_FontHasGlyph(font, ord(c)))
                    self.assertEqual(missing, [], 'Missing Filipino glyphs: ' + repr(missing))
        finally:
            for font in fonts:
                ttf.TTF_CloseFont(font)
            ttf.TTF_Quit()


if __name__ == '__main__':
    unittest.main()
