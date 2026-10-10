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
process CPU time, optional thread CPU stage costs, backend operation counts, cache memory
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
run at least seven alternating pairs on the same fixtures, resolution and hardware:

```sh
python3 tools/software_render_benchmark.py \
  --baseline artifacts/software-renderer/baseline/SoftwareRenderBenchmark \
  --candidate build/darwin/client/release/test/SoftwareRenderBenchmark \
  --save artifacts/software-renderer/initial.game.gz --save artifacts/software-renderer/mid.game.gz \
  --save artifacts/software-renderer/late.game.gz --repeat 7 \
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
