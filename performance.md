# Final performance measurements

Exactly 1,000 simulation ticks per run; one discarded warmup and three measured repetitions per variant. Baseline `508942f08`, final candidate `ef1f90ed1`. Positive reduction means faster. These small samples do not establish a confidence interval.

| Workload | Baseline simulation median (range), s | Candidate simulation median (range), s | Reduction | Baseline process wall, s | Candidate process wall, s | Process reduction |
|---|---:|---:|---:|---:|---:|---:|
| even12 | 7.784 (7.754–7.822) | 7.300 (7.286–8.013) | 6.21% | 17.180 | 16.701 | 2.79% |
| held-islands8 | 0.821 (0.761–0.840) | 0.831 (0.813–0.839) | -1.15% | 1.975 | 1.991 | -0.78% |
| held-even12-late | 22.890 (22.859–22.929) | 17.202 (17.185–17.710) | 24.85% | 31.032 | 25.210 | 18.76% |
| held-islands12-late | 27.666 (27.586–28.205) | 24.201 (24.148–24.357) | 12.52% | 40.790 | 37.490 | 8.09% |

`held-islands8` contains no Maxima players and is the noise control. Full CPU-time statistics are in `performance.json`; every raw sample, including warmups, is in `timing/results.json`. The two late cases begin at tick 40,000 with 845 and 2,376 units. Map dimensions, rosters and seeds are in `scenarios.json`.
