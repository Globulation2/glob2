# AI Telemetry verification evidence

Final verified source: c3cd5ba3f3688bd293edad43718c4ec5955cbb31, integrated with base 0ac3a79ad68bb8ace58e6b926301c8ca6e4d344a. The patch-only paired comparison uses a4fad39b4e2a2ee1daf882008654eeed4de4a22c versus stock production 79229b3101c6bd6e0c83abb5ed55579834595785. Master font-shaping/build fixes were merged after that comparison; final validation and timings were refreshed separately.

macOS arm64, Apple clang 21, release O3 with debug information, SDL3 3.4.16 / SDL3_ttf 3.2.2 / SDL3_image 3.4.6 / SDL3_net 3.2.0. Same saved seed-23 match, authored desktop 1280x800 and phone 390x760 (HiDPI drawable 2x), native software renderer (screenFlags=0, GLOB2_RENDERER unset), same dependencies and build flags.

The benchmark executes the production Game, GameGUI and telemetry dialog through the engine harness. The stress fixture replaces only the copied, access-filtered published Scene values with 4,228 rows. Eight samples per phase: the nearest-rank p95 is the largest sample. Opening includes attach/update/draw; refresh includes update/draw; scrolling includes scroll/update/draw; searching includes input/update/draw. Simulation advances 32 ticks between refreshes outside UI timing. These are UI action timings, not FPS or a concurrent simulation throughput measurement.

## Observed p95 milliseconds

Eight samples per phase; busy machine, not reference-machine acceptance.

| Layout | Fields | Phase | Stock baseline | Patch only | Final integrated |
|---|---|---|---:|---:|---:|
| Desktop | Full Maxima | opening | 888.276 | 14.429 | 10.032 |
| Desktop | Full Maxima | refresh | 808.716 | 11.845 | 8.431 |
| Desktop | Full Maxima | scrolling | 815.152 | 10.725 | 7.630 |
| Desktop | Full Maxima | searching | 48.205 | 13.277 | 9.581 |
| Desktop | 4,228 stress | opening | 2311.746 | 11.790 | 8.445 |
| Desktop | 4,228 stress | refresh | 2297.333 | 11.451 | 8.506 |
| Desktop | 4,228 stress | scrolling | 2437.116 | 10.431 | 7.576 |
| Desktop | 4,228 stress | searching | 14.112 | 10.939 | 8.006 |
| Phone | Full Maxima | opening | 690.571 | 9.091 | 6.366 |
| Phone | Full Maxima | refresh | 747.900 | 6.571 | 4.614 |
| Phone | Full Maxima | scrolling | 745.814 | 5.824 | 4.333 |
| Phone | Full Maxima | searching | 44.257 | 9.065 | 6.537 |
| Phone | 4,228 stress | opening | 1807.281 | 6.551 | 4.732 |
| Phone | 4,228 stress | refresh | 1754.109 | 6.482 | 4.694 |
| Phone | 4,228 stress | scrolling | 1938.791 | 5.191 | 3.797 |
| Phone | 4,228 stress | searching | 8.429 | 5.921 | 4.134 |

All 1,024 per-tick checksums matched across the baseline, patch-only and final integrated runs. Dialog actions also assert unchanged checksums within each run. Each scenario advances 256 simulation ticks. No simulation source, collection, save format, replay/network contract or simulation revision changed.

## Coverage and limits

The new focused font/telemetry regressions passed. The final integrated revision passed all 685 quick headless unit cases; 13 display/slow cases were skipped by that command. The final renderer/touch/font/telemetry/presentation sweep passed 40 cases and failed only two environmental window-height cases, recorded in integration-engine-run.log. Requested 1024/1080 px tall windows were clamped by macOS to 811 px. These failures also reproduce on stock. The earlier patch-only sweep passed 32 cases with four failures, recorded in renderers-run.log. JavaScript telemetry scene permissions/replay roundtrip and actual telemetry dialog rendering both passed. All four broader failures reproduced against stock production: the same landscape/use safe-area failures at 150% text and macOS window-height clamping (810/811 px instead of 1024/1080). See baseline-layout-run.log and layout-failure-comparison.txt. After integrating master, both landscape failures pass. The game and native test binaries built successfully. The 16 CI policy tests passed.

Cross-platform metric parity, Linux/Windows/Android/browser renderer coverage and a physical phone were not available. Phone evidence uses desktop emulation. Competing builds, game/gallery processes and system services were active; see baseline-machine-load.txt. The fixed observations are below 16 ms refresh and 100 ms opening, but a quiet reference-machine benchmark and maintainer playtest remain pending. The PR stays draft.

## Reproduction

Build with `CCACHE=1 GLOB2_SDL3_PREFIX=<SDL SDK prefix> scons -j4 release=1 server=0 tests`, then build the game with the same options and no target. Run the commands listed in commands.txt. `benchmark-input.game` is the exact input save. `benchmark-harness.cpp` is the committed engine test source. `baseline-production-reversal.patch` reverts only the four production files to stock; `baseline-production.diff` is empty, proving source equivalence to the baseline in src/ and libgag/. Keep the current test registry/harness for both benchmarks, and build engine-tests for baseline (the metrics regression needs the new font internals). Perform that baseline reconstruction at a4fad39b, then restore the PR sources and rebuild tests afterward. For the final merged revision, use c3cd5ba3 and the final integrated commands.

Build logs are compressed. JUnit reports, timing output, checksum trace, save hash and desktop/phone PNGs are retained alongside this report. Full local sweep screenshots remain in ignored artifacts and are not all uploaded.
