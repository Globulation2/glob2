# Glob2 Sans

`sans.ttf` is the game's original DejaVu Sans 2.26 with 33,199 missing CJK
characters appended from Droid Sans Fallback. Original outlines, hinting,
advance widths, kerning, shaping tables and line metrics are preserved.
The donor outlines are scaled from 256 to 2048 units per em and imported
without their font-specific hint programs. This supports the Chinese, Japanese
and Korean catalogs
without changing the renderer or replacing glyphs used by existing languages.

Coverage includes the donor's CJK radicals, punctuation, kana, bopomofo,
ideographs, all 11,172 precomposed Hangul syllables, and fullwidth forms,
including characters not currently in the
catalogs. It is not complete Unicode coverage. One shared Han glyph style is
used for Chinese and Japanese; locale-specific typeface
variants would require separate fonts and renderer support.

The original font's Bitstream/Arev notices are in `LICENSE-DejaVu.txt`.
The appended Droid outlines use Apache License 2.0; copyright and full license
are in `LICENSE-Droid.txt`. The combined font is renamed Glob2 Sans and records
the modification in its embedded metadata.

The browser client starts with `browser/assets/sans-core.ttf`, this font without
the appended CJK outlines, and downloads the full font later
(`browser/derive_assets.py`). Regenerate that copy whenever this font changes.

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

The coverage test uses the same SDL3_ttf library as the game and fails if any
catalog character cannot be displayed. Font rebuilds should also be checked
visually at the game's 10, 13 and 20 pixel sizes, with layout measurements and
an original-glyph regression comparison.

## Prospective script checks

The script-specific Noto Sans fonts in `scripts/` supplement the original font
without modifying its outlines or layout tables. The renderer loads them lazily
for supported script runs and uses HarfBuzz on logical text. Existing Arabic,
Persian, Latin and CJK interfaces keep their original font and bidi route; the
prospective Urdu, Shahmukhi Punjabi, Pashto and Sindhi locales use the Arabic
script font when registered.
Each newly supported complex-script locale also selects appropriate default
line metrics, including for empty strings used to size UI rows. Language changes
invalidate those metrics and cached text.

At high pixel densities, hinted script advances can differ from the authored
layout size. Scaled drawing preserves the full raster while using the authored
logical width as its destination; `rasterizeString` still returns the unmodified
physical-size raster. The preview harness records raw-raster and drawn bounds
separately so a change in raster size cannot hide a clipping failure.

The online app and server-rendered sign-in/invite pages share
`platform/packages/i18n/script-fonts.css`. Vite emits its font URLs for the app;
the API rebases them under `/signin/assets/` and serves only manifest-listed
fonts, with `LICENSE-Noto.txt`. Container builds copy both the shared stylesheet
and pinned font files. New script locales select these families before generic
fallbacks; existing locale font stacks remain unchanged.

Bulgarian uses the existing `browser/assets/sans-core.ttf` through the shared
stylesheet for body text, headings and its language-picker option. The Latin-only
design-system fonts cannot provide its Cyrillic glyphs. Vite bundles the font;
server-rendered pages explicitly serve the same bytes at
`/signin/assets/sans-core.ttf`, and the standalone launcher packages them under
`fonts/`. The DejaVu and Droid notices accompany those HTTP assets. This reuses the game
font without changing other locales' font stacks or the native renderer.

The standalone browser launcher also loads this stylesheet before WebAssembly
starts. Its build copies the pinned fonts and licence under `fonts/` and rewrites
the shared stylesheet URLs. Static packages include those files and their gzip
sidecars in the release identity. `/play/` must serve CSS as `text/css`, JSON as
`application/json` and TrueType fonts as `font/ttf`, including compressed
responses; successful downloads with the wrong MIME type do not establish that
the browser loaded a font. The deployment HTTP test exercises the production
Caddy route. Native game text inputs use the launcher font variables and refresh
their translated accessibility label when the interface language changes.

`scripts/manifest.json` pins the upstream commit, per-file hashes and sizes.
`LICENSE-Noto.txt` contains the SIL Open Font License 1.1. Verify or reproduce
the unchanged upstream assets with:

```sh
python3 data/fonts/fetch_script_fonts.py
python3 data/fonts/fetch_script_fonts.py --download
```

Both the font and SDL3_ttf shaping support must be packaged on every platform.
The managed SDL build, desktop/mobile vcpkg manifests and Flatpak configuration
enable HarfBuzz. A font-loading success without HarfBuzz is rejected by the
script-font route rather than accepted as shaped text. The built-in bidi path
resolves implicit levels for a single line; it is not a complete implementation
of explicit Unicode embedding/isolate controls. Numeric ratios/dates are kept
as LTR spans within the new RTL route.

HarfBuzz 10.1.0 is pinned in `scons/sdl3-versions.json` and matches the vcpkg
baseline. Its unmodified upstream `COPYING` notice ships as
`LICENSE-HarfBuzz.txt` alongside the font notices on native, mobile and browser
packages. It uses the Old MIT licence (the vcpkg SPDX identifier is
`MIT-Modern-Variant`). Font and shaping-library notices are distinct licences.

Before registering a new locale, run the standalone readiness diagnostic with
the exact SDL3 and SDL3_ttf libraries from the build being evaluated:

```sh
python3 data/fonts/check_script_support.py \
  --sdl-library /path/to/libSDL3.so \
  --library /path/to/libSDL3_ttf.so \
  --json-output artifacts/script-support/report.json \
  --render-directory artifacts/script-support/renders
```

`script-probes.json` exercises prospective locale scripts, combining marks,
conjuncts and mixed Latin/numeric text. These short diagnostic phrases are not
game translations or evidence of linguistic approval. `--locales` selects a
subset. `--font` and repeatable `--fallback-font` arguments allow isolated font
candidate evaluation without changing shipped assets. With no explicit library
arguments, the tool uses `GLOB2_SDL3_PREFIX`, then system discovery; record the
reported paths to avoid mistaking host libraries for packaged dependencies.

The diagnostic exits with status 1 on missing glyphs and status 2 on a loading,
rendering or configuration error. It reports the runtime HarfBuzz version, but
neither a nonzero version nor zero missing glyphs proves correct shaping. The
rendered BMPs use SDL3_ttf directly and do not exercise the game's bidirectional
processing or wrapping. Inspect complex clusters, mark positioning and numeric
or URL ordering in actual game captures at 10, 13 and 20 pixels before declaring
a script supported. Script-specific fonts must retain their GSUB/GPOS tables;
copying their Unicode outlines into another font is insufficient.
