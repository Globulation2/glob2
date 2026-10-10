# Rendering benchmarks

Use release builds and retained scenarios to measure rendering changes. Timing evidence complements visual review.

## Renderer stress measurements

`torus-render-benchmark` uses the production loaded-map renderer. Its optional
flat-map fixture places real workers,
explorers and warriors in distinct visible cells at the camera's minimum zoom.
Run from the repository root with an isolated profile:

```sh
scons release=1 server=0 torus-render-benchmark
mkdir -p artifacts/render-profile
GLOB2_USER_DATA_DIR="$PWD/artifacts/render-profile/profile" \
GLOB2_BENCH_FLAT=1 GLOB2_BENCH_SIZE=256x256 GLOB2_BENCH_UNITS=1000 \
GLOB2_BENCH_FRAMES=300 GLOB2_BENCH_CAPTURE=artifacts/render-profile/frame.ppm \
build/darwin/client/release/test/torus-render-benchmark -g -F -m -s 1280x800
```

Use the current host's release directory on Linux/Windows. `GLOB2_BENCH_MODE`
selects `2D no clouds` or `2D clouds`; otherwise both run. Set
`GLOB2_BENCH_VISIBLE=1` to show and present the completed fixture.
`GLOB2_BENCH_NATIVE_CLOUD_DETAIL=1` restores the original dense cloud grid for
a controlled comparison. `GLOB2_BENCH_BARS=1` adds health/food bars. Unit count
zero measures the same terrain without units. Retain executable hashes, commands,
GPU identity, logs and captures with before/after comparisons. The flat fixture
checks that rendering leaves the simulation checksum unchanged. New fixture headers
use seed 1; saved-game runs retain their saved seed.

For an AI match, replace `GLOB2_BENCH_SIZE` and `GLOB2_BENCH_UNITS` with
`GLOB2_BENCH_GAME=/absolute/path/to/checkpoint.game.gz`. The saved players and
entities are retained. `GLOB2_BENCH_AI_TICKS=N` advances their AI orders and
simulation for a fixed warmup before measurements. `GLOB2_BENCH_MIN_UNITS=N`
first adds units on free cells until that population is reached; the log separates
the checkpoint's natural population from this deliberately seeded stress case.
`GLOB2_BENCH_FULL_MAP=1` fits the complete map, including on nonsquare viewports,
and can go below the interactive camera's minimum zoom.
`GLOB2_BENCH_CAMERA_SWEEP=1` repeatedly changes zoom and pans across wrap seams.
Sweep measurements mix those view sizes; use a fixed camera for paired timings.
`GLOB2_BENCH_CAMERA_MOTION=1` instead pans four/two map pixels per frame and
cycles smoothly between the selected zoom and four times that zoom over 120
frames. It takes precedence over the seam sweep in the ordinary flat pass.
`GLOB2_BENCH_CAMERA_PAN=1` uses the same scrolling at a fixed zoom.
`GLOB2_BENCH_FRAME_TIMES=1` prints each measured and warmup frame; summaries
include p99 and maximum latency as well as median and p95. The benchmark finishes
deferred HD artwork loading before timing, so density changes compare identical
source artwork rather than different asset-loader progress. Keep cold frames when
investigating navigation stalls, and repeat cycles to distinguish first-use work
from recurring hitches. The motion option does not change the paired comparison's
camera; use the seam sweep for that comparison.
`GLOB2_BENCH_COMPARE_RENDERER=1` additionally compares immediate and optimized
native rendering in the same process, at the same camera and simulation state.
It reports paired process CPU timings and checks pixel differences after timing
ends. Set `GLOB2_BENCH_COMPARE_AI=1` to advance one AI tick before each pair;
combine this with the camera sweep to exercise resource changes and wrap seams.
The comparison uses the no-cloud pass, a fixed animation phase, eight warmup pairs,
and a sparse tolerance of at most 100 changed channels with a maximum delta of
1/255. That tolerance does not establish bit-exact moving-scene output. The
immediate reference retains the ordinary resource sprite batch; it disables the
mixed unit queue, texture arrays and persistent map geometry.
`GLOB2_BENCH_COMPARE_CAPTURE_PREFIX=artifacts/render-profile/pair` saves the final
pair as `pair-immediate.ppm` and `pair-optimized.ppm` for visual review.

Timings include GPU completion (`glFinish`) and exclude frame presentation, AI,
input, scene extraction and simulation work. Flat passes retain a prepared scene;
AI comparisons refresh it before each timed pair. They are renderer measurements,
not whole-game FPS.
POSIX builds also report process CPU time separately from elapsed time.
Scope timings separately report CPU submission and overlap; do not sum inclusive
scopes. The fixture is native OpenGL only; mobile uses the SDL portable renderer,
so desktop results do not qualify Android/iOS hardware performance.

The native client exports production skin artwork without menu, audio or
simulation startup:

```sh
glob2 --skin-render-info
glob2 --render-skin --manifest skin.json --texture texture.png \
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

The `skin-game-preview` diagnostic measures the colony-skin path through the real
Scene renderer. Build it with `scons release=1 skin-game-preview`, then set
`SKIN_PREVIEW_SAVE` to a two-colony saved game, `GLOB2_SKIN_PREVIEW_DIR` to a
mesh directory containing the colony-v2 `paint.webp` (a 512x512 colour atlas with
one 256x256 quadrant per model: worker, warrior, explorer, swarm) and optionally
`material.webp` (the matching 512x512 material-id map; absent means all glossy),
`SKIN_PREVIEW_CAPTURE` to a capture name, and `SKIN_PREVIEW_BENCHMARK` to a
relative capture prefix. Set `GLOB2_SKIN_PREVIEW_SWARM` to a swarm mesh id (such
as `crown`) to draw team 0's swarm with that mesh; the directory then needs its
`swarm-<id>.gsk`, and the swarm quadrant paints it. Run with `-g -m
-s800x600` and an isolated `GLOB2_USER_DATA_DIR`. `SKIN_BENCH_FRAMES` and
`SKIN_BENCH_WARMUP` control total and discarded warmup frames (defaults 45 and 5).
For signed software artwork, replace the preview directory with
`SKIN_PREVIEW_ASSIGNMENT` (a JSON file containing `origin`, `matchId` and signed
`colonySkins` tickets) and `SKIN_PREVIEW_CACHE` (an isolated cache directory).
Use `-G` for software or `-g` for OpenGL with the same assignment and save.
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

Cloud patches in the flat game and editor views now use a coarser, world-anchored
lattice when zooming out to half size or smaller. Patches retain at most their configured
1:1 size on screen; normal zoom keeps the original sampling. The field and animation
time remain unchanged, but distant clouds have less fine detail. The torus view
retains its separate sampling budget.

Point bars batch opaque fills within each bar using bounded OpenGL or SDL geometry
submissions. OpenGL outlines and translucent fills preserve their original order;
software surfaces retain their existing path. Full-map terrain and resource passes
skip fog discovery queries when `DRAW_WHOLE_MAP` already makes every tile visible.

Flat-map resources use a bounded OpenGL/portable SDL sprite batch, including
standalone frames from partial HD packs. Draws sharing a texture and alpha can join an earlier run
only when their rectangles do not overlap intervening runs. Conservative bounds
preserve the order of overlapping artwork while reducing draw submissions and
texture switches without changing sampling or allocating another texture atlas.
OpenGL texture uploads flush pending draws, and the scope flushes before leaving
the resource pass. Software surfaces and dynamic team-color sprites retain their
existing paths; cache-backed team-color surfaces cannot be deferred safely.


For comparisons with another revision, set `GLOB2_BENCH_PAUSE_PRESENTATION=1`
to freeze terrain animation and `GLOB2_BENCH_WARMUP_FRAMES` to the same number of
frames on both executables. Record cold-frame samples as well as steady-state
medians, and confirm `STEADY_CACHE pending=0` before describing results as fully
warmed. Compare complete builds from both revisions; the diagnostic immediate
path is not an untouched-master baseline.

