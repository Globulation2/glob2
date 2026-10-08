# Controlled rendering measurements

Each comparison uses six counterbalanced pairs. Percentage changes are medians of paired changes, not ratios of separately aggregated medians. Raw data and bootstrap median intervals are in `paired-summary.json`. Positive CPU, RSS and latency changes mean higher cost.

## measure-architecture-final

| Fixture | Cores / compute participants | Baseline TPS | Merged TPS | Candidate TPS | Paired TPS change | CPU / tick change | RSS change | Owner p95 change | Tick interval p99 change |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| small | 2 / 2 | 490.2 | 466.5 | 499.0 | +0.72% | +2.79% | +4.72% | +0.88% | +0.85% |
| small | 4 / 4 | 519.1 | 500.7 | 534.8 | +3.03% | -0.58% | +4.60% | -0.06% | +0.08% |
| small | 8 / 4 | 536.6 | 528.9 | 579.9 | +5.14% | -5.82% | +4.75% | -1.66% | -1.50% |
| large | 2 / 2 | 222.4 | 195.2 | 234.4 | +5.36% | +2.05% | +1.26% | +1.80% | +5.15% |
| large | 4 / 4 | 242.6 | 199.9 | 266.7 | +10.45% | +1.28% | +7.66% | -2.89% | -1.42% |
| large | 8 / 4 | 260.8 | 221.9 | 291.2 | +10.61% | +0.66% | +7.09% | -4.29% | -1.86% |
| dense | 2 / 2 | 50.4 | 36.4 | 51.6 | +5.09% | +13.46% | +0.73% | -1.00% | +1.25% |
| dense | 4 / 4 | 51.0 | 37.0 | 55.4 | +8.46% | +15.07% | +1.74% | -3.11% | -0.19% |
| dense | 8 / 4 | 54.7 | 39.5 | 59.1 | +7.99% | +15.77% | +1.82% | -4.52% | -0.39% |

| Fixture | Cores | Candidate frame p95 (ms) | Candidate snapshot age p95 (ms) | Baseline frame p95 (ms) | Baseline snapshot age p95 (ms) |
|---|---:|---:|---:|---:|---:|
| small | 2 | 18.78 | 37.0 | 30.08 | 49.5 |
| small | 4 | 15.54 | 32.0 | 26.67 | 43.5 |
| small | 8 | 15.03 | 31.5 | 25.93 | 41.0 |
| large | 2 | 28.34 | 92.0 | 44.36 | 67.5 |
| large | 4 | 17.74 | 100.0 | 32.52 | 46.0 |
| large | 8 | 14.22 | 93.5 | 30.68 | 43.5 |
| dense | 2 | 33.36 | 112.0 | 66.95 | 73.0 |
| dense | 4 | 19.79 | 121.5 | 61.71 | 57.0 |
| dense | 8 | 17.79 | 117.0 | 57.89 | 57.0 |

## Measurement scope

- CPU is time across all engine-process threads; it excludes the external X server, compositor and kernel work outside the process.
- Allocations count tracked snapshot storage allocations, not every heap allocation. Buffer retention is sampled at measurement-window boundaries; these readings are not continuous lease peaks.
- Owner iteration includes shared capture and owner work. Tick completion intervals include waits and pacing. The legacy `simulation_ns` sub-scope is not used as the cross-revision critical-path comparison.
- Software runs use SDL/OpenGL with software rendering forced and one llvmpipe thread. Physical-GPU runs use the native OpenGL backend on the RTX 2070 SUPER display and are reported separately.
- Graphics, compute and driver threads compete within the selected CPU budget. Core reservation does not isolate package power, interrupts, memory bandwidth or shared caches.
- The historical revisions predate later terrain artwork/rendering changes. The clean-master profiling control reproduces periodic animated-water cache stalls; this explains why historical frame-age comparisons do not isolate this PR.

