# CI recovery evidence for PR #1271

Final PR head: `171fa9bd4`; integrated base: `c095ec7c1` (including the Savannah and Anthill merges). Broad native and CLI validation ran at `b08f040c9`. Later changes update only the Maxima expected checksum JSON, assert a busy checkpoint in the Python CLI test, and format the TypeScript fixture. Production C++ and the CLI executable are unchanged; `verification.json` checks the executable hash against parity evidence. The final clean head was rebuilt and passed three engine smoke cases (PNG observation, frozen Generator Studio world, and JavaScript worker/replay/save parity) and all eight account-export tests.

## Results

- Build-system contracts: 651 cases, no failures, 5 skipped, using the pinned asset encoder Python.
- CI Python contracts: 162 cases, no failures, 2 skipped.
- Native units: 1,159 passed, 89 display/benchmark cases skipped (1,248 total in JUnit).
- Focused native engine: 43 passed, one display case skipped. Covers gradient scheduling and invalidation, historical and current saves, worker lifecycle, market fields, ground/air cargo, diagnostics, JavaScript and Generator Studio.
- JavaScript strict corpus: both binaries passed, with clean source/binary provenance checks. CLI checks pass complete 1/4-worker replay/save equivalence and six saved continuations across both fixtures.
- Map CLI: 56 commands passed. Diagnostics: 21 commands passed, including PNG and threaded output, complete per-tick checksum/final-save equality against diagnostics-off runs, and continued saves.
- Gradient CLI: all 15 worker/delay combinations and repeat reference agree byte for byte; eight save phases agree across three worker settings. The added busy-checkpoint path was separately exercised against the same executable: at tick 8 it retains four jobs and rejects a delay change with the required diagnostic. Later idle checkpoints accept it.
- Scoped gradients: all complete sidecars match at 1/4 workers, including 1,024/2,048-tick scenarios and a v108 continuation. The 1,024-tick SHA256 is `0f1b04dd698a5b044a4d4a87c4728b2aa6553410857040b15a5d89fb9172452c`, exactly the actual trace reported by GCC 11 in the original failing master run.
- Maxima: all 512 complete v115 continuation hashes and all 256 midpoint save/reload records pass at 1/4 workers. Regeneration is gated on a successful independent midpoint reload.
- Save-header parser: 21 cases passed. Documentation: 310 Markdown files checked, zero errors. Sim-version/committed-record guard passes.

The source checkpoint and historical checksum epochs remain untouched. Only explicitly current checksum baselines were regenerated. No simulation rules, save floor, replay gate or network gate change; the sim revision remains master’s 100.

## Environment and limits

Ubuntu 26.04 x86-64, GCC 15.2.0, matching pinned SDL3 family. Builds use `dev_fast=1`, `-O0 -g1`: correctness evidence only, with no shipping or runtime-performance claim. Exact commands, binary hashes and platform information are in `verification.json`. The macro syntax compile reproduces the Windows near/far collision on the host compiler; it does not replace a Windows build or execution.

Windows, macOS, Android, browsers, sanitizer/coverage and the full building-delay sweep were not executed locally. Full hosted PR/master verification is still pending; a selected or partial green result is not a full checkpoint. A hosted FFmpeg download timeout was not reproduced locally and remains an infrastructure observation for follow-up if it recurs. No active master run was cancelled.

Original failure: https://github.com/Globulation2/glob2/actions/runs/40425349089 . GCC 11 trace anchor: https://github.com/Globulation2/glob2/actions/runs/40425349089/job/121936967552 .

Author acceptance: this focused evidence is sufficient to merge the repair under AGENTS.md, with the listed hosted/platform gaps retained. The user explicitly authorized merging repair PRs. The hourly CI follow-up will verify the retained full master result and investigate new failures.

The sample diagnostic PNG was visually inspected. Its capture metadata and successful six-capture summary are included under `png-diagnostics/`.
