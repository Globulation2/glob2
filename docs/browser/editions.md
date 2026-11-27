# Browser editions

The browser client is built in editions: the **standard** edition, with every
feature and asset, and the **lite** edition, built for download size. Both come
from the same sources, the same release workflow and the same hosts; lite is
served beside standard under `lite/` (`/play/lite/`, `gs://glob2-browser/lite/`).

```sh
python3 tools/dev_build.py target=web release=1 edition=lite web-package
python3 platforms/web/package-static.py --source build/emscripten/client/release/lite \
    --output build/browser-static-lite
```

`buildsys/edition.py` defines the switches and presets. The standard edition
records nothing in the build identity, so its directories, caches, generated
headers and package are unchanged by the edition machinery.

## Switches

Every feature is one switch, on in standard and off in lite. Any switch can be
overridden on either edition (`edition=lite feature_editor=1`,
`feature_hd_assets=0`), and a build that matches no preset gets its own directory
(`release/lite-with_editor`), so the cost of each feature can be measured alone.

| Switch | Off means |
| --- | --- |
| `editor` | no map or campaign editor |
| `online` | no online platform client: accounts, rooms, rated play, match pages, community AIs and generators, authoring links |
| `lan` | no LAN games |
| `mcp` | no MCP automation server |
| `hive` | no Hive Mind (and no commander HUD) |
| `recording` | no gameplay recording; `GameplayRecordingDisabled.cpp` stands in for the recorder |
| `video_assets` | no H.264 sprite groups and no asset-video worker |
| `ai_maxima` | no Maxima AI; saves and replays that use it cannot load |
| `renderer_gl`, `renderer_software`, `renderer_webgpu` | the renderer is not offered; with all three off, Portable SDL draws through WebGL2 |
| `js_toolkit` | no JavaScript AIs, generator toolkit or generator studio (campaign and tutorial scripts keep QuickJS) |
| `cli` | no command-line tools beyond launching play, replays and invites |
| `harfbuzz` | SDL3_ttf without complex-script shaping |
| `sdl_extras` | SDL3_image reads WebP only |
| `hd_assets` | no high-resolution artwork |
| `music_sets` | the original soundtrack only, no music library |
| `skins` | no colony skins |
| `translations` | English only: no other catalogs, script fonts or language choice |
| `premade_maps` | no premade map library; random maps and campaigns remain |
| `unused_files` | none of the data the browser never reads (custom cursors, shell completions, the menu colony still image) |
| `page_font` | the loading page uses the browser's own font |

Compile switches leave the feature's sources out (`buildsys/sources.py`
`FEATURE_SOURCES`) and set `GLOB2_HAS_<FEATURE>` to 0 in `glob2/BuildConfig.h`;
`libs/gag/include/BuildFeatures.h` defaults every switch on. Code outside a
feature tests `#if GLOB2_HAS_<FEATURE>` where it would name the feature's symbols,
and `BuildFeatures::<Feature>` elsewhere. A disabled feature disappears from the
interface entirely: menus are built from lists, so an entry is left out rather
than disabled, and no layout keeps its place. The page reads the edition as
`globalThis.Glob2Edition`. Native builds accept the compile switches
(`python3 tools/dev_build.py edition=lite`), which keeps the gated code compiling
where builds are fast; the browser-only switches stay on there. No switch changes
what the simulation computes.

## Settings

| Setting | Standard | Lite | Effect |
| --- | --- | --- | --- |
| `webp_quality` | 90 | 75 | lossy WebP quality; terrain sheets and packed sprite groups use at most 75 ([by kind of image](../development/package-size.md#lossy-webp-quality)) |
| `sprite_target` | 75 | 70 | SSIMULACRA2 score packed sprite groups must reach when picking from the [quality curves](../development/package-size.md#sprite-quality-curves) |
| `web_opt` | `O3` | `O3` | compile and link optimization (`Oz` trades speed for about 1.7 MB) |
| `web_assertions` | 1 | 0 | Emscripten runtime assertions |
| `web_closure` | 0 | 0 | Closure-compiled JavaScript glue |
| `music_bitrate` | 0 | 24 | kbps of the music copies under `platforms/web/assets/music/<N>k/` (0: the originals) |
| `variant_blocks` | 0 | 1 | interchangeable terrain variant blocks kept per static material (0: all) |
| `native_stones` | 0 | 1 | terrain stone maps at one texel per logical pixel instead of four |
| `terrain_fallbacks` | 0 | 1 | each material with a `small_build_fallback` is drawn with that material instead |
| `single_colour_decor` | 0 | 1 | connected decor with one corner colour and no mass-meets-mass frames |

Music copies are re-encoded from the shipped files with `tools/encode_music.py`
(the moods keep their exact frame count and loop) by
`platforms/web/derive_music.py --bitrate N`, and checked in so builds need no
FFmpeg; a copy whose source changed is not used. Native-resolution stone maps keep,
of each 4×4 block, the texel native drawing samples, so native-scale terrain is
unchanged; magnified views show stone edges at native resolution. Fewer variant
blocks repeat a material's 8×8 layouts more visibly; animated materials keep
every block.

A material's `"small_build_fallback": "<key>"` names a similar material that draws
it in a small build: the export leaves the material, its frames and stone map out
and rebinds its terrain names (bindings may share a material), so maps, saves and
generators need nothing. The fallback must ship and must not fold itself
(`tools/terrain_tileset.py` checks). Single-colour decor keeps, for each corner
mask, the frame drawn where every map vertex has colour 0, so neighbours still
meet, plus that colour's interior crowns; where two masses meet each draws its
own corners. Decor frames nothing shows are dropped and the rest renumbered
(`tools/package_assets.py small_build_terrain`).

## Tuning

Lite targets about 10 MB of Brotli-compressed download per English-speaking
player (one runtime, every package it can fetch). To change what it contains:

- **Turn a switch or setting on or off:** edit the `lite` entry of `PRESETS`
  (features) or the lite column of `SETTINGS` in `buildsys/edition.py`, and the
  tables above. Try a value first as a build argument (`edition=lite
  webp_quality=60`); it builds in its own directory.
- **Add a compile switch:** add the name to `COMPILE_FEATURES` with a comment, its
  default to `libs/gag/include/BuildFeatures.h`, its sources to
  `FEATURE_SOURCES` in `buildsys/sources.py` (a `a+b` key is left out only when
  every named feature is), and gates where kept code calls into it; build
  `python3 tools/dev_build.py edition=lite` natively to find the callers. Remove
  its interface entries (`BuildFeatures::<Feature>`) rather than disabling them.
- **Add an asset switch:** add it to `ASSET_FEATURES` and to `WEB_FEATURES` when it
  only concerns browser data, and its exclusions to `edition_export` in
  `buildsys/web_assets.py`.
- **Re-encode the music** at another bitrate: `python3 platforms/web/derive_music.py
  --bitrate N`, commit `platforms/web/assets/music/<N>k/`, set `music_bitrate`, and
  remove the copies no edition uses.
- **Fold a terrain material:** add `"small_build_fallback": "<key>"` to its
  `content/data/terrain/materials/<key>.json`; remove the field to ship it again.

## Measuring

`tools/web_size_report.py` measures a packaged edition (raw, gzip and Brotli,
per category, startup and the total one English-speaking player downloads) and
keeps a ledger of labelled steps. `tools/web_size_attribution.py` attributes the
runtime's WebAssembly code to source areas, from the symbol map an edition links
with. Measure every step against the same baseline and record the table in the
pull request, not in the repository.
