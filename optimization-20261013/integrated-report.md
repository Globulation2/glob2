# Integrated-head turret optimization evidence

Tested revision: `c96908b3ab4defd54ec363c5eab70ccf0d194d8b`; merged master/base `277510840` (full hash in feature Git history). Separate dataset from the optimization-isolation matrix at `5ca02001d`. Release GCC13.3, Linux x86_64; exact build commands/flags in `../merged-release-build.log` and test provenance. Shipping targeting and hiring unchanged; cached firing remains benchmark-only.

## Validation

| Invocation | Result |
|---|---|
| engine | test cases:     142 ;     142 passed ; 0 failed ; 1517 skipped; assertions: 2982053 ; 2982053 passed ; 0 failed ; |
| unit | test cases:     92 ;     92 passed ; 0 failed ; 1076 skipped; assertions: 139370 ; 139370 passed ; 0 failed ; |
| whole-engine | test cases:     2 ;     2 passed ; 0 failed ; 2 skipped; assertions: 25336 ; 25336 passed ; 0 failed ; |
| opportunities | test cases:    1 ;    1 passed ; 0 failed ; 3 skipped; assertions: 9201 ; 9201 passed ; 0 failed ; |
| stock-L4 | test cases:      1 ;      1 passed ; 0 failed ; 1658 skipped; assertions: 159000 ; 159000 passed ; 0 failed ; |
| stock-L8 | test cases:      1 ;      1 passed ; 0 failed ; 1658 skipped; assertions: 159000 ; 159000 passed ; 0 failed ; |
| invalid-installed-catalog | test cases:  1 ;  1 passed ; 0 failed ; 1658 skipped; assertions: 15 ; 15 passed ; 0 failed ; |
| legacy-installed-catalog | test cases:  1 ;  1 passed ; 0 failed ; 1658 skipped; assertions: 11 ; 11 passed ; 0 failed ; |

Initial broad engine invocation incorrectly included two resource catalog fixtures that require fresh processes after other cases initialized the catalog cache. Five assertions in those two cases failed. The exact failed log is retained as `engine-initial-invalid-grouping.log`. The corrected group excludes them, and each fixture passes in a separate fresh invocation. This is an invocation correction, not an unreported source fix.

Whole-engine cases verify original/shadow orders, checksums and final saves and cached trajectory determinism across participants 1/2/4/8. Original/shadow continuation passes 9/9 cuts. Cached continuation still fails cuts 39 and 40, passing the other seven; the harness explicitly reports those unresolved readiness gates. Passing this harness does not imply cached saves are qualified.

## Reserved-core full-engine rerun

Combined optimization only, capacities 4/8, participants 1/2/4/8. One warmup, ten paired repetitions, rotated original/shadow/cached order; timing and CPU-probe cohorts separate. 16 accepted cohorts, 720 timing and 720 CPU-probe observations. All exit zero and no sampled blocking contenders. Same masks and exclusion rule as the isolation campaign. Host activity and thermal observations are retained. Do not pool these samples with the older binary.

| L | Workload | Participants | Original µs/tick | Cached µs/tick | Mean change (95% CI) | CPU change | Workload gate |
|---:|---|---:|---:|---:|---|---:|---|
| 4 | empty100 | 1 | 532.5 | 457.0 | -14.19% [-14.76, -13.58] | -14.19% | pass |
| 4 | empty100 | 2 | 371.0 | 376.6 | +1.51% [+0.75, +2.27] | -12.64% | FAIL |
| 4 | empty100 | 4 | 270.9 | 203.7 | -24.79% [-28.10, -22.18] | -13.97% | pass |
| 4 | empty100 | 8 | 324.8 | 244.3 | -24.78% [-26.46, -23.07] | -12.39% | pass |
| 4 | combat100 | 1 | 843.8 | 845.3 | +0.18% [-0.07, +0.44] | +0.21% | pass |
| 4 | combat100 | 2 | 658.3 | 650.9 | -1.14% [-3.61, +0.56] | +0.08% | pass |
| 4 | combat100 | 4 | 462.6 | 471.2 | +1.86% [+0.97, +2.86] | +0.51% | pass |
| 4 | combat100 | 8 | 499.6 | 502.9 | +0.66% [-0.91, +2.24] | -0.92% | pass |
| 4 | ordinary4 | 1 | 403.9 | 404.8 | +0.24% [-0.31, +0.66] | +0.24% | pass |
| 4 | ordinary4 | 2 | 356.7 | 357.6 | +0.25% [+0.03, +0.51] | +0.74% | pass |
| 4 | ordinary4 | 4 | 170.3 | 170.8 | +0.27% [-0.83, +1.42] | -0.11% | pass |
| 4 | ordinary4 | 8 | 207.7 | 226.7 | +9.16% [-0.53, +23.59] | +2.32% | FAIL |
| 8 | empty100 | 1 | 531.3 | 456.7 | -14.05% [-14.47, -13.58] | -14.13% | pass |
| 8 | empty100 | 2 | 369.2 | 375.5 | +1.70% [+1.19, +2.19] | -12.39% | FAIL |
| 8 | empty100 | 4 | 262.2 | 202.3 | -22.86% [-24.56, -21.05] | -11.55% | pass |
| 8 | empty100 | 8 | 329.3 | 242.3 | -26.41% [-28.36, -24.58] | -12.53% | pass |
| 8 | combat100 | 1 | 842.3 | 846.1 | +0.45% [+0.12, +0.81] | +0.46% | pass |
| 8 | combat100 | 2 | 658.5 | 656.8 | -0.27% [-1.93, +1.09] | +0.52% | pass |
| 8 | combat100 | 4 | 466.0 | 469.5 | +0.74% [-0.62, +2.00] | -0.46% | pass |
| 8 | combat100 | 8 | 499.1 | 510.5 | +2.28% [+0.87, +3.64] | +0.00% | pass |
| 8 | ordinary4 | 1 | 403.9 | 404.0 | +0.04% [-0.92, +0.79] | +0.04% | pass |
| 8 | ordinary4 | 2 | 401.9 | 378.3 | -5.88% [-12.42, +0.30] | -4.97% | pass |
| 8 | ordinary4 | 4 | 173.2 | 174.7 | +0.82% [-2.62, +5.26] | -0.62% | pass |
| 8 | ordinary4 | 8 | 192.4 | 200.4 | +4.16% [+2.39, +6.23] | +4.15% | FAIL |

Empty improvement is gated on paired median change and its CI, not this table’s mean alone. All median fields and raw samples are in `summary.json`/`timing.csv`. Combat and ordinary gates are the plan’s mean point thresholds; crossing CI bounds remain uncertainty. CPU scopes include every planning callback thread but incompletely attribute shared executor bookkeeping; process CPU includes all work. Incremental capture is a paired difference. Probe overhead and join wait are separate as in the main report.

## Stock reference opportunities

Twenty stock Numbi/Castor battles per capacity, same seeds and orders as the earlier corpus. Hypothetical first-valid evaluation mutates no gameplay. Misses count as infinite added delay in percentile calculations. These are reference-trajectory results, not qualification of different shots.

| L | Opportunities | Target agreement | Missed | Censored | p95 delay | Max completed |
|---:|---:|---:|---:|---:|---:|---:|
| 4 | 868 | 756 (87.10%) | 2 | 0 | 0 | 4 |
| 8 | 868 | 758 (87.33%) | 2 | 0 | 0 | 0 |

## Assessment and coverage

At the integrated head, both capacities pass the three workload point gates at participants 1 and 4. Both fail the empty-range improvement gate at participant 2 (+1.5%/+1.7% means). At participant 8, ordinary control fails for L=4 (+9.2%, very wide interval) and L=8 (+4.2%). Retained outliers are not removed. All process CPU point changes stay below the 15% limit. L=8/four-participant ordinary control has a CI crossing its regression budget despite its point pass. These are remaining qualification costs, not permission to weaken gates.

Performance gates and capacity ordering must be assessed from this complete table together with the individual optimization campaign; no default/live configuration is promoted by this rerun. The original short-window failed controls remain relevant. Continuation at two cuts remains a hard live-activation blocker. Preserve the first-valid rule, four-tick budget and independent off-default experiment scope.

Linux native release only. Supported-platform execution equivalence and future live replay/network/golden acceptance remain unavailable. The ordinary firing path is unchanged, and the future persistent pipeline, experiment registration and live-rule versioning are still required. The archive contains both measured and integrated binaries; recorded hashes disambiguate them.
