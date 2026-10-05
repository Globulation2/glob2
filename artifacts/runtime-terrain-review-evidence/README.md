# Runtime terrain review and cleanup evidence

Two independent sub-agents reviewed simulation/persistence and presentation/editor architecture, reported concrete findings, and reviewed the integrated fixes again. See the bundled `feedback.md` for findings, dispositions and test coverage. No additional blocking correctness issue remained after the second review.

[Download evidence](evidence.tar.gz) · [Archive SHA-256](archive.sha256) · [Desktop palette](screenshots/terrain-palette-desktop.png) · [Phone palette](screenshots/terrain-palette-phone.png)

This evidence refreshes PR #799 after the review. It does not declare the original cross-platform/performance qualification complete.

## Revisions and environment

- Full cleanup revision: `ba739d5d7`.
- Final tested/pushed revision: `7afc0e1c2` (full SHA in `environment.json`).
- Integrated the independently merged scene-header boundary fix `efb2613a6` and kept compact visual access behind that boundary.
- Performance comparison: pre-review `ad8406ed6` binaries versus final `7afc0e1c2`. The preserved before-review binaries match the SHA-256 values in the original evidence bundle.
- Master was fetched before validation. The already integrated terrain/ecology base remains `dbc625342`; later master music-buffer tuning did not overlap this cleanup.
- Linux x86-64, Threadripper 2950X, GCC15, native release `-O3`. The earlier SDL prefix was removed externally, so the repository's pinned dependency script rebuilt a local bundle with the current source patches. Both sides of the benchmark load that same bundle. The full dependency manifest, build logs, command records and binary hashes are included.

## Correctness

At cleanup revision `ba739d5d7`, the broad run passed **671 engine cases**, **818 unit cases**, **40 UI/rendering cases**, and both old/custom replay checksum checks. The engine run skipped 101 cases according to its headless/default selection; units skipped 17 display cases. JUnit records the exact inventories and skip reasons.

After integrating the scene-header boundary change, final revision `7afc0e1c2` rebuilt the affected targets and passed:

- **67 focused engine cases** spanning terrain, scenes, pathfinding, shared matches, custom LAN transfer and malformed input handling.
- **818 headless unit cases**.
- **40 UI/rendering cases**, including software/OpenGL cache behavior, editor import/retry/selection/cancellation, map previews, file dialogs and script editor flows.
- The format-135 local replay (3,000 stored per-tick checksums) and custom network verifier replay (702 heavy-checksum ticks).
- **252 assertions in 8 cases** for both scalar and emulated ARM64/NEON registry/kernel tests. These standalone sources are unchanged by the scene-boundary integration; this is kernel coverage, not full ARM64 engine determinism.
- Two scene dependency-boundary tests, five translation tests and the strict translation audit.

The final integration only relocates registry-dependent scene methods and removes the header's implementation dependency; it does not change simulation code or saved bytes. Focused final engine coverage plus full unit/UI and replay checks was chosen for that boundary. Broad native coverage from the directly preceding cleanup remains attached, with its revision identified rather than represented as a repeat at the final head.

The regression additions cover unsafe registry copying, authoring string lifetimes, canonical serialization, invalid/undersized rings without field mutation, unused slow profiles, actual serial/worker pipeline capture and stale-publication rejection, all 16 wrapped edge masks for custom visual aliases, distinct saved visual metadata, and the actual file/palette interaction paths. Invalid nesting is bounded before constructing a large JSON tree.

## Performance interpretation

The refresh uses randomized paired release runs: one complete warm-up round and ten measured repetitions for full matches and software rendering, plus three warm-up calls and fifteen randomized rounds for kernels. Full match and render processes are pinned to CPU29. Software rendering uses X11 at 1280×800, zoom1, 20 warm-up and 120 measured frames, with presentation disabled. This differs from the original bundle's inherited Wayland setup; use the new before/after pairs for cleanup comparisons.

CPU, wall time, setup CPU and peak RSS are retained separately. Every full-match variant uses FourSquares1, four Warrush AIs and seed2026, and must finish at 17,090 ticks with identical team outcomes and repeatable per-case checksums. The additional `before-16384` case measures the old binary on exactly the same large map/save as the new binary.

Other host workloads were possible. These measurements remain exploratory: they do not satisfy the requested quiet-machine acceptance criterion or establish a broad match-workload pass. Peak RSS includes temporary parsing/allocation high-water usage, not just persistent registry storage. Full Windows, native ARM64 and browser/WASM engine checksum comparisons, additional representative matches and GPU performance remain outstanding.

## Measured results

Changes below are mean paired differences, with bootstrap 95% intervals over the ten pairs. Negative means less time or memory. These intervals do not remove shared-host bias.

| Comparison | CPU change | Interval |
| --- | ---: | ---: |
| Built-in map, final vs pre-review | +0.26% | -0.69% to +1.27% |
| 259 vs 7 definitions, final | -0.37% | -2.53% to +1.84% |
| 1,024 vs 7 definitions, final | +0.16% | -1.75% to +2.47% |
| 16,384 vs 7 definitions, final | +0.19% | -2.25% to +2.33% |
| 16,384 definitions, final vs pre-review | +1.80% | -0.38% to +3.85% |
| Software frame, 16,384 definitions, final vs pre-review | **-24.19%** | -30.94% to -18.07% |
| Software frame, 16,384 vs 7 definitions, final | +0.02% | -7.05% to +7.01% |

The CPU match figures time the warmed simulation segment, as specified in the reproduction notes; setup is separate. Maximum-registry setup CPU increased **16.35%** (interval +14.91% to +17.76%), from a median **1.463 s to 1.693 s**. This is a measured cold-load tradeoff, not a universal performance improvement.

Maximum-registry headless peak RSS fell **58.84%**, from median **300.2 MiB to 123.6 MiB**. Renderer peak RSS fell **34.71%**, from **506.7 MiB to 329.7 MiB** using the matched X11 setup. The two-byte scene visual plane adds a small per-cell allocation, while streamed serialization avoids retaining a second expanded registry JSON tree.

Equivalent used custom gradient kernels measured **6.8–13.6% less CPU** than the static kernel across 256²/512² and 259/1,024/16,384 definitions. Built-in dispatch measured -0.25%/-0.07%. Full arrays are checked against the static result before timing; the separate distinct-cost stress case is absolute workload evidence, not an equivalent-baseline speedup.

Host load ranged from about 1.3–5.8 in match runs and 3.6–20.8 in renderer runs. Full-match confidence bounds still cross the requested 2% threshold in some comparisons; **qualification remains inconclusive**. The raw data supports a substantial maximum-size memory/frame improvement, but does not establish the strict full-match performance gate.

## Reproduction and contents

`evidence.tar.gz` contains `payload/`. The payload's SHA-256 manifest covers each file. It includes commands, scripts, raw results/CSV/logs, JUnit, relevant saves/maps/replays/checksums, screenshots, toolchain/dependency metadata and review findings. The full setup instructions and older compatibility fixtures remain in the [original evidence](https://github.com/Globulation2/glob2/blob/96a3f949bf4f4aeca3ea521d9b940a71d5fa999a/artifacts/runtime-terrain-evidence/README.md).

Run from the checked-out final revision. Replace the prefix paths with your local pinned build:

```sh
python3 scons/sdl3_dependencies.py --prefix build/runtime-terrain-sdl/prefix --work build/runtime-terrain-sdl/sources --jobs 10
GLOB2_SDL3_PREFIX="$PWD/build/runtime-terrain-sdl/prefix" scons -j10 release=1 server=0 tests build/linux/client/release/src/glob2 software-render-benchmark
python3 test/build_system/test_scene_boundary.py
python3 test/test_translations.py
python3 data/check_translations.py --strict
```

`validation-commands.json` records the broad cleanup commands; `integrated-commands.json` records the final revision commands, including fixture paths for replay checks. These JSON command arrays preserve arguments exactly. Kernel builds use the same standalone sources and include paths as the original evidence, now with updated `TerrainRegistryTest.cpp` (scalar define `GLOB2_GRADIENT_SCALAR`; ARM64 cross compiler and `qemu-aarch64 -L /usr/aarch64-linux-gnu`).

To reproduce measurements, place the supplied benchmark scripts under `artifacts/terrain-runtime-review/` and the pre-review binaries under its `before-review/` directory. Build those binaries from `ad8406ed6` using identical dependencies. Use the fixtures and path layout documented in the original evidence. Start with fresh measurement output directories and substitute a valid CPU consistently if CPU29 is unavailable. The scripts record every full-match and renderer invocation. Run the commands in `measurement-commands.json` with:

```sh
export LD_LIBRARY_PATH="$PWD/build/runtime-terrain-sdl/prefix/lib"
export SDL_VIDEODRIVER=x11
```

Rendering elapsed wall time includes loading; frame wall times are recorded separately in renderer logs. Full-match CPU excludes its initial 1,000 warm-up ticks, while its run wall time includes them. Do not interpret those different windows as CPU utilization.
