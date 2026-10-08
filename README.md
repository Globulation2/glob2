# Accepted and merged: PR 963

The maintainer accepted the accelerated local evidence and explicitly requested merging all five improvements, followed by reanalysis. PR [963](https://github.com/Globulation2/glob2/pull/963) was rebased into master as five separate commits, ending at `6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf`. No new optimization is included in the subsequent profiling pass.

## Completed evidence

- Seven GCC 15 variants (unchanged baseline, five independent patches, combination), five fixtures, one/four participants: 70 verified 4,096-tick continuations match every tick, replay bytes and final save bytes; the corresponding 140 allocation/Callgrind runs completed. All retain scheduling delays and normal release optimization with symbols, profile=0 and no -pg.
- Focused native coverage: 130 cases in 101 jobs per candidate; additional concurrent/reset queue coverage: 11 passing cases.
- GCC 13 combined candidate: five fixtures at one/four participants match the baseline. Updated integration candidate `7954cff48a2d5b6ec3b363197f8d4a343ee8b4d4` additionally matches all five fixtures at one/four/32 participants. Upstream SIM_REVISION 36 is retained; this PR does not change simulation version or goldens. The simulation-version contract passed.
- Uninstrumented ordinary-release controls for sparse/four and dense/four match instrumented checksums, replay bytes and save bytes.
- Representative native timing screens: ten pairs for hiring/statistics/queues, twenty for players/vectors. Statistics clearly improves owner and wall time in its sparse fixture; queues improves owner time. Other timing claims remain limited or inconclusive. All five demonstrate their intended instruction/allocation mechanisms. See priority-results.md and early-results.md.
- All five initial fixtures and canonical checksum traces, replays, final saves and commands are downloadable from the [evidence release](https://github.com/Globulation2/glob2/releases/tag/evidence-serial-loop-963). The compressed archive has a SHA-256 manifest and passed decompression validation.
- Complete raw Callgrind files, allocation folded stacks, optimized disassembly and per-variant test/build metadata are in the [mechanism evidence archive](https://github.com/Globulation2/glob2/releases/tag/evidence-serial-loop-963-mechanisms).
- New native CPU/counter reconnaissance uses the exact merged revision, GCC 13, and records host load. See [findings](serial-reanalysis/findings.md) and [measurements](serial-reanalysis/report.md).

## Deliberate limitations

The maintainer replaced the multi-host plan with therig-only work. GCC 13 and GCC 15 were exercised locally; no Mac/ARM64 or devlaptop coverage is claimed.

The exhaustive per-fixture native timing matrix was canceled after acceptance. The complete GCC 13 native suite was also stopped: its log contains 1,307 passing jobs, but there is no successful final aggregate and it must not be described as a full-suite pass. Three UI cases that failed with inherited Wayland all pass under explicit X11; both results are retained. Slow map-generator and broad UI presentation coverage was interrupted and remains unverified. This reduced scope was accepted before merge.

New profiles are process-scoped on a shared host with unrelated builds active. Timing tables are diagnostic, not isolated performance comparisons. Samples and counters identify next experiments; they do not establish speedups for unimplemented proposals. The new profiling pass checks initial/final checksums; exhaustive per-tick/replay/save comparison is the earlier acceptance evidence, not repeated in each CPU profile.

Historical reports on this branch retain their original provisional status text. This document supersedes that status; it does not retroactively expand their coverage.
