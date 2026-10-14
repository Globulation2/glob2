# Lean concurrent engine: review evidence

Evidence for stacked draft PRs [1181](https://github.com/Globulation2/glob2/pull/1181), [1182](https://github.com/Globulation2/glob2/pull/1182), [1183](https://github.com/Globulation2/glob2/pull/1183), [1184](https://github.com/Globulation2/glob2/pull/1184). Generated evidence lives on this dedicated branch, never in the engine source tree.

The requested outcome is simpler ownership and execution, preserved concurrency and fixed publication deadlines, and a CPU-only path with no GPU work. GPU performance qualification was explicitly removed from scope by the maintainer. No GPU speedup is claimed.

## Source and provenance

- Baseline: `7e23a914cdd6937e35987c0244585437eb786176`, public branch `codex/lean-compute-baseline-linux`. Local macOS baseline `0c93d3821` has the identical Git tree `39fb8dcb98c1ba0f10579920a7bf995874270d6c`. Includes PRs 1161/1180 and prerequisite compilation repairs. Baseline binaries and inputs were frozen before candidate comparisons.
- CPU timing candidate: `d6372abb48c16150331e8b7bf65e8bbf24cdd7cd`; final broad native/browser validation: `4fc5877da7c5c6aff71d3b408bf37683b1446899`. Production source is identical between these two revisions (documentation and stack parent merges only).
- Final code: `a28ce1f80dc966d81a5d8aaf632e86cd9d38e62a`. The final incremental change removes a redundant GPU request owner pointer, unused includes and a stale comment. Reservation release uses the existing retained execution state; CPU execution code is unchanged. Additional final-owner logs cover this change. Native incremental builds started before its commit was recorded, so their provenance reports 4fc5877 plus exactly these three modified files (source hash retained); the source content is the committed a28ce1f80 tree. The exact patch is attached as `commands/final-owner.patch`.
- Stack heads: state `c673d31f781b794e132b3d4848427e63bdb1106a`; execution `034753e2f93309da4780f2623cd7abf98a8eacb0`; GPU lifecycle `d6a4a752aa5c97e6197b6bcb1de5801cca5f34fc`; integration `a28ce1f80dc966d81a5d8aaf632e86cd9d38e62a`.
- Latest fetched master: `21f432e0c0d10751e7189af37b0acfe2c19eeff6`; no newer integration changes were present. PRs are tested stack heads, not merges into master. No PR was merged.

Linux: Ubuntu 24.04 x86-64, kernel 7.0.0-38, Ryzen 6800H, GCC 13.3.0, SCons 4.5.2, NVIDIA RTX 3070 Ti Laptop, driver 580.178.04. macOS: ARM64, macOS 26.6.2 (25G83), Apple clang 21.0.0, SCons 4.11.1, Python 3.14.7. Browser: release Emscripten builds, Playwright Chromium; exact compiler/dependency paths, defines, source hashes and flags appear in build logs and `GLOB2_TEST_PROVENANCE` records. Existing pinned repository dependencies were used; no new runtime dependencies.

## Correctness results

- Linux focused native release: 79 unit cases / 641,249 assertions; 166 engine cases / 515,665 assertions, all passed. Executor, AI scheduling, snapshots, gradients, persistence, backend switching and lifecycle covered.
- CPU-only lifecycle: 34 assertions passed, zero hits on ten GPU service/control/lifetime/backend entry points under GDB across participant and configuration changes, saving and clearing. A separate CLI driver trace contains no GPU driver loads/calls during CPU startup, ticks, save and teardown. Script and traces included.
- Baseline/candidate 1/2/4/8/auto equivalence includes per-tick checksums, replay orders, final saves and continuation, including actual legacy format 121 saves and multiple AI implementations. Three retained small/ordinary/large mature workloads also match at 1/2/4/8 for CPU/OpenCL/Vulkan. Device correctness requires real commits, not CPU fallback.
- Real NVIDIA OpenCL: 31 cases / 675 assertions passed; additional participant test 95 assertions at 1/3/6 participants with 12 actual device fields. Vulkan: 22 cases / 2,677 assertions passed. Raw outputs include retained failures, independent weighted-path oracle, wrapping, movement classes, caps, reuse and driver completion. Isolated fault injection covers admission, acquisition, query/wait failure and rollback; shipping binary injection-hook scan empty.
- Browser serial: 180 cases / 7,972,664 assertions; threaded: 180 cases / 7,973,565 assertions. Both passed. Eight browser determinism/golden tests passed. NVIDIA WebGPU: 66 retained JavaScript fields passed, plus C++ 149 assertions with 15 actual device commits and five cancellation recoveries.
- One combined WebGPU attempt failed device creation with NVIDIA Xid32 / `VK_ERROR_DEVICE_LOST`. It remains in the evidence. Isolated and fresh combined retries passed. This failed attempt was never counted as GPU correctness; absence of a GPU commit fails the test.
- Final ownership cleanup: Linux and macOS both passed the same 79-case unit and 166-case engine selections; final CPU-only GDB guard passed. Final OpenCL 31 cases, Vulkan 22 cases, serial/threaded browser lifecycle and two real WebGPU tests also passed.
- macOS release snapshot/backend follow-up: 46 cases / 158,166 assertions passed. Earlier broad core and unit selections also passed (logs retained). Nine mature-workload Linux/macOS checksum, replay and save files match byte for byte. Browser/native 1,500-tick traces and golden match record agree.
- SIM_REVISION is 61, with regenerated and verified golden match and trace. Save encoding version 163 and compatibility floor 58 are unchanged. Shader receipts, CLI reference, simulation version, documentation and affected Python contracts checked.

Normal gameplay deliberately no longer performs per-field numerical GPU fault detection. Input checks, solver convergence, confirmed driver completion, cancellation, fail-closed uncertain retirement and CPU recovery remain. The transient device-loss result demonstrates why this is not a claim that all GPU/device behavior is fixed.

## CPU-only release measurements

One warmup and ten rotated paired repetitions at 1/2/4/8 participants for each retained workload; Linux small and four uncertain macOS workload/participant cells extended to twenty pairs. Same frozen inputs (ticks 8192–10240), dependencies and ordinary `release=1` builds; no development/PCH/unity builds. Startup/save CPU is separate from warmed CPU per tick; raw process CPU, elapsed and peak RSS are retained.

Median paired warmed CPU-per-tick changes, candidate relative to baseline:

| Platform/workload | 1 participant | 2 | 4 | 8 |
| --- | ---: | ---: | ---: | ---: |
| Linux small (20 pairs) | +0.52% | +0.59% | +0.99% | +0.94% |
| Linux ordinary (10) | -0.74% | -0.65% | -0.20% | -2.28% |
| Linux large (10) | -0.56% | -0.57% | -0.45% | -1.07% |
| macOS small | -0.62% | -0.95% | -1.12% | +0.38% |
| macOS ordinary | -3.07% | -1.13% | -2.25% | +0.29% |
| macOS large | -2.14% | -0.29% | -0.92% | -0.14% |

No observed repeatable CPU-only regression above 2% in these workloads. This is workload evidence, not a guarantee for every map. Linux small/8 uncertainty remains: the bootstrap interval upper bound is +2.58%, although its twenty-pair median is +0.94%. Full summaries include intervals and startup/wall/process figures. Initial slower candidates and the investigation are retained rather than discarded.

Linux peak RSS is roughly 2.3 MiB higher (about +5% small, +2–3% ordinary, +1% large), despite slightly lower heap/anonymous retention. Matched `/proc` sampling attributes this mostly to additional clean resident executable/read-only pages. No claim of overall Linux process-memory reduction. macOS RSS was generally lower. GPU timing files were collected earlier but are not acceptance criteria and support no speedup claim.

## Reproduction

Build commands (run in each respective source checkout; Linux existing SDL prefix is explicitly selected):

```sh
GLOB2_SDL3_PREFIX=/home/bradley/glob2/build/sdl3/prefix python3 tools/dev_build.py dev_fast=0 release=1 --build=artifacts/lean-compute/native artifacts/lean-compute/native/src/glob2 engine-tests unit-tests -j8
PYTHONPATH=. python3 tools/dev_build.py dev_fast=0 release=1 --build=artifacts/lean-compute/build artifacts/lean-compute/build/src/glob2 engine-tests unit-tests -j4
PATH=/home/bradley/.nvm/versions/node/v20.19.4/bin:$PATH scons target=web release=1 --build=artifacts/lean-compute/web web-tests web-serial web-threaded -j4
```

Exact native/browser test selections and environment are in `commands/final-validation.py` and `commands/glob2-final-resume.py`; logs report commands/provenance and assertion counts. The first script records the failed combined hardware attempt; the resume records successful retries and corpus comparisons. Original machine paths are intentionally retained so command provenance is unambiguous. Benchmark `metadata.json` and every `measurements.jsonl` contain exact arguments, input/binary hashes, run order and raw measurements. Remap paths to extracted `inputs.tar.gz` and locally built binaries when reproducing.

Representative checks:

```sh
python3 test/check_parallel_compute.py CANDIDATE --baseline BASELINE --output artifacts/parallel-check
python3 test/benchmark_parallel_compute.py BASELINE CANDIDATE INPUT_MANIFEST --verify --threads 1 2 4 8 --output artifacts/corpus-check
python3 test/benchmark_parallel_compute.py BASELINE CANDIDATE INPUT_MANIFEST --repeats 10 --threads 1 2 4 8 --output artifacts/cpu-pairs
python3 test/check_sim_revision.py --base codex/lean-engine-execution
python3 tools/cli_reference.py --binary CANDIDATE --check
python3 tools/check_docs.py
```

Archives use standard gzip tar with duplicate contents encoded as hardlinks; extract normally. Each archive has a SHA-256 file manifest. `SHA256SUMS` authenticates the published files. Inputs, actual checksums/replays/saves, full raw timing runs and logs are attached, not merely described.

## Limits and acceptance

No Windows, Android/iOS or Apple GPU execution coverage was performed. Browser coverage is Chromium/NVIDIA plus serial/threaded CPU; it does not establish other browser/device coverage. No interactive maintainer playthrough performed. Hosted cheap contracts do not replace the local evidence. All four PRs remain draft for maintainer review; acceptance is not asserted on the maintainer's behalf.
