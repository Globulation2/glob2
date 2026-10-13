# PR #1152 cleanup verification

Tested commit: `8ca5c77059358d20e1c6eb526ee0d6fc37cbc04e`. PR base: `b1f1affb1ca39682c428003250ddac574a6608ed`. Fetched master: `277510840054b1416f2defd7d86fe1431dd0ddc0`; no newer merge execution is claimed. Platform: Darwin arm64, Apple clang version 21.0.0 (clang-2100.3.34.2), Python 3.14.7. Build: normal development command with `dev_fast=1`, `-O0 -g1`; correctness only.

## Review and changes

See `review-findings.md` for the independent sub-agent review and follow-up. Simplified snapshot loops, pipeline formatting and frame-chain naming; corrected documentation/version references. Fixed sticky immediate/submission failures and retained callback contexts before enqueueing work. Successful calculations, fixed deadlines, field encoding and old-save migrations remain unchanged. Independent live calculation remains for equivalence/legacy loading.

## Checks

- Final clean unit suites: 48 passed (47 ComputeExecutor, one ReadOnlyPhase), 1.2 seconds of reported test time. New repeated-lane/empty-group happy path covers serial and threaded execution. Completion/resumption covers early/late handoff and Off/Waits/Trace modes.
- Ten repeated runs of the two scheduling edge cases passed (20 case executions).
- Engine selection: 18 TeamStatsPipeline, 22 TeamStatsSave, 32 WorldSnapshot and seven AIPipeline cases. Initial run: 77 passed, one display skip, one 120-second timeout in the existing 36-configuration AI save-continuation matrix. The timeout is retained in `engine-final.log/xml`.
- Isolated AI save-continuation retry passed in 243.2 seconds with a 600-second limit. Combined selection: 78 engine cases passed, one display skip, no unresolved assertion failures.
- All statistics-specific cases passed, including immediate/deferred calculation and submission failure persistence, worker-count pending/publication checksums, raw/snapshot equivalence, smoothing, retained readers, pending save/load/resave and formats155–157 migration. Other legacy saves and published observation tests also passed.
- `git diff --check` passed. Final binaries report clean commit provenance matching the tested SHA.

Exact commands and source/toolchain inputs are in `provenance.json`, with raw logs/JUnit and `cleanup.patch` in the archive. Initial asset generation failed in spawned Python workers (`ModuleNotFoundError: tools`); rerunning the build with `PYTHONPATH=.` passed. Both failure and successful build logs are retained. Formatting used an isolated ignored clang-format 21.1.8 environment; no runtime dependency changed.

## Limits

No fresh release timings/Callgrind, sanitizer run, full engine matrix, actual browser/Windows/mobile/Linux engine execution, cross-platform per-tick traces or GUI/gameplay review for this cleanup. This correctness run makes no new speedup claim; earlier profiling artifacts remain separate evidence for their tested revisions. Existing OpenCL convergence and other platform limitations from earlier PR comments remain unresolved. PR stays draft.
