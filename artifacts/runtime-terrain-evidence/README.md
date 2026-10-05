# Runtime terrain implementation: review evidence

[Download the complete evidence archive](evidence.tar.gz) · [Archive SHA-256](archive.sha256) · [Desktop palette](screenshots/terrain-palette-desktop.png) · [Phone palette](screenshots/terrain-palette-phone.png)

This is development evidence, not product documentation. The feature PR remains **draft**: the implementation and native correctness checks are complete, but the requested performance/platform qualification is not complete.

- Tested feature revision: `ad8406ed6` (full SHA in `payload/environment.json`).
- Compared base: `dbc6253427edc1ca71227ec91a8cd9ab66b5d326`.
- Latest master was fetched and overlapping ecology, terrain-gradient and audio changes were integrated before final validation. A later unrelated music-buffer tuning commit (`2a775e0aa`) appeared in the shared remote-tracking ref after validation; neither measured binary includes it.
- Linux x86-64, AMD Threadripper 2950X, GCC 15, SCons release build (`-O3`), the same SDL3 prefix and build options for both native binaries. Standalone kernels use `-O3 -DNDEBUG`; scalar/NEON oracle tests use `-O2 -DNDEBUG`. See `environment.json`, binary hashes and baseline source verification (2,059 native source/build/test files, zero mismatches).

## Results and limits

- Final native engine run: **669 passed, 0 failed, 101 skipped**. The complete test-case inventory and skip reasons are in JUnit and the runner log.
- Native headless unit run: **816 passed**, 17 display cases skipped.
- Focused display run: **10 passed**: terrain software/OpenGL rendering, desktop/phone custom-terrain editor workflows and map previews. Four PNG captures are included. These are exercised logical desktop/phone layouts, not physical phone-device tests.
- Scalar and emulated ARM64/NEON registry/kernel oracle runs: **6 cases, 218 assertions passed** each. This verifies the NEON implementation under QEMU; it does **not** establish whole-engine ARM64 determinism.
- Format-135 local replay: 3,000 ticks checked against its stored per-tick checksums. Custom network verifier replay: 702 ticks checked using network heavy checksums. Both pass on the final feature revision. The full native suite includes the regenerated golden match, malformed map/registry inputs, embedded custom LAN transfer, online-style shared-map validation and match verification, old saves, pending-gradient save/load continuation and isolation between registries.
- Full Windows, native ARM64 and browser/WASM engine replay/checksum comparison is **unavailable and outstanding**. Their runtime/toolchain/dependency environments were not available for this task.
- No hosted CI qualification has been requested. Local evidence is not a hosted checkpoint.

## Performance

Randomized paired release runs use one warm-up round followed by **10 measured repetitions**. Kernels use three warm-up calls and **15 randomized measured rounds**, calibrated to roughly 100 ms per case. CPU and wall time are recorded separately. Full-match and kernel processes are pinned to CPU 29; render processes use ordinary scheduling. No benchmark ran concurrently with this task's builds or correctness suites. Other host workloads were active: the quiet-machine requirement is **not met**, so these measurements are exploratory and the requested performance acceptance remains **inconclusive**. Confidence intervals below are bootstrap intervals over paired percentage differences, not proof against shared-host bias.

The full-match workload is FourSquares1, seed 2026, four Warrush AIs, one compute thread, synchronous gradients and a 1,000-tick timing warm-up. Every run completed at **17,090 ticks**, with identical team outcomes and repeatable per-case checksums. Custom-definition digests intentionally make checksums differ between registries. Only one complete match workload is represented; additional representative workloads remain outstanding.

| Comparison | Mean paired CPU change | Bootstrap 95% interval |
|---|---:|---:|
| Feature built-in map vs base built-in map | -1.28% | -3.41% to +0.71% |
| 7-definition reserialized map vs feature original map | +0.64% | -1.83% to +2.99% |
| 259 vs 7 definitions | -0.35% | -1.83% to +1.10% |
| 1,024 vs 7 definitions | -0.78% | -2.87% to +1.24% |
| 16,384 vs 7 definitions | -1.07% | -3.06% to +0.90% |

Equivalent custom gradient kernels at 256² and 512² use **12.9–14.9% less CPU time** than the static prepared kernel in these runs. Built-in registry dispatch differs by +0.22% and -0.32% respectively. Both used and unused definition cases are included, with full gradient-array equality verified before timing. The genuinely distinct-cost, 16,384-definition, 256-bucket stress field takes a median **6.224 ms** at 256²; this is an absolute stress result, not an equivalent-workload comparison. The raw CSV includes CPU/wall times and checksums.

Rendering measures 120 software frames after 20 warm-up frames at a fixed initialized save, 1280×800, zoom 1, presentation disabled. The observed SDL driver was Wayland (inherited environment), despite invocation through `xvfb-run`. Built-in feature vs base mean paired CPU change was +1.71% (interval -2.08% to +5.55%). Relative to the 7-definition map, 259/1,024 types measured +0.79%/+2.32%, and 16,384 types measured **+24.76%** (interval +21.50% to +27.84%). This maximum-size rendering cost remains a known qualification concern. Custom full-tile presentation is intentional and need not produce the same pixels as legacy corners. GPU and moving-camera performance are not qualified.

| Registry size | Full-match peak RSS, median | Setup CPU, median | Render CPU/frame, median | Render peak RSS, median |
|---|---:|---:|---:|---:|
| 7 | 47.2 MiB | 0.137 s | 1.833 ms | 286.2 MiB |
| 259 | 47.2 MiB | 0.152 s | 1.842 ms | 286.8 MiB |
| 1,024 | 48.1 MiB | 0.205 s | 1.864 ms | 299.8 MiB |
| 16,384 | 300.4 MiB | 1.468 s | 2.261 ms | 556.2 MiB |

Peak RSS includes JSON import/load and allocator high-water usage, not just retained registry storage. The simulation caches a two-byte property-profile index per cell to keep equivalent definitions in a compact working set. No claim is made that the maximum registry has negligible loading, memory or rendering cost.

## Reproduction

Unpack `evidence.tar.gz`. Its `payload/` contains original logs, JUnit, raw benchmark outputs, command JSON files, scripts, maps, saves, match records, replay checksum sidecars and screenshots. `manifest.sha256` covers every payload file. Paths in original commands describe the tested host; adjust the checkout/dependency prefix when reproducing.

Build the feature at the tested revision and a separate archive/checkout of the pinned base with identical dependencies:

```sh
export GLOB2_SDL3_PREFIX=/path/to/the/same/SDL3/prefix
scons -j10 release=1 server=0 tests build/linux/client/release/src/glob2 software-render-benchmark
python3 test/run_tests.py --binary engine --no-display --junit artifacts/qualified-tests.xml -j4
python3 test/run_tests.py --binary unit --no-display --junit artifacts/qualified-units.xml -j4
python3 test/run_tests.py --binary engine --filter 'TerrainPresentation/*' --filter 'EditorActionCoverage/custom terrain*' --filter 'MapPreview/*' --junit artifacts/qualified-display.xml -j2
```

Registry/kernel standalone reproducer (place the supplied `registry-main.cpp` under `artifacts/terrain-runtime-implementation/`):

```sh
g++ -std=c++20 -O2 -DNDEBUG -DGLOB2_GRADIENT_SCALAR -Isrc -Isrc/map -Itest/support -Ithird_party/nlohmann-json/include artifacts/terrain-runtime-implementation/registry-main.cpp src/map/TerrainRegistryTest.cpp src/map/TerrainRegistry.cpp src/online/Sha256.cpp -o artifacts/registry-scalar
artifacts/registry-scalar
aarch64-linux-gnu-g++ -std=c++20 -O2 -DNDEBUG -Isrc -Isrc/map -Itest/support -Ithird_party/nlohmann-json/include artifacts/terrain-runtime-implementation/registry-main.cpp src/map/TerrainRegistryTest.cpp src/map/TerrainRegistry.cpp src/online/Sha256.cpp -o artifacts/registry-arm64
qemu-aarch64 -L /usr/aarch64-linux-gnu artifacts/registry-arm64
```

Replay commands (the supplied files are authoritative; author JSON is not needed):

```sh
GLOB2_REFERENCE_REPLAY=/absolute/path/payload/reference-save-run/game.replay python3 test/run_tests.py --binary engine --tag benchmark --filter 'EngineSession/external replay*'
GLOB2_REFERENCE_HEAVY=1 GLOB2_REFERENCE_REPLAY=/absolute/path/payload/test-artifacts/TurnEngineHarness/embedded_terrain_definitions_travel_with_shared_matches_and_verifier_replays_network-sim_artifacts/verify/match.replay python3 test/run_tests.py --binary engine --tag benchmark --filter 'EngineSession/external replay*'
```

For benchmarks, copy the supplied scripts and `kernel-bench.cpp` to `artifacts/terrain-runtime-implementation/`, copy `payload/test-artifacts/` contents to `artifacts/tests/`, and copy `reference-game-four/` and `render-fixtures/` into that benchmark directory. Build the pinned baseline at `artifacts/terrain-runtime-implementation/baseline/` using the same command/dependencies. Use a fresh output directory: scripts intentionally refuse to overwrite existing full-match result files. CPU 29 must be replaced consistently if unavailable. Each full-match run also includes its exact command JSON.

```sh
g++ -std=c++20 -O3 -DNDEBUG -Isrc -Isrc/map -Ithird_party/nlohmann-json/include artifacts/terrain-runtime-implementation/kernel-bench.cpp src/map/TerrainRegistry.cpp src/online/Sha256.cpp -o artifacts/terrain-runtime-implementation/kernel-bench
python3 artifacts/terrain-runtime-implementation/full-match-bench.py
taskset -c 29 artifacts/terrain-runtime-implementation/kernel-bench 47 > artifacts/terrain-runtime-implementation/kernel-final.log
xvfb-run -a python3 artifacts/terrain-runtime-implementation/render-bench.py
python3 artifacts/terrain-runtime-implementation/analyze.py
```

The equivalent map fixtures can also be regenerated with `python3 test/run_tests.py --binary engine --tag benchmark --filter 'TerrainRuntime/write equivalent custom maps*'`. The scripts preserve the shuffle seeds and warm-up policy. Full-match wall time includes the initial 1,000 ticks; benchmark CPU time excludes them. Rendering wall elapsed includes loading; renderer logs separately record frame wall timings. Compare like metrics rather than subtracting CPU and wall values from different timing windows.
