# Glob2 Sans

`sans.ttf` is the game's original DejaVu Sans 2.26 with 21,900 missing CJK
characters appended from Droid Sans Fallback. Original outlines, hinting,
advance widths, kerning, shaping tables and line metrics are preserved.
The donor outlines are scaled from 256 to 2048 units per em and imported
without their font-specific hint programs. This supports both Chinese catalogs
without changing the renderer or replacing glyphs used by existing languages.

Coverage includes the donor's CJK radicals, punctuation, kana, bopomofo,
ideographs and fullwidth forms, including characters not currently in the
catalogs. It is not complete Unicode coverage. One shared Han glyph style is
used for both Simplified and Traditional Chinese; locale-specific typeface
variants would require separate fonts and renderer support.

The original font's Bitstream/Arev notices are in `LICENSE-DejaVu.txt`.
The appended Droid outlines use Apache License 2.0; copyright and full license
are in `LICENSE-Droid.txt`. The combined font is renamed Glob2 Sans and records
the modification in its embedded metadata.

## Rebuild

Normal game builds use the checked-in font and need no Python font packages.
To reproduce the asset, use Python with `fonttools==4.64.0` and these inputs:

* Original font: `git show 88934ecfb:data/fonts/sans.ttf > /tmp/glob2-base.ttf`
  (run from this repository).
* Donor: [DroidSansFallback.ttf in Android 13 r33](https://android.googlesource.com/platform/frameworks/base/+/android-13.0.0_r33/data/fonts/DroidSansFallback.ttf).
  Download the `?format=TEXT` response and base64-decode it to
  `/tmp/DroidSansFallback.ttf`. Its upstream notice is in the adjacent `NOTICE`.

The script verifies SHA-256 hashes of both inputs before generating the font:

```sh
python3 data/fonts/build_chinese_font.py /tmp/glob2-base.ttf /tmp/DroidSansFallback.ttf data/fonts/sans.ttf
python3 test/test_font_coverage.py
```

The coverage test uses the same SDL2_ttf library as the game and fails if any
catalog character cannot be displayed. Font rebuilds should also be checked
visually at the game's 10, 13 and 20 pixel sizes, with layout measurements and
an original-glyph regression comparison.
