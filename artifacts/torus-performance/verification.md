# Torus rendering regression verification

Source: working tree changes based on 012d57694f790788f3fe3c5e2a08196d236b949a. Fetched master before final validation: 4edaed552c3574914197d4978fbff4b81bd1eedb; newer changes do not edit the affected rendering/libgag/build sources. No merge or rebase performed.

Environment: Linux x86_64, Ubuntu GCC 15.2.0, release (-O3), native OpenGL/X11, NVIDIA RTX 2070 SUPER, driver 580.178.04. SDL headers from the existing development prefix; Linux SDL libraries from /tmp/glob2-terrain-sdl-patched/prefix. No new runtime dependencies.

Fixture: Lava Shield revision 5, 256x256, four teams, seed 1; copied from the existing local Lava Shield evidence to profile/maps/lava-performance.map.gz. Input and executable hashes: final-inputs.sha256; original executable hash: before-executable.sha256. Requested window 1280x800; benchmark map viewport 1120x743, zoom .25. Rendering only; no simulation stepping.

## Measurements

| Run | Frames / warmup | Median ms | p95 ms | Maximum ms |
| --- | --- | --- | --- | --- |
| Original, no clouds | 12 / 2 | 1490.280 | 1603.671 | 1603.671 |
| Original, clouds | 12 / 2 | 1485.602 | 1583.984 | 1583.984 |
| Patched, clouds | 12 / 2 | 26.124 | 119.720 | 119.720 |
| Patched, no clouds, longer warmup | 64 / 96 | 13.148 | 113.428 | 190.114 |

The paired clouds measurement is about 57 times faster at the median. The long run still has animation-phase spikes, so smooth 120 FPS has **not** been established. Separate warmup/frame counts are intentional and shown above; do not treat the longer run as an exact paired speedup measurement.

Original frames rebuilt roughly 180 detailed pages and recomposed overview contours repeatedly. Patched unchanged frames retain their pages; overview composition remains fixed at 65536 cells after warmup. Original capture: 8192x8192, 25 tiles, 32 pixels/cell, 289816576 allocated atlas bytes. Patched: 4096x4096, one tile, 16 pixels/cell, 68161536 atlas bytes. This trades fine texture detail under magnification for a resident whole-map working set. Contour/filter reference checks remain exact. Patched benchmark simulation checksum remains 764d540f throughout.

Before/after paired screenshots: before-clouds.png and final-clouds.png. Inspected both; layout, wrapped shores and blend are preserved. Fine texture grain is softer at the reduced density. The benchmark includes synchronous GL completion; these timings are not a claim about interactive game FPS.

## Commands

Build:

```sh
CCACHE=1 GLOB2_SDL3_PREFIX=/home/bradley/.local/share/glob2/development/dependencies/Linux-x86_64-0db2318ca135e0c408e4c9d3 GLOB2_RECORDING_PREFIX="$PWD/build/linux/client/release/recording/prefix" scons release=1 torus-render-benchmark engine-tests unit-tests build/linux/client/release/src/glob2 -j10 'LINKFLAGS=-L/tmp/glob2-terrain-sdl-patched/prefix/lib -L/usr/lib/x86_64-linux-gnu -Wl,-rpath=/tmp/glob2-terrain-sdl-patched/prefix/lib'
```

Paired benchmark (replace executable with artifacts/torus-performance/before-benchmark for original):

```sh
SDL_VIDEODRIVER=x11 GLOB2_USER_DATA_DIR="$PWD/artifacts/torus-performance/profile" GLOB2_BENCH_MAP=maps/lava-performance.map GLOB2_BENCH_ZOOM=.25 GLOB2_BENCH_MODE='Torus clouds' GLOB2_BENCH_FRAMES=12 GLOB2_BENCH_WARMUP_FRAMES=2 GLOB2_BENCH_FRAME_TIMES=1 GLOB2_BENCH_CAPTURE="$PWD/artifacts/torus-performance/final-clouds.ppm" build/linux/client/release/test/torus-render-benchmark -g -F -m -s 1280x800
```

Longer run: mode 'Torus no clouds', frames64, warmup96, same other inputs. Logs: before.log, before-clouds.log, final-clouds.log, final-long.log.

```sh
python3 test/run_tests.py --binary engine --filter 'TerrainPresentation/*' --filter 'TerrainMaterials/*' --filter 'TorusRender/*' -j2 --display-jobs 1 --junit artifacts/torus-performance/final-render-tests.xml --artifacts artifacts/torus-performance/final-test-artifacts
python3 test/run_tests.py --binary unit --filter 'ImageAssets/*' --filter 'AssetLoader/*' -j2 --display-jobs 1 --junit artifacts/torus-performance/final-asset-tests.xml
python3 test/run_tests.py --binary unit --filter '*batch*' --filter 'OpaqueRectangleBatch/*' -j2 --display-jobs 1 --junit artifacts/torus-performance/final-batch-tests.xml
```

JUnit and corresponding .log files record final results. Coverage focuses on terrain pixels, native/portable GPU and software rendering, wraps, zoom/HD, cache lifetime, mip generation, sprite uploads and batch lifetime. No simulation algorithms or save formats changed; simulation version unchanged. Cross-platform builds, browser/mobile execution and hosted CI were not run. No claim of cross-platform performance equivalence.

Final results: **91 test cases passed, zero failures** (56 rendering/terrain, 17 assets, 18 batch/related cases). Release game executable and both test binaries built successfully. `git diff --check` passed.

Tested source commit: `13df1e26dbd06d1dd09d61e61d58e11cf80224ab`. Commit packages the exact source tree used for the final successful tests and benchmarks; no source edits after verification.
