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
build/darwin/client/release/test/torus-render-benchmark --renderer gpu --no-fullscreen --mute --window-size 1280x800
```

Use the current host's release directory on Linux/Windows. `GLOB2_BENCH_MODE`
selects `2D no clouds` or `2D clouds`; otherwise both run. Run
each mode in a separate fresh process with identical warmup and camera settings
when comparing cloud cost: running both shares terrain and device caches, so the
second mode benefits from the first mode's work. Alternate process order across
repeated pairs to limit drift from temperature and background activity. Set
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
include p99 and maximum latency as well as median and p95. Per-frame records
include process CPU time captured before diagnostic output on platforms where
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

Timings include GPU completion (`glFinish`) and exclude frame presentation, AI,
input, scene extraction and simulation work. Flat passes retain a prepared scene;
AI comparisons refresh it before each timed pair. They are renderer measurements,
not whole-game FPS.
POSIX builds also report process CPU time separately from elapsed time.
Scope timings separately report CPU submission and overlap; do not sum inclusive
scopes. The fixture is native OpenGL only; mobile uses the SDL portable renderer,
so desktop results do not qualify Android/iOS hardware performance.

For comparisons with another revision, set `GLOB2_BENCH_PAUSE_PRESENTATION=1`
to freeze terrain animation and `GLOB2_BENCH_WARMUP_FRAMES` to the same number of
frames on both executables. Record cold-frame samples as well as steady-state
medians, and confirm `STEADY_CACHE pending=0` before describing results as fully
warmed. Compare complete builds from both revisions; the diagnostic immediate
path is not an untouched-master baseline.


See [skin rendering](skin-rendering.md) for production sprite export and skin preview diagnostics.

## CPU presentation batches

Build `software-render-benchmark` and `torus-render-benchmark` with `release=1`.
Use the same retained save, seed, animation, viewport, warmup and compute budget
for `GLOB2_RENDER_EXECUTION=auto`, `serial` and `parallel` in separate processes.
Both standalone tools apply the launch `--compute-threads auto|N` budget to their
map executor; keep that budget fixed between modes.

For paired saved-game software measurements, supply a caller-owned CPU mask and
an owner-inclusive compute participant count within that mask:

```sh
python3 tools/software_render_benchmark.py \
  --baseline /absolute/path/to/baseline/software-render-benchmark \
  --candidate /absolute/path/to/candidate/software-render-benchmark \
  --save /absolute/path/to/checkpoint.game.gz \
  --cpu-ids "$BENCH_CPU_IDS" --compute-participants "$BENCH_PARTICIPANTS" \
  --execution-profile fast-simulation --output artifacts/renderer-pair
```

Choose `BENCH_CPU_IDS` from the allocation assigned to this experiment; the count
includes the owner and must not exceed its mask width. The launcher defaults to
`fast-simulation` intent, pins both children to the same mask, and checks the
reported resolved plan and actual/frozen participant counts. The named intent
does not establish calibrated tuning or an optimal worker count. Optional
`--memory-planning-budget-bytes` records a soft planning budget, not an RSS cap.

For a pre-resource-profile CLI2 baseline harness, add
`--baseline-execution-compatibility legacy_argv`. It omits the newer profile and
memory flags but still passes the same existing `--compute-threads` count when a
mask is supplied. A reported actual count must match; missing legacy plan/count
markers remain explicitly unverified. Use `--execution-compatibility legacy_argv`
only when both harnesses require that compatibility. Non-CLI2 harnesses are not
supported by this compatibility mode; unsupported arguments fail rather than
triggering silent fallback. Existing frozen benchmark protocols remain unchanged.

`auto` measures the conservative immediate path; `serial` measures packed CPU
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
frame pacer enter the distributions. Interactive observations require telemetry;
`GLOB2_PERF_DISABLE` is rejected for this mode. Renderer-only timings exclude simulation;
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
The automatic default remains serial until representative hardware results
justify enabling the bounded paths.
