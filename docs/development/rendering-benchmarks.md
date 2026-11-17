# Rendering benchmarks

Use perf builds ([build tiers](build-options.md#build-tiers)) and retained scenarios to measure rendering changes. Add `sprite_packing=video optimized_assets=1` when the measurement needs the shipped asset paths. Timing evidence complements visual review.

## Renderer stress measurements

`torus-render-benchmark` uses the production loaded-map renderer. Its optional
flat-map fixture places real workers,
explorers and warriors in distinct visible cells at the camera's minimum zoom.
Run from the repository root with an isolated profile:

```sh
python3 tools/dev_build.py perf=1 torus-render-benchmark
mkdir -p artifacts/render-profile
GLOB2_USER_DATA_DIR="$PWD/artifacts/render-profile/profile" \
GLOB2_BENCH_FLAT=1 GLOB2_BENCH_SIZE=256x256 GLOB2_BENCH_UNITS=1000 \
GLOB2_BENCH_FRAMES=300 GLOB2_BENCH_CAPTURE=artifacts/render-profile/frame.ppm \
build/darwin/client/perf/test/torus-render-benchmark --renderer gpu --no-fullscreen --mute --window-size 1280x800
```

Use the current host's perf directory on Linux/Windows. `GLOB2_BENCH_MODE`
selects `2D no clouds` or `2D clouds`; otherwise both run. Run
each mode in a separate fresh process with identical warmup and camera settings
when comparing cloud cost: running both shares terrain and device caches, so the
second mode benefits from the first mode's work. Alternate process order across
repeated pairs to limit drift from temperature and background activity. Set
`GLOB2_BENCH_VISIBLE=1` to show and present the completed fixture; otherwise, on
macOS, the window is created hidden and never activates the process
(`GLOB2_TEST_HIDDEN_WINDOWS`, see [test/README.md](../../test/README.md)), so
repeated captures don't steal focus. `software-render-benchmark` does the same
unless `PROFILE_VISIBLE` is set.
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
`GLOB2_BENCH_CAMERA_ZOOM=1` runs the smooth zoom cycle with a stationary
map origin. Run it on its own to separate zoom from panning.
`GLOB2_BENCH_TERRAIN_PIXELS_PER_TILE=32` or `16` requests a constant
terrain page density in the flat GPU fixture. With the variable unset, the
fixture uses the normal adaptive page density. Compare both densities with
and without `GLOB2_BENCH_PAUSE_PRESENTATION=1` using the same camera path.
Inspect the reported sampling resolution and downsample before attributing a
result to fixed-density reuse. If the view cannot fit the requested density,
the renderer can still fall back to its normal streaming path.
`GLOB2_BENCH_FRAME_TIMES=1` prints each measured and warmup frame; summaries
include p99 and maximum latency as well as median and p95. Per-frame records
include estimated resident terrain texture bytes within the cache accounting,
and, on the portable SDL renderer, `FRAME_UPLOADS` with the texture uploads and
bytes since the previous frame. `GLOB2_BENCH_CAPTURE_FRAMES=60,61,62` with
`GLOB2_BENCH_CAPTURE_PREFIX` saves those frames, for example over a fixed camera
(`GLOB2_BENCH_PAN_X`/`Y`, `GLOB2_BENCH_ZOOM`) on open water: consecutive frames
show whether every animated cell steps together.
They also include process CPU time captured before diagnostic output on platforms where
`clock()` measures CPU time; Windows omits that field. The benchmark finishes
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

Timings include GPU completion (`glFinish` on legacy GL, synchronous readback on
device backends) and exclude final frame presentation, AI,
input, scene extraction and simulation work. Flat passes retain a prepared scene;
AI comparisons refresh it before each timed pair. They are renderer measurements,
not whole-game FPS.
POSIX builds also report process CPU time separately from elapsed time.
Scope timings separately report CPU submission and overlap; do not sum inclusive
scopes. Check the reported active backend to catch renderer fallback.
Desktop results do not qualify Android/iOS hardware performance.

### Resource rows and terrain residency

Use `GLOB2_BENCH_RESOURCE_ROWS=1` with the 512×64 synthetic map, Original
artwork, and legacy OpenGL. Repeat with `GLOB2_BENCH_CAMERA_PAN=1` to
include wrapped row selection. The
`FRAME_RESOURCE_ROWS` counters report eligibility, frame-vector construction,
and animated cell visits separately.

For terrain residency, use `GLOB2_BENCH_TERRAIN_SWITCH=pages` with at least 72
frames and zero warmup. The benchmark draws pages for four frames, direct
terrain for 64 frames, then pages again.
`FRAME_TERRAIN_RESIDENCY` reports direct atlas bytes, page texture bytes,
retained page bytes, and peak combined texture bytes for the view. Compare
the first frame after the switch and the final capture. Use
`GLOB2_BENCH_TERRAIN_SWITCH=hd` to return through the HD artwork path, or
`GLOB2_BENCH_TERRAIN_SWITCH=tiled` to exercise the tiled-capture draw options.
Starting on programmable OpenGL, use `GLOB2_BENCH_TERRAIN_SWITCH=backend` to
switch to legacy OpenGL and draw pages there on frame 68.

### Direct terrain discovery and OpenGL diagnostics

Set `GLOB2_BENCH_FOG=1` and `GLOB2_BENCH_DISCOVERY_CROSSING=1` on a
synthetic map with at least 32 tiles on both axes. Place tiles (15, 15) and
(16, 16) in the viewport. The fixture reveals those tiles on frame 4.
The cumulative `FRAME_DIRECT_DISCOVERY` counter tracks rebuilt cells.
Capture before and after the reveal to verify the boundary transition.

On legacy OpenGL, `GLOB2_PROFILE_GL_ARRAYS=1` reports live, retired, and free
array slots and allocated bytes. `GLOB2_BENCH_ARRAY_CHURN=1` replaces the same
16 source textures across 21 cycles to verify that promotions continue after
the page budget fills.

On programmable OpenGL, full opaque redraws swap the backing color textures.
Check partial redraws and discarded frames against the committed image.

### Direct terrain

Direct terrain is the default for flat views with Original artwork
(`highResolutionArtwork=0`) on desktop legacy OpenGL, programmable OpenGL,
SDL GPU with the direct terrain shaders, and WebGPU with the WGSL assets.
It uses the map's terrain variants, coverage, contact bands, surf, and
discovery. Desktop legacy OpenGL, SDL GPU and programmable OpenGL draw every
view, HD artwork included, with the streamed direct pass described below. On the other backends, HD artwork, editor views,
tiled captures, and backends without direct terrain support use composed pages.

The shader blends in floating point and uses linear atlas sampling. The page
renderer can box-filter composed terrain when zoomed out. Compare captures
at native zoom and during motion to check color and distant texture detail.

`FRAME_DIRECT_TERRAIN` reports cumulative draws and upload bytes plus
resident texture bytes. A rising draw count confirms direct terrain
drew the frame. Legacy OpenGL uploads mixed coverage progressively, starting
with the current viewport. Once coverage is resident, camera motion and
animation leave upload bytes stable. Terrain, asset bundle, or registry changes
rebuild the atlases.
Discovery changes can upload a separate visibility texture. On programmable GL,
`FRAME_DEVICE` also reports physical uploads for the whole renderer. Compare capture
pixels, frame p95 and p99, startup time, and resident terrain memory on the same
map and camera path.

Legacy GL draws all terrain with one direct pass at every zoom, in games, the
editor and tiled captures. The map's per-cell tables are built once, in 3 ms
slices while the overview shows, and terrain edits update only the cells near
a changed vertex. Each frame the pass streams the visible variants' artwork into
one atlas near the display density (8 to 128 pixels a cell), and background
workers build a coverage mask for each visible mixed cell into one atlas at the
same density (8 to 128 texels a side), nearest the view centre first. Until its
mask lands a mixed cell draws its previous-density mask after a zoom, or else
its whole-map base mask; `DIRECT_TERRAIN_BASE` reports when the base covers
the map, and only cells without a base mask yet draw a straight split between their corners. `FRAME_HD` reports the
visible mixed cells still waiting. The editor and captures complete the view
first: they build every visible mask in parallel and wait for streamed HD
artwork.

The pass filters its atlas tiles from the sprite frames themselves, so it only
declares the view's HD stream window to the material compositor and keeps none
of the compositor's CPU textures. HD material and decor frames are requested
only above 32 pixels a cell: the native frames are those frames reduced by
four, which `tools/artwork` guarantees when it renders both, and a zoomed-out
view, which shows the most variants, would otherwise decode them all to filter
them back down. Frames that leave
a stream window are retired, not deleted: they stay decoded, most recently
retired first, until they exceed a shared budget (192 MiB on desktop, 32 MiB on
mobile and in the browser) or have gone unused for 20 seconds, so panning or
zooming back decodes nothing. `image_decodes` in the
[memory report](#renderer-memory) shows what a camera path still decodes.

Below that threshold the pass filters from the native frame even while an HD
frame is still resident, and shipped materials are sampled in place, without a
converted copy; only art that takes its border from another variant is
prepared first. A density change re-filters every visible variant at once
(over a thousand tiles when it crosses to HD frames): the render thread gathers
each tile's source and uploads the results, and helper threads, started only
for batches of 32 tiles or more, do the box filtering in between.

The atlases give memory back too. The coverage atlas a zoom retires stays as a
spare, so a run of zooms allocates nothing, and is freed once the view has been
still for ten seconds. A density change takes the 2048-texel coverage or
artwork atlas again when the view fits it twice over. Once the whole-map base
mask is complete it does more than cover for missing masks: at its own density
(eight texels a cell on maps up to 256 cells) the view draws from it alone and
keeps no coverage atlas, and while a cell is under 32 pixels wide, cells that do
not fit the 2048-texel atlas draw the base instead of growing the atlas to 4096
texels. The terrain sheet's HD atlas is not built on desktop legacy GL, where
only the menu backdrop draws that sheet, from its standalone frames.

Standalone HD frames keep no decoded pixels on desktop legacy GL once they are
uploaded: the texture is the only copy. That covers the unit sheet, the
largest HD family, and buildings, effects and the cloud. The team-colour
shader draws every frame that has a team layer, units and buildings alike, as
one quad from the two layers' textures, so no recoloured copy of a team layer
exists in memory or on the GPU for each team. The exceptions are read as
pixels later: frames an atlas is built from, terrain materials, which the
compositor samples (`Sprite::keepHighResolutionPixels`), and, without the
shader, team layers, which a CPU recolour then clones. A context rebuild (a
renderer switch) loses the textures, so those sprites draw native frames at
once and reload HD from disk on the asset workers.

Those textures also follow the zoom. HD frames hold four texels for each
logical pixel, more than a zoomed-out view samples, so a detail band leaves out
their largest mip levels: full detail where a logical pixel covers more than two
screen pixels, the 64-texel level above one, the 32-texel level otherwise. The
display's scale is the floor, since the interface draws sprites at their
logical size, and it picks the band before any map is drawn. More detail loads
at once and less only after the view has wanted it for ten seconds; either way
the sprites reload one at a time on the asset workers while their current
textures keep drawing, and the allocator returns the decode buffers afterwards
(`malloc_trim` on glibc). Once a frame's HD layers are published its native
textures are freed too: their pixels stay as the floor, and a draw that falls
back to them uploads them again. `hd_gpu_bytes` in `RENDER_MEMORY_SPRITE`
separates HD from native texture bytes.

Pages serve the software and programmable-device renderers, and the
browser's WebGL2 renderer: it drives Legacy GL through Emscripten's WebGL2 bridge,
which the direct pass does not support. There pages draw native artwork until the
view's `hd-terrain-*` families download, then HD.
Direct terrain stops (`DIRECT_TERRAIN_FALLBACK`) only for device limits, such as
a map wider than the maximum texture size; Legacy GL then shows the overview. On programmable GL, actual GPU uploads happen during frame submission,
where device-wide memory pressure can defer the frame. Far zoom uses the shared overview. For a large-map admission check,
set `GLOB2_BENCH_SIZE=512x64`, `GLOB2_BENCH_SPARSE_TERRAIN=1`,
`GLOB2_BENCH_UNITS=0`, and `GLOB2_BENCH_ZOOM=0.55`. The sparse fixture uses
32-cell terrain blocks so it exercises map dimensions without a dense
coverage atlas. GPU time for a terrain-only draw and elapsed time
for the complete renderer through GPU completion are separate measurements.

On device renderers, use `GLOB2_RENDER_EXECUTION=parallel` to test building
direct terrain coverage with presentation workers. Legacy OpenGL prepares
coverage on the graphics thread in time slices and draws the overview beneath
pending tiles. Measure the first metadata upload separately from subsequent
coverage uploads.

For comparisons with another revision, set `GLOB2_BENCH_PAUSE_PRESENTATION=1`
to freeze terrain animation and `GLOB2_BENCH_WARMUP_FRAMES` to the same number of
frames on both executables. Record cold-frame samples as well as steady-state
medians, and confirm `STEADY_CACHE pending=0` before describing results as fully
warmed. Its `stale_artwork` counts cached map rows replayed at their previous
resolution while newly streamed HD frames wait to be recorded into them; it
settles once streaming does. Compare complete builds from both revisions; the
diagnostic immediate path is not an untouched-master baseline.


See [skin rendering](skin-rendering.md) for production sprite export and skin preview diagnostics.

## Renderer parity

Every renderer may draw by its own path, but once a view has been held still
long enough for streaming and background composition to finish, the GPU-backed
renderers (`legacy-gl`, `portable-sdl`, `sdl-gpu`, `programmable-gl`) must
show the same picture. `tools/rendering/renderer_parity.py` checks that over a
matrix of scenes. It draws each scene with each renderer in a fresh process and
profile (renderers the host cannot create are skipped and listed), holds the
camera for `--settle-frames` (600 by default, about ten seconds: long enough
for legacy GL's HD band reload after a zoom) and captures the last frame.

```sh
python3 tools/dev_build.py perf=1 glob2 torus-render-benchmark
python3 tools/rendering/renderer_parity.py --prepare --quick --label mine   # everyday: ten scenes
python3 tools/rendering/renderer_parity.py --label mine                    # renderer PRs: the whole matrix
python3 tools/rendering/renderer_parity.py --list-scenes
```

`--quick` runs the everyday subset (ten scenes, about two and a half minutes
with `--jobs 8` on a 32-thread Linux desktop with one discrete GPU); renderer
PRs run the whole matrix (65 scenes, about sixteen minutes there). A renderer
process takes 15 to 25 seconds per scene, most of it the settle frames;
`--timeout` (300 seconds) bounds a hang. Repeated runs give identical numbers. `--scenes` names scenes and `--filter` takes a
regular expression over their names, such as `--filter hidpi` or
`--filter '^unit-'`; `--renderers` limits the backends. `--prepare` makes the
saves the scenes load: the memory scenario's Honeycomb 256 save at tick 30000,
and the same game played on to tick 40000 (`honeycomb-256-tick-40000.game.gz`),
which has fights, damaged buildings and swimmers. It needs a perf `glob2`.

The matrix (`matrix()` in the script) covers:

- the local colony at every zoom (overview, far, 0.5x, 1x, 1.5x, 2x, 3x, 4x),
  either side of the [HD sprite policy's](../architecture/rendering.md#hd-sprite-policy)
  threshold (0.9x, 1.1x), on a 2x display, and with HD artwork off;
- building states and unit actions, framed by the benchmark's
  `GLOB2_BENCH_FOCUS` as their owner sees them (`GLOB2_BENCH_TEAM`): a
  construction site, an upgrade, a complete building selected, a damaged
  building, a flag, a worker carrying a resource, an explorer, a warrior,
  warriors fighting and a swimmer, some also on a 2x display or with HD off;
- the bare map with every tile visible: the torus corner and wrap seams, the
  Honeycomb hexes (stone, sand, forest and fields, a bridge, a shore with
  algae), Oazis, Archipelago, Sand River, Wild River (not square), Mazury and
  SmallForTwo, at several zooms, with areas and with health bars.

Inputs are pinned so renderers draw the same thing: device tier `high`
(`--device-tier`), HD artwork as each scene says (`--hd on|off` pins it for
all), the desktop HUD (`GLOB2_MOBILE_UI=0`), a fixed animation phase, no
clouds, particles or unit interpolation, and whole-tile camera positions. Each
log's `BENCH_SETTINGS` line records the resolved settings. A `-hidpi` scene
emulates a 2x display the way a Retina screen behaves: the window is twice
`--window-size` in pixels and `GLOB2_UI_SCALE=2` keeps the interface at
`--window-size` logical points, so the captures are 2560 by 1600 pixels of a
1280 by 800 screen. On macOS `--hidpi native` (the default there) draws them
like the other scenes, relying on a real HiDPI display. SDL GPU and
programmable GL need their shaders: pass `--shader-dir` (or set
`GLOB2_RENDERER_SHADER_DIR`), or `--glslang PATH` to generate them with
`tools/shaders/generate.py` first. On Linux the runner sets
`SDL_VIDEO_DRIVER=x11` when a display is available, and turns off waiting for
vertical blank (`__GL_SYNC_TO_VBLANK=0`, `vblank_mode=0`): a swap on the hidden
window otherwise costs legacy GL over a minute of start-up. Runs need a real
display (in an agent session, outside its sandbox). On macOS the windows are
created hidden. `--jobs` (6 by default) draws that many renderer processes at
once across scenes; the captures do not depend on it. `--reanalyse` compares
a label's existing captures again without drawing them, for example after
changing `--max-over-8` or `--shift`.

For each scene the runner builds a consensus image, the per-pixel median of the
GPU renderers' captures, and measures each renderer against it and each GPU
pair against each other. A pixel takes the smallest difference to the other
image within `--shift` pixels (1 by default), so an edge one pixel out of place
does not count while a missing sprite or a wrong colour does. **The bar:** a
scene fails when a GPU renderer has more than the bar's share of its pixels
(2% without HD block compression, 5% with it) more than 8 levels from the consensus, or two GPU renderers
are that far apart, or a GPU renderer fails to draw it. HD sprite frames are
block-compressed by default (`GLOB2_HD_BLOCK_COMPRESSION=0` turns that off), and
their 4x4 blocks differ from the portable renderer's RGBA at team-colour edges by
up to about 2.6% of a zoomed-in screen (legacy GL, which uploads plain BC3, up to 3.9%). So the runner draws every scene twice by
default (`--compression both`): with compression off under `<label>/off`, where
the renderers must agree within 2% (`--max-over-8-uncompressed`), and with it on
under `<label>/on`, the shipped configuration, within 5% (`--max-over-8`). It then
measures each renderer against itself across the two passes
(`<label>/compression.md`): portable SDL and the software renderer never
compress and must match themselves exactly, so any difference there is renderer
nondeterminism; on the others the difference is what compression alone costs,
and more than `--max-over-8` fails the run. `--compression on` or `off` draws a
single pass. The report groups the
GPU renderers that agree within the bar and names the outlier: the odd one out
in a three-to-one split, or `split: a+b / c+d` when they divide evenly. The
consensus is a majority, not the truth: when two or three renderers share a
defect (say, blurred terrain), the correct one is the outlier, which is why the
sheets must be looked at. A failing scene therefore also compares the detail
each GPU renderer holds over the pixels where they disagree (the luminance's
squared difference from its 3x3 neighbourhood). Where one group holds less than
`--sharpness-split` (0.6) of the detail of the next sharper one, the report
names the split instead of the outlier: `HD residency mismatch: sharp a /
soft b+c+d` in a scene above a density of one with HD on, where the
[HD sprite policy](../architecture/rendering.md#hd-sprite-policy) has every
renderer draw HD, and `detail split` elsewhere. It adds `(the consensus
follows the soft ...)` when the soft renderers are the majority, so the bar
blames the renderer that does draw HD. `summary.json` keeps each renderer's
detail relative to the sharpest (`sharpness`). `--reference NAME` adds each renderer's distance from one renderer. The
software renderer is drawn and measured for information only, so a crash or a
blank capture still shows, but it is left out of the consensus and the exit
status: it has no filtering, draws native artwork at every zoom and leaves out
several effects. The run exits 1 when any scene fails.

Results go to `artifacts/renderer-parity/<label>/`: `index.html` shows every
scene's contact sheet in one page to scroll through, failures marked;
`<scene>.png` is the sheet: the GPU captures side by side at full logical size
(2x scenes at half their pixels), each renderer's heat map against the
consensus (yellow to red where a pixel differs by more than 8 levels), the
software capture, then nearest-upscaled crops, every renderer side by side
with the consensus and a difference panel, of the densest differences, a
stretch of flat terrain and the view's centre (the framed unit or building).
`report.md` is the scene-by-renderer table of consensus distances with the
worst pair, and `summary.json` holds every metric, crop position and the
seconds each renderer took; each scene's directory keeps the captures (PNG)
and gzipped logs.

**Look at the sheets, not only the numbers.** A pass under the bar can still hide a
visible bug (a blurred texture, a missing overlay, a wrong scale or team
colour), and a fail can be harmless sub-pixel noise. A renderer PR includes a
visual review: open `index.html`, look at every failing scene and a sample of
passing ones across zooms, 2x display and HD on and off, and say in the PR what
the crops show, with the most telling sheets attached.

`tools/rendering/parity_diagnose.py FIRST.png SECOND.png --region panel|topbar|map|all|x0,y0,x1,y1`
explains one difference that the parity run found (`--scale 2` for a 2x
scene's captures). It reports how much of the difference remains after the
best whole-pixel and half-pixel shifts (position or rounding), the share on
edges as against flat areas (filtering lives on edges; a wrong colour, blend
or missing layer shows on flat areas too), the mean signed difference on
differing edges (premultiplication or blending darkens or brightens fringes
consistently), and the ratio of gradient energy (one side softer: linear
against nearest sampling, or another mip level).

### Browser renderers

`tools/rendering/browser_parity.py` measures the browser's WebGL2 renderer
(legacy GL through Emscripten) against a native run's consensus with the same
metric. It draws the scenes with `torus-render-benchmark` built for the
browser (`web-render-parity` in `cmake/web.cmake`: the release build's game
objects and asset packages, never packaged), loads the same save with the same
pinned inputs and settle frames in headless Chromium
(`platforms/web/benchmarks/render-parity.cjs`), and writes sheets, heat maps
and `report.md` to `artifacts/browser-parity/<label>/`. The browser always
uploads RGBA, so it compares with the native compression-off pass (`<label>/off`,
or a `--compression off` run) and its bar is 2%.

```sh
python3 tools/rendering/renderer_parity.py --prepare --quick --compression off --label native
python3 tools/dev_build.py target=web release=1 web-render-parity web-package
python3 tools/rendering/browser_parity.py --quick --native-label native --label mine
```

Chromium draws through ANGLE on SwiftShader (as CI does). A hardware ANGLE
backend (`--angle`) needs `--allow-gpu`: never run it on a machine that is
doing native GPU work at the same time, since a browser sharing the GPU with
native benchmarks has hung the driver. Run the native captures first, and the
browser after they finish. The browser's WebGPU renderer cannot run this
benchmark: its frame loop blocks the application worker and completes each
frame with a synchronous readback, which the browser's WebGPU device does not
offer. On a desktop smaller than a `-hidpi` scene's window (2560 by 1600 by
default) the window manager resizes some renderers' windows, so the native
captures differ in size and have no consensus; draw those scenes with a smaller
`--window-size` (such as `640x400`) under their own label and compare the
browser with that label.

The benchmark options behind it: `GLOB2_BENCH_GUI=1` draws the game screen in
the flat fixture, centred on the local colony at `GLOB2_BENCH_ZOOM`, with
`GLOB2_BENCH_PAN_X`/`_Y` moving it by that many tiles;
`GLOB2_BENCH_FOCUS=tile:X,Y` or a kind (`site`, `upgrade`, `repair`,
`damaged`, `complete`, `flag`, `carrying`, `explorer`, `warrior`, `fighting`,
`swimming`) centres it on that tile or on the first such building or unit, the
local team's first, and fails when there is none; `GLOB2_BENCH_TEAM=N` views
the game as team N's player; `GLOB2_BENCH_SELECT=unit` or `building` selects
the local team's first one; `GLOB2_BENCH_HD=0|1` pins HD artwork. In the bare
map, `GLOB2_BENCH_PAN_X`/`_Y` give the top-left tile. All of them only read the
game state. `torus-render-benchmark` also captures the software renderer.

## Terrain fallbacks

`tools/rendering/fallback_scenario.py` measures how much terrain the view draws
from fallbacks while terrain composes in the background. It loads the memory
scenario's save (or `--save` another, such as a short game on a 512 by 512
map, whose overview the cache cannot hold whole) in
`torus-render-benchmark` with `GLOB2_BENCH_CAMERA_SCRIPT=fallback`: from the
first frame of the game, cycles of eight seconds at 60 frames a second, each
starting with a jump to a far place (as a minimap click), then a hold, a pan at
fifteen tiles a second, six wheel steps out towards the overview, a hold, six
steps back and a hold. Each frame prints `FRAME_FALLBACK` with the visible cells
by source (`TerrainFallbackMetrics`): pages drawn from another density, missing
pages over the overview backdrop, pages fading in, and overview cells drawn from
the other density, the whole-map base, or nothing (transparent). The runner
prints the frames and share of visible cells per source and, for each kind of
camera event, the median and longest time until a frame shows no fallback, and
how often the next event came first.

```sh
python3 tools/dev_build.py perf=1 torus-render-benchmark
python3 tools/rendering/fallback_scenario.py --label master --renderer portable-sdl
python3 tools/rendering/fallback_scenario.py --label change --renderer portable-sdl --compare master
```

`--workers N` measures a phone-class worker count, `--hd` switches HD terrain on
after about a second, and results go to `artifacts/render-fallback/<label>/`.

`GLOB2_BENCH_RAPID_ZOOM=N` replaces the script's slow wheel with a player
spinning it: a step every N frames, out `GLOB2_BENCH_RAPID_DEPTH` steps (6 by
default) and straight back, over the cycle's zoom window. That is the case where
an art atlas is still filling when the next density change arrives.

## Terrain in motion

The parity matrix compares settled frames, so it cannot see what a moving view
draws while terrain refills. `tools/rendering/motion_parity.py` plays the
fallback script with a fast wheel (two frames a step, three steps out) twice
per renderer: once as a player sees it, and once with
`GLOB2_BENCH_COMPLETE_TERRAIN=1`, which completes every frame's terrain first.
It compares the captured frames of the pan and the wheel in blocks of 1/80 of
the width. Stand-ins may be softer, but a block whose mean colour differs by more
than 24 levels in a channel shows foreign art: another variant's tile, an empty
slot or a hole. It also reports frame times (median, p99, maximum, frames over
16.7 and 33.3 ms) from a third run without captures, whose readbacks would stall
frames, and the direct pass's `FRAME_DIRECT_ART`
counters (variants drawing the resident low-resolution art, and stale records,
which must be zero).

```sh
python3 tools/dev_build.py perf=1 torus-render-benchmark
python3 tools/rendering/motion_parity.py --label master
```

It exits 1 when a renderer shows foreign blocks or a stale record, and writes
`summary.json` and each renderer's worst frame beside its complete frame to
`artifacts/render-motion/<label>/`. Run it with the parity matrix for any change
to terrain streaming, atlases or stand-ins.

## Renderer memory

`tools/rendering/memory_scenario.py` runs the standard memory scenario and reports
peak memory by owner. It loads a mid-game save of a 256×256 Honeycomb Isles map
in `torus-render-benchmark`, draws the flat game view on legacy OpenGL (`--renderer` picks
another backend, such as `sdl-gpu` or `portable-sdl`) with HD artwork, and pans while cycling the zoom between the minimum and four times
that; `--zoom 1` cycles from one to four instead, where the portable renderer
draws HD sprites and pages. Build the perf tier first; `--prepare` generates the save (map seed 7,
four AIs, tick 30000) under `artifacts/render-memory/`.

```sh
python3 tools/dev_build.py perf=1 glob2 torus-render-benchmark
python3 tools/rendering/memory_scenario.py --prepare --label master
python3 tools/rendering/memory_scenario.py --label change --compare master
```

The table it prints goes in the pull request. Peak process resident memory and,
on Linux, the GPU driver's per-process memory (`drm-memory-*` in
`/proc/<pid>/fdinfo`, or `nvidia-smi` on the NVIDIA driver, which has no fdinfo
memory) come from sampling the process; an integrated GPU's driver memory is
system RAM. The remaining rows are the renderer's own tracked bytes,
which the benchmark prints as `FRAME_RENDER_MEMORY` with `GLOB2_BENCH_FRAME_TIMES`
and as `RENDER_MEMORY` when a pass ends:

| Field | Meaning |
| --- | --- |
| `cpu_surface_bytes`, `cpu_surfaces` | Decoded pixels that live surfaces still hold, and how many surfaces hold some |
| `cpu_high_resolution_bytes` | The HD artwork share of those pixels |
| `cpu_prepared_bytes` | Upload staging not yet released: converted pixels, mip chains, compressed blocks |
| `cpu_compositor_bytes` | The terrain compositor's own copies of material pixels |
| `gpu_surface_bytes` | Sprite and atlas textures |
| `gpu_array_bytes` | Texture-array pages, which duplicate their source textures |
| `gpu_direct_terrain_bytes`, `gpu_terrain_page_bytes` | Direct terrain atlases and composed terrain pages |
| `cpu_terrain_page_bytes`, `cpu_terrain_page_pixel_bytes` | The page cache's budget accounting (pixels, bookkeeping and texture reservations) and the CPU pixels its pages actually hold |
| `gpu_backend_texture_bytes`, `gpu_backend_peak_texture_bytes` | Textures the portable SDL renderer holds, current and peak, which no surface accounts for; on phones they share system memory |
| `gpu_frame_cache_bytes`, `gpu_skin_atlas_bytes`, `gpu_view_tile_bytes` | Render targets no surface owns |
| `device_resource_bytes` | A render device's own total (SDL GPU, Programmable GL, WebGPU): resident textures, depth, geometry and staging; the `gpu_*` surface rows stay zero on these devices |
| `device_snapshot_bytes`, `device_retained_bytes` | The logical CPU copies `RenderResources` keeps so a device can restore its textures, and the part retained for surfaces whose own pixels were released |
| `image_decodes`, `image_decoded_bytes` | Image decodes since launch and the pixels they produced; growth during camera motion is streaming churn |
| `peak_rss_bytes` | The process's peak resident set (zero on Windows and in the browser) |

`RENDER_MEMORY_SPRITE` lists the sixteen largest sprites with their native and
HD pixel bytes and texture bytes. Tracked bytes are what the renderer asked for,
not what the driver or the allocator holds: compare them with the sampled
process and driver figures, and expect the default framebuffer and driver
overhead on top.

Streaming is asynchronous, so unpaced frames outrun the asset workers and show
almost no churn. `GLOB2_BENCH_FRAME_INTERVAL_MS=16` pauses between frames,
outside the timed interval; the scenario sets it. `GLOB2_BENCH_SETTLE_FRAMES=600`
first holds one view (the closest zoom of the cycle) for that many frames, as a
game does before the player moves, so background work such as the whole-map
base mask finishes; a camera that never rests starves that work. Camera motion applies to the
flat pass, so a saved-game memory run needs `GLOB2_BENCH_FLAT=1` as well as
`GLOB2_BENCH_GAME`.

`--trim-at N` (`GLOB2_BENCH_TRIM_AT=N`) sends a low-memory presentation trim
before measured frame N. The benchmark prints `PRESENTATION_TRIM` with the device
registry's snapshot bytes and the device's resource bytes, then
`PRESENTATION_TRIM_AFTER` every ten frames for sixty frames; compare them, and
the `FRAME_RENDER_MEMORY` lines around frame N, to see what the trim freed. Set
`GLOB2_RENDER_BUDGET=mobile` to run the scenario under the phone budgets.

In the game client, legacy OpenGL records the same tracked GPU total as the
`render.resource_bytes` gauge, and `GLOB2_TEAM_TIMELINE=1` adds a
`GLOB2_RENDER_MEMORY` line with these fields once a second
([performance telemetry](performance-telemetry.md#rendering-and-gpu-timing)).

## CPU presentation batches

Build `software-render-benchmark` and `torus-render-benchmark` with `perf=1`.
Use the same retained save, seed, animation, viewport, warmup and compute budget
for `GLOB2_RENDER_EXECUTION=immediate`, `serial` and `parallel` in separate processes.
Both standalone tools apply the launch `--compute-threads auto|N` budget to their
map executor; keep that budget fixed between modes.
The former `auto` spelling is a compatibility alias for `immediate`; neither
spelling dynamically chooses batching or workers.
`immediate` measures direct serial drawing; `serial` measures packed CPU
terrain and recorder overhead; `parallel` requests worker execution where bounds
permit it. Use `GLOB2_RENDER_WORKERS=1` to test the rendering concurrency ceiling
without changing simulation configuration. Retain an untouched base executable
and captures too: the diagnostic immediate path shares current kernels and is
not an independent pixel reference.

The saved-game benchmark reports draw/present p95 and p99, process CPU, terrain
jobs/cells/reservation, raster tiles/references/source-copy bytes, serial batches,
fallback operations, summed queue delay/worker activity and owner wait. Summed
worker activity is elapsed callback time, not process CPU or batch latency.
Counters exclude warmup; peak retained/reserved bytes cover the entire run.
Terrain cache counters describe the retained view cache. Streaming uses temporary
page caches; use executor counters and terrain scopes to observe their CPU jobs.
Scope records separate material preparation, terrain planning/install, command
capture, source copying, binning and raster execution. Inclusive scopes must not
be added together. Counters are owner aggregates; worker callbacks do not use
shared scope collectors. Reduced terrain remains an explicitly serial fallback.

`PROFILE_MODE=interactive PROFILE_SAVE=/path/to/save.game PROFILE_SECONDS=10
PROFILE_FPS=60` runs the production threaded session with a fixed display cadence.
It reports drawing plus presentation p95/p99, missed render deadlines and undrawn
opportunities (pacing or unavailable scenes),
displayed-scene age and the last observed simulation deadline lateness at each
display. The latter distribution is weighted by displayed frames; the
`simulation.deadline_lateness` scope records actual tick observations. Scene age
includes drawing/presentation time and uses the captured tick completion time.
`PROFILE_PAN=1` adds camera motion. Only actual draws admitted by the production
frame pacer enter the distributions. Production performance collection is mandatory
in all modes; the retired disable variable has no effect. Renderer-only timings exclude simulation;
interactive results include its contention and must be reported separately.

For renderer-only composition diagnostics, `PROFILE_MODE=map PROFILE_WHOLE_MAP=1`
shows the captured terrain irrespective of discovery; it does not modify the saved
world. `PROFILE_PAUSED=1` freezes view animation in renderer-only workloads,
separating warm static caches from animated-page rebuilding. Verify counters show
admitted jobs before interpreting parallel timings.
The software recorder serves backing-scale, fractional-transform and offscreen
backend paths. Ordinary native-size immediate surface drawing retains its existing
arithmetic and runs serially; `PROFILE_FRACTION=1` exercises the transform path.

Keep warm caches, cold first frames, animated terrain, camera/zoom motion, large
colonies and translucent overlays separate. CPU fallback and device-composed
terrain also need separate results. Verify exact capture pixels and unchanged
simulation checksums before discussing latency. Do not infer smoother gameplay
from composition throughput alone, or establish thresholds from fast builds.
The default remains immediate serial drawing. Enabling batching by default or
adding an automatic selection policy requires representative hardware results.

Executor graphics timings are optional. Set `PROFILE_COMPUTE_TELEMETRY=waits`
for owner barrier measurements or `trace` for worker queue/execution measurements.
The default is off; `graphics_executor` fields report `na` when uncollected and
include `mode` and `worker_available`. Compare instrumented and ordinary rendering
separately. Other renderer performance scopes remain controlled as before.
