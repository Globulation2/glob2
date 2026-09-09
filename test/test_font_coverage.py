#!/usr/bin/env python3
"""Check bundled font coverage through the game's SDL2_ttf library."""
import ctypes
import ctypes.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FontCoverageTest(unittest.TestCase):
    def test_catalog_characters(self):
        library = ctypes.util.find_library('SDL2_ttf')
        self.assertIsNotNone(library, 'SDL2_ttf is required for this test')
        ttf = ctypes.CDLL(library)
        ttf.TTF_OpenFont.argtypes = [ctypes.c_char_p, ctypes.c_int]
        ttf.TTF_OpenFont.restype = ctypes.c_void_p
        ttf.TTF_CloseFont.argtypes = [ctypes.c_void_p]
        # Current catalogs use BMP characters; this API also works with older
        # SDL2_ttf versions that predate TTF_GlyphIsProvided32.
        ttf.TTF_GlyphIsProvided.argtypes = [ctypes.c_void_p, ctypes.c_uint16]
        self.assertEqual(ttf.TTF_Init(), 0)
        font = ttf.TTF_OpenFont(str(ROOT / 'data/fonts/sans.ttf').encode(), 13)
        try:
            self.assertTrue(font, 'Could not load bundled sans.ttf')
            for path in sorted((ROOT / 'data').glob('texts.*.txt')):
                if path.name in ('texts.keys.txt', 'texts.list.txt', 'texts.incomplete.txt'):
                    continue
                values = path.read_text(encoding='utf-8').splitlines()[1::2]
                characters = set(''.join(values))
                # Formatting controls have no visible glyph of their own.
                characters -= set('\u200c\u200d\u200e\u200f')
                missing = sorted(c for c in characters if ord(c) > 32 and (
                    ord(c) > 0xFFFF or not ttf.TTF_GlyphIsProvided(font, ord(c))))
                with self.subTest(catalog=path.name):
                    self.assertEqual(missing, [], 'Missing glyphs: ' + repr(missing))
        finally:
            if font:
                ttf.TTF_CloseFont(font)
            ttf.TTF_Quit()


if __name__ == '__main__':
    unittest.main()
