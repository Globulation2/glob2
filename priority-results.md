# Representative per-idea timing results

These are an accelerated screen, not full-fixture acceptance. Baseline da57b459f1eb20b9b4a08d25eb502711dd81986b, GCC 15.2.0, four executor participants, 4,096 measured continuation ticks per run. Each pair alternates baseline/candidate ordering. Loading and teardown are excluded. Negative percentages mean less time.

| Idea | Fixture | Pairs | Owner CPU change, 95% CI | Wall change, 95% CI |
|---|---|---:|---:|---:|
| hiring | hiring | 10 | -3.39% [-10.93, +3.73] | -1.73% [-5.52, -0.05] |
| stats | sparse | 10 | -10.16% [-13.60, -3.10] | -6.60% [-8.69, -1.92] |
| queues | sparse | 10 | -6.07% [-8.06, -2.10] | -2.34% [-4.47, +0.84] |
| players | sparse | 20 | +2.29% [-4.48, +4.31] | +2.21% [-0.96, +3.44] |
| vectors | dense | 20 | +1.87% [-0.61, +3.89] | +0.56% [-0.23, +2.04] |

Statistics/checksum scans show a clear owner and wall improvement in this representative case. Queues show an owner CPU improvement, with wall time inconclusive. Hiring reduces instructions substantially, but this timing screen is noisier; the very small upper wall-time bound below zero should not be generalized. Player reservation and hiring-vector reuse demonstrate allocation savings without a timing gain distinguished from noise. Neither twenty-pair repeat meets the specified persistent-regression rejection threshold.

Own compiler/profile jobs were paused for the screen and resumed in a finally block. No unrelated processes were stopped. Runs waited for visible compilers to clear; native-host-load.jsonl records remaining machine activity. Short compiler bursts can occur between observations. Bootstrap intervals describe these pairs, not all gameplay workloads. CPU affinity was unrestricted. The subsequent complete campaign will use separate matched core groups plus a standalone final-combination control.

See early-results.md for the instruction/allocation mechanisms and test coverage. All five patches remain provisional in draft PR 963; final integration/full matrix acceptance is pending. Current integration candidate: `7954cff48a2d5b6ec3b363197f8d4a343ee8b4d4`.
