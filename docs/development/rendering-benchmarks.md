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
include estimated resident terrain texture bytes within the cache accounting.
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
discovery. Desktop legacy OpenGL draws every view, HD artwork included, with
the direct pass described below. On the other backends, HD artwork, editor views,
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
warmed. Compare complete builds from both revisions; the diagnostic immediate
path is not an untouched-master baseline.


See [skin rendering](skin-rendering.md) for production sprite export and skin preview diagnostics.

## Renderer memory

`tools/rendering/memory_scenario.py` runs the standard memory scenario and reports
peak memory by owner. It loads a mid-game save of a 256×256 Honeycomb Isles map
in `torus-render-benchmark`, draws the flat game view on legacy OpenGL with HD
artwork, and pans while cycling the zoom between the minimum and four times
that. Build the perf tier first; `--prepare` generates the save (map seed 7,
four AIs, tick 30000) under `artifacts/render-memory/`.

```sh
python3 tools/dev_build.py perf=1 glob2 torus-render-benchmark
python3 tools/rendering/memory_scenario.py --prepare --label master
python3 tools/rendering/memory_scenario.py --label change --compare master
```

The table it prints goes in the pull request. Peak process resident memory and,
on Linux, the GPU driver's per-process memory (`drm-memory-*` in
`/proc/<pid>/fdinfo`) come from sampling the process; an integrated GPU's driver
memory is system RAM. The remaining rows are the renderer's own tracked bytes,
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
| `gpu_frame_cache_bytes`, `gpu_skin_atlas_bytes`, `gpu_view_tile_bytes` | Render targets no surface owns |
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
