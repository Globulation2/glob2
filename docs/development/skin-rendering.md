# Skin rendering and diagnostics

Export production skin artwork or inspect its cost through the real Scene renderer. Asset authorization belongs to the online service; the CLI validates source identity and bounded rendering inputs.

## Export production sprites

The native client exports production skin artwork without menu, audio or
simulation startup:

```sh
glob2 assets skin-info --format json
glob2 assets render-skin --manifest skin.json --texture texture.png \
  --material material.png --output-dir sprites
```

The server authorizes entitlement before queuing this local command. The source
manifest contains `skinId`, `layout: colony-v2`, `buildingColor`, the two source
SHA-256 values, `manifestSha256`, and optional swarm mesh and integer angle. The
CLI validates the canonical manifest identity, bounded 512×512 source images,
opaque paint and discrete opaque material ids. It requires a native OpenGL
context and the pinned libwebp version in `tools/image_encoding.json`; Linux
workers use `SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2`
under `xvfb-run -a`. Run from the installed data directory.

Seven clips each contain eight directions and 32 phases, packed into four
1024×1024 pages of 64 transparent 128×128 tiles. The selected rotated swarm has
one 128×128 image. Production framebuffer readback preserves top-to-bottom image
orientation and 1.25 padding, omits the separate ground shadow, and converts
premultiplied pixels to straight alpha for storage. Each page compares lossy WebP
Q90/method 6 against lossless WebP Q75/method 4 and keeps the smaller candidate
(lossy wins ties); both use exact alpha. This shares the versioned encoding
recipe with bundled assets and considers only WebP candidates for skin sheets.
The encoder verifies unchanged alpha for both encodings and exact RGBA for
lossless output. The JSON bundle records source identity, render revision,
logical sizes, padding, frame mapping, and page hashes and byte counts.
`manifest.json` is written last; the complete staging directory is renamed
atomically. Existing output directories are never overwritten.

The render revision hashes the meshes, production shaders, view transforms,
animation mapping, layout and encoding recipe. Changing these inputs regenerates
derivatives while existing matches retain their pinned bundle.
 Focused validation
uses the `SkinAuthorization`, `SkinDownloads`, `SkinSprites` and `SurfaceCoverage`
unit suites plus the skin-render worker and API tests. To exercise the actual
worker adapter, run its opt-in `native.test.ts` under Xvfb with
`GLOB2_SKIN_RENDER_TEST_BINARY` set to the absolute built client path.

## Preview a skinned match

The `skin-game-preview` diagnostic measures the colony-skin path through the real
Scene renderer. Build it with `scons release=1 skin-game-preview`, then set
`SKIN_PREVIEW_SAVE` to a two-colony saved game, `GLOB2_SKIN_PREVIEW_DIR` to a
mesh directory containing the colony-v2 `paint.webp` (a 512x512 colour atlas with
one 256x256 quadrant per model: worker, warrior, explorer, swarm) and optionally
`material.webp` (the matching 512x512 material-id map; absent means all glossy),
`SKIN_PREVIEW_CAPTURE` to a capture name, and `SKIN_PREVIEW_BENCHMARK` to a
relative capture prefix. Set `GLOB2_SKIN_PREVIEW_SWARM` to a swarm mesh id (such
as `crown`) to draw team 0's swarm with that mesh; the directory then needs its
`swarm-<id>.gsk`, and the swarm quadrant paints it. Run with `--renderer gpu --mute --window-size 800x600` and an isolated `GLOB2_USER_DATA_DIR`. `SKIN_BENCH_FRAMES` and
`SKIN_BENCH_WARMUP` control total and discarded warmup frames (defaults 45 and 5).
For signed software artwork, replace the preview directory with
`SKIN_PREVIEW_ASSIGNMENT` (a JSON file containing `origin`, `matchId` and signed
`colonySkins` tickets) and `SKIN_PREVIEW_CACHE` (an isolated cache directory).
Use `--renderer software` or `--renderer gpu` for OpenGL with the same assignment and save.
`SKIN_BENCH_TEAMS` controls the number of colonies in the crowd; their tickets
can share or select different skins. `SKIN_BENCH_FRAME_PREFIX` captures 32
animation frames for comparison videos. Software measurements also report
decoded sprite memory.
Decoded pages compact transparent pose margins with a one-pixel filtering guard,
preserving their original resolution and placement while accounting their packed
allocation against the shared cache limit.
The harness adds a crowded diagnostic colony, advances its animation phases,
and checks that every draw preserves simulation checksums and that classic and
skinned states match. It reports first-frame cost separately from warmed mean,
p95, draw counts and `render.skins.*` preparation/geometry/raster/composite scopes.
Set `SKIN_PREVIEW_ZOOM` (0.02–5.0, clamped by the map camera) to exercise adaptive
zoom detail. Set `SKIN_PREVIEW_ADAPTIVE=0` to check skins with adaptive detail
disabled. When only overview markers and building icons are visible, the
diagnostic checks that hidden skin meshes are neither prepared nor drawn.
Frame times include presentation; scope times measure CPU submission and driver
work, not isolated GPU duration. Preserve the fixture, binaries, build inputs,
resolution, driver, counters and captures for matched comparisons; run repeated
alternating pairs without concurrent builds. Software GL results do not establish
hardware performance. The smaller `skin-preview ASSET_DIRECTORY OUTPUT_PREFIX`
renders every clip from the same 512x512 `paint.webp` and optional `material.webp`
(create both with `python3 tools/skins/make_paint.py DIR --material mixed`), each
clip sampling its own model quadrant. Its `--validate-opacity` diagnostic captures opaque, half-opacity and invisible mesh/shadow
composites and verifies that opacity changes reuse cached poses. The
`--validate-cache` diagnostic checks cache hits, repainting, material-map and
region changes, texture address reuse and atlas overflow, and saves images for pixel comparison.
