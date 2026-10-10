# PR 1045 final review and rebase verification

Source: `0520c89a9` (full SHA in source-manifest.json). Base: `4ca7b086359891010281dd994dbbcd8cceadadad`.

The branch was rebased on current master, then two independent subagents reviewed runtime contracts/lifecycle and offline kernel qualification. Review fixes consolidate all runtime/benchmark GPU parameters into one shared descriptor table; reject invalid or heterogeneous groups before execution; safely drop malformed optional observations; separate GPU execution from qualification policy; bind completion receipts to protocol, sources, corpus and raw results; restrict final qualification to development survivors; and preserve untouched holdouts when setup fails. Both re-reviews reported no remaining actionable findings. Kernel source and its six execution parameter sets are unchanged.

Final PR head: `13843165bd7e778650142fe884c7af37b40b56f1`. The only subsequent change after the fully tested `0520c89a9` revision removes one trailing blank line from `tools/gradient_qualification/backend.py`; `final-source.diff` records it. All 11 qualification contract tests were rerun at the final head and passed. The full release, exactness and GPU evidence below belongs to `0520c89a9`, not a claimed rerun of the full matrix at the whitespace-only child.

## Tested revision results

| Check | Result |
| --- | --- |
| Release game, unit and engine builds | Pass |
| Full release unit suite | 937 passed, 1 failed, 20 display skips |
| Relevant engine suite | 171 passed, 1 display skip |
| CLI smoke suite | 15 passed |
| Qualification contracts | 11 passed |
| Documentation | 306 documents, zero errors; 10 checker tests passed |
| Exact simulation/save/replay matrix | 84 runs passed; identical per-tick traces, replay bytes, saves and continuation |
| Development corpus | 40 layouts, 120 chronological fields, 6,000 exact eligible executions, 480 semantic skips |
| Stress corpus | 18 layouts, 54 fields, 2,700 exact eligible executions, 216 semantic skips |
| Adversarial edges | 120 cases, 1,039 exact executions |

The single failing unit case is `ImageAssets/16-bit RGBA rounds normalized channels to the exporter reference`: two channel-comparison assertions in the same case. This reproduces the previous local failure in unchanged image-loading code. The failure log is retained; a fresh master-only build was not performed to attribute it conclusively to master or local dependency behavior. No introduced gradient test failed. Keep the PR draft; this is not a green full-suite claim.

All three offline candidates again have zero winning development class maps out of 24 under the frozen combined cold/warm gates. These reruns validate exactness and the revised evidence pipeline; they are not a new isolated performance study. Release compilation overlapped part of the offline run, and unrelated host work was present. No candidate is admitted, no final holdout was generated, and no whole-game candidate-removal experiment ran. The prior source-revision-specific development study remains linked from the PR. Accounting remains disabled by default; the earlier overhead study did not pass its limits and has not been repeated for this cleanup commit.

## Environment and reproduction

Linux x86-64, GCC 15.2.0, release `-O3` build, NVIDIA GeForce RTX 2070 SUPER, driver 580.178.04. Full platform, compiler, dependency prefix, Python/NumPy/PyOpenCL versions and kernel build options are in source-manifest.json and the corpus environment/build receipts. The existing qualification virtual environment used NumPy 2.3.3 and PyOpenCL 2025.2.6; this does not claim verification of every version in the general development requirements.

From the source checkout, with the existing SDL3 and recording prefixes:

```sh
GLOB2_SDL3_PREFIX=/home/bradley/glob2-claude/build/sdl3/prefix \
GLOB2_RECORDING_PREFIX=/home/bradley/glob2-vertex-terrain/build/linux/client/release/recording/prefix \
CCACHE=1 taskset -c 0-7 scons release=1 server=0 linker=auto -j8 \
  build/linux/client/release/dev-linker-auto/src/glob2 unit-tests engine-tests

taskset -c 0-7 python3 artifacts/pr1045-final-review/run-suites.py
taskset -c 8-15 python3 test/test_cli_smoke.py \
  --binary build/linux/client/release/dev-linker-auto/src/glob2 \
  --artifacts artifacts/pr1045-final-review/cli-artifacts --junit artifacts/pr1045-final-review/cli.xml
taskset -c 0-7 python3 artifacts/pr1045-final-review/verify-exact.py 0
taskset -c 0-7 python3 artifacts/pr1045-final-review/verify-exact.py 1
python3 tools/check_docs.py
python3 -m unittest discover -s test -p test_check_docs.py

# Use the qualification virtual environment for these commands.
python -m unittest discover -s tools/gradient_qualification -p 'test_*.py'
taskset -c 0-7 python tools/gradient_qualification/run.py --split development --device 1 --output artifacts/pr1045-final-review/development
python tools/gradient_qualification/analyze.py artifacts/pr1045-final-review/development
taskset -c 0-7 python tools/gradient_qualification/run.py --split stress --device 1 --output artifacts/pr1045-final-review/stress
python tools/gradient_qualification/analyze.py artifacts/pr1045-final-review/stress
taskset -c 0-7 python tools/gradient_qualification/check_edges.py --device 1 --output artifacts/pr1045-final-review/edges
```

Adjust dependency prefixes and choose an unused output directory for reproduction. The archive includes the exact runner scripts, suite filters, commands, raw JSONL records, completion receipts, JUnit XML, failure logs, checksums, saves and replays. Files are content-deduplicated: run `restore-duplicates.py` after extraction. Disposable profiles and compiled binaries are excluded. Independent-map counts never include repeated timings or chronological fields as additional layouts.

The exactness matrix compares CPU/OpenCL/automatic modes with accounting off/on, 128² and 256² maps, and 1/4/8 execution slots; worker-count variation is compatibility coverage, not a performance ablation. Eight remains the default and includes the owner. It checks per-tick traces, final saves, replay bytes and continuation after cross-backend save loading, also comparing available prior validated artifacts. Relevant engine suites cover larger gradient fixtures, owner/worker exclusions, runtime continuation, save compatibility and replay boundaries.

Only Linux/NVIDIA execution was verified. Windows, macOS, browser/mobile, other OpenCL devices, controlled rendering contention, a repeated final-commit accounting-overhead study and fresh TSAN runs were not exercised. The previous TSAN evidence remains historical; this cleanup changes descriptors and validation, not the synchronization design. No simulation-version or save-format change is intended or introduced. Exact local results do not substitute for cross-platform execution coverage.
