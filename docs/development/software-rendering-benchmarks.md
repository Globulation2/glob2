# Software renderer benchmarks

Measure the production software backend with matching release binaries and retained scenes. [Backend architecture](../architecture/software-rendering.md) explains rasterization and caches.

```sh
scons release=1 server=0 opengl=0 software-render-benchmark
PROFILE_SAVE=artifacts/software-renderer/initial.game.gz PROFILE_ZOOM=0.5 \
  PROFILE_FRAMES=240 PROFILE_WARMUP=30 PROFILE_NO_PRESENT=1 PROFILE_CPU_SCOPES=1 \
  GLOB2_USER_DATA_DIR=artifacts/software-renderer/profile \
  build/darwin/client/release/test/SoftwareRenderBenchmark --renderer software --window-size 1280x800 --mute --no-fullscreen
```

Use the appropriate `linux`/`mingw` build directory or an explicit `--build=DIR`.
Resolution is the existing `--window-size WxH` argument, measured in framebuffer pixels.
The benchmark creates its SDL3 window without high-density backing pixels by default,
so the workload does not change with monitor density. `PROFILE_NATIVE_DISPLAY=1` retains native Retina/HiDPI presentation. `PROFILE_OFFSET_X/Y` add logical-pixel camera
offsets; `PROFILE_FRACTION=1` adds a half-pixel horizontal offset. `PROFILE_VISIBLE=1`
shows the window; omit `PROFILE_NO_PRESENT` to include presentation. `PROFILE_CAPTURE`
names an output BMP. `PROFILE_TERRAIN_CACHE=0` isolates primitive performance without
adding a user graphics setting. `PROFILE_SELECT=building|flag|unit` selects the local
team's first such entity, so frames include its selection panel and map markers;
`PROFILE_TOOL=<building type>` (for example `inn`) activates the building tool with the
cursor over the middle of the map view, so frames include the placement preview. Use them
with `PROFILE_MODE=gui` for Scene parity captures against another revision. The harness reports population, wall-time mean/median/p95,
process CPU time, complete draw-plus-presentation frame distributions, optional thread
CPU stage costs, backend operation counts, cache memory
and cache hit/rebuild counts. It also checks that drawing preserves the simulation checksum.
Run captured fixtures from early, mid and late games; keep generated saves and profiles
under ignored `artifacts/`. To advance a saved initial game into population fixtures,
use the existing structured runner with its saved seed and orders, for example:

```sh
GLOB2_USER_DATA_DIR=artifacts/software-renderer/fixture-profile \
  build/darwin/client/release/src/glob2 game run \
  --load-game "$PWD/artifacts/software-renderer/initial.game.gz" --ticks 12000 \
  --save every:6000 --save final --telemetry checksums \
  --output-dir "$PWD/artifacts/software-renderer/populated"
```

Keep the initial save, generated checkpoints and runner metadata together. Fixture
population matters more than the tick label; a late game can have fewer surviving units.

For paired measurements, preserve a baseline benchmark executable before rebuilding and
run at least twelve alternating pairs on the same fixtures, resolution and hardware:

```sh
python3 tools/software_render_benchmark.py \
  --baseline artifacts/software-renderer/baseline/SoftwareRenderBenchmark \
  --candidate build/darwin/client/release/test/SoftwareRenderBenchmark \
  --save artifacts/software-renderer/initial.game.gz --save artifacts/software-renderer/mid.game.gz \
  --save artifacts/software-renderer/late.game.gz --repeat 12 \
  --output artifacts/software-renderer/comparison
```

The runner records raw logs/captures, exact commands and CPU distributions for native,
half, double and fractional-offset scenarios. `--no-terrain-cache` now streams
composed pages without retaining them between frames; it measures repeated
composition and upload, not the old sprite-only terrain primitives. Compare the
same binary with `--baseline-no-terrain-cache` to isolate retained-page caching.
Use `--present --visible --scenario native --baseline-preserve-frame` with the same
binary to measure the retention-copy savings. `PROFILE_PRESERVE_FRAME=1` begins each
benchmark frame in preserve-content mode before the full redraw. Keep other heavy
work off the measurement machine. On macOS, `sample PID SECONDS -file artifacts/profile.txt`
can identify CPU stacks; Linux `perf` and Windows profiling tools can sample the same
opt-in executable. Timing thresholds are review criteria, not CI assertions. Run
`SoftwareRenderer`, `PortableRenderer`, `WindowResize`, `MapRenderResize` and
`HighResolutionIntegration` suites on supported SDL/platform builds, retain before/after
captures, and report unavailable platform and maintainer-playtesting coverage explicitly.

For CPU batch optimization, compare the same execution mode before and after the
change, then compare the candidate's `immediate`, `serial` and `parallel` modes.
`raster_cpu` distinguishes painted tiles/references from dispatched jobs and reports
copied source bytes, source revision reuses and current retained source bytes. These
counters establish whether less preparation and dispatch work occurred; frame time
and process CPU establish whether that work reduction helps the complete frame.
Use ordinary wall-clock scopes for parallel comparisons: `PROFILE_CPU_SCOPES=1`
measures only the calling thread and cannot attribute worker CPU time. Run separate
`PROFILE_COMPUTE_TELEMETRY=trace` diagnostics to examine executor activity and owner
waits; keep telemetry configuration identical within each comparison.
Set `PROFILE_COMPUTE_TRACE=artifacts/renderer/compute.json` with trace telemetry
to retain per-callback timestamps after the final drawing segment has joined.
Compare callback duration tails separately from whole-frame timing; fewer jobs
alone mechanically reduce summed queue time. Interactive benchmarks use their
external FPS schedule as the sole pacer and retain the ordinary render admission
boundary, so two independent pacers cannot skip measurement opportunities.

## Camera and profiler boundaries

The standalone harness extracts the initial scene before establishing camera bounds.
`PROFILE_ZOOM` must match the effective zoom; an out-of-range request fails rather
than silently measuring another workload. GUI mode starts at the local team's initial
viewport; `PROFILE_MODE=map` starts at map origin zero. Keep that distinction identical
between revisions. Earlier harnesses could clamp a requested half-scale GUI view to
1.0 before scene extraction; their scenario labels do not establish the actual zoom.
Always retain the `CAMERA` and `READY` lines with comparison evidence.
Fixed-state GUI runs seed the simulation-rate meter at zero before drawing, keeping
its label at `0.0`; otherwise elapsed wall time can change it from `-` during a run.
The live simulation probe retains the production meter.

On POSIX builds, `PROFILE_BOUNDARY_STOP=1` prints and flushes
`PROFILE_BOUNDARY phase=begin` after warmup, then stops the benchmark with `SIGSTOP`.
An external profiler may attach all existing threads, enable its counters, and resume
that PID with `SIGCONT`. At `phase=end`, drawing is fenced and the process stops again;
disable counters before resuming it for reporting and shutdown. The benchmark links
no profiler library. Use native perf instruction/branch counters for CPU work and
instruction samples for attribution; require all requested counters to be counted
without multiplexing. Record user and kernel counts separately. These boundaries
apply to fixed-scene mode, not the interactive simulation probe.

The harness reports `timing_valid=false` for its stop boundaries, known perf/Callgrind
boundary flags, and preloaded interposition libraries (`LD_PRELOAD`, `LD_AUDIT` or
`DYLD_INSERT_LIBRARIES`). The timing runner rejects inherited interposition and
`GLOB2_PROFILE_*` environments; it clears inherited `PROFILE_*` options before applying
its explicit workload configuration. Never use their elapsed time,
FPS, CPU distributions, or frame percentiles as latency evidence. Callgrind serializes
client threads: its instruction and call-graph evidence cannot establish native
parallel speedup. Cache simulation is a model, not measured memory bandwidth.
Run timings separately with ordinary scopes and no profiler stops or CPU-stage scopes.

## Isolated full-pipeline comparisons

`test/run_software_render_comparison.py` collects twelve alternating comparison groups
for warm static maps, animated maps, actual half-scale GUI, native GUI, streamed terrain,
and live simulation. Each group runs both baseline modes and the candidate's immediate,
serial and parallel modes. It retains logs, initial fixed-scene captures, binary/fixture
hashes, commands and environments. It requires identical reported camera, framebuffer,
map dimensions, population and completed frame counts across fixed-work variants,
checks their simulation checksum, and requires exact capture hashes across candidate
execution modes. Baseline and candidate must both include the corrected
camera setup; do not compare a mislabeled legacy half-scale workload against a corrected one.
The interactive case requires displayed frames and positive simulation tick progress.
Its manifest records live progress and latency observations separately from fixed-work
comparisons. It exercises admission and live simulation rather than the frozen-scene
checksum probe; analyze its displayed-scene age and deadline behavior separately.

```sh
python3 test/run_software_render_comparison.py \
  --baseline artifacts/renderer/baseline/SoftwareRenderBenchmark \
  --candidate build/linux/client/release/retain-symbols/test/SoftwareRenderBenchmark \
  --save artifacts/renderer/fixture.game --assets artifacts/renderer/runtime-assets \
  --output artifacts/renderer/comparison
```

The runner invokes the audited `run_with_benchmark_cpuset.py` wrapper. Its current
host configuration reserves CPUs 0–11 including SMT siblings, places the runner and
benchmark on CPUs 0,2,4,6,8,10, and leaves CPUs 12–15 for ordinary work on the 16-thread
measurement host. The wrapper validates topology and complementary CPU ownership,
records an audit, and restores its temporary partition. It fails on incompatible
hosts rather than guessing CPU topology. Use the wrapper's documented interface
for other hosts. Isolation requires authorization to create a temporary cgroup;
it does not justify changing global CPU settings. Retain the audit with results.

Compare independent paired runs and report confidence intervals. Isolation reduces
scheduler competition but does not eliminate thermal, frequency or shared-memory
contention; record those conditions and treat inconclusive timing as inconclusive.
Render-only instruction counts establish work and coverage, while complete frame
measurements and live simulation establish whether distributing it is beneficial.
