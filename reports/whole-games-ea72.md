# Final repeated whole-match comparison

Frozen commit `ea72d9add7d15936c4a3c7688ffb1858eb27c2b9`, binary SHA256 `8ddd2d7680d63f5e3eae096d8189a99acfd4bfd37cb3ead2e39e8a4c59cbfd01`. This report contains two complete passes of the ea72 activation binary: eight predeclared map fixtures × CPU/OpenCL/automatic × two repetitions = 48 valid timings. The prior binary’s one-pass screen remains separate and is not included in these medians.

All modes use eight compute threads, async disabled, 20,000 ticks, and the same loaded fixture and seed. The game is pinned to CPUs 0–7, their SMT siblings 16–23 are protected, and the interference observer runs on CPU15. Each match holds a cooperative GPU0 file lock that excludes our other benchmark jobs; desktop graphics remain active on the device. Disjoint background build/test activity is allowed and logged; shared memory, thermals, clocks, and GPU desktop activity can still vary.

## Large-map priority and overall result

512×512 priority (3 cases): automatic vs CPU equal-case geometric mean +2.55% runtime / +2.52% full wall; forced OpenCL vs CPU -0.35% / -0.03%. Automatic vs best forced: +3.03% / +2.85%; worst per-map regret +5.58% / +5.39%.

All eight maps: automatic vs CPU equal-case geometric mean +2.55% runtime / +2.85% full wall; forced OpenCL vs CPU +7.76% / +7.68%. Automatic vs the best forced mode: +2.73% / +2.98%; worst per-map regret +16.17% / +16.25%.

The full-match evidence does not justify promoting automatic over CPU for elapsed-time throughput on this host, or claiming that automatic consistently beats both forced modes. Retain the exact kernel and transfer improvements on their measured evidence, keep the CPU override available, and describe automatic placement as a heuristic with measured regret. Small-map losses are reported but are not the reason for this recommendation: the three large maps also show no aggregate automatic elapsed-time benefit.

Large-map process CPU falls by an equal-case geometric mean of 12.77% with forced GPU and 10.07% with automatic, while elapsed time stays similar or rises. For example, watershed gradient active time falls from 48.34s on CPU to 37.38s on GPU, but whole-match median runtime remains 38.23s versus 38.36s. This is evidence that isolated field/kernel throughput does not predict the game critical path. Preparation, transfers, calibration, other simulation work and overlap can affect placement cost; attributing a particular loss to a particular policy decision would require traces not collected by this campaign. Process CPU is not an energy measurement.

These are descriptive results on one machine. Two observations per cell do not establish a reliable confidence interval, and small differences should not be treated as proven wins. Negative regret means automatic was faster than both forced modes; the denominator is selected separately for runtime and full wall. All geometric means weight map cases equally.

## Per-map results

Seconds are median [minimum, maximum] across two observations. `run_ns` includes the 1,000 warmup ticks; histogram and benchmark CPU counters exclude those ticks. Full process wall includes initialization and final saving. No cold driver-cache reset was performed.

| Generator | Size | CPU runtime | OpenCL runtime | Automatic runtime | Auto regret run / wall |
|---|---:|---:|---:|---:|---:|
| watershed | 512 | 38.231 [37.623, 38.838] | 38.362 [37.950, 38.774] | 40.363 [39.499, 41.227] | +5.58% / +5.39% |
| bajada | 512 | 42.079 [41.209, 42.949] | 42.043 [41.399, 42.686] | 41.944 [41.865, 42.023] | -0.23% / -0.35% |
| contested-commons | 512 | 45.376 [42.531, 48.221] | 44.785 [43.380, 46.191] | 46.497 [43.129, 49.865] | +3.82% / +3.60% |
| islands | 256 | 25.679 [23.871, 27.488] | 26.355 [25.148, 27.562] | 24.813 [24.188, 25.439] | -3.37% / -2.83% |
| maze | 256 | 11.382 [11.224, 11.540] | 14.761 [14.473, 15.048] | 11.738 [11.477, 11.998] | +3.12% / +3.53% |
| canals | 256 | 16.157 [16.031, 16.283] | 17.567 [17.276, 17.859] | 16.626 [16.397, 16.855] | +2.91% / +3.35% |
| even-ground | 128 | 12.342 [11.236, 13.448] | 15.355 [14.230, 16.480] | 14.338 [12.966, 15.710] | +16.17% / +16.25% |
| swamp | 128 | 15.754 [15.049, 16.460] | 16.082 [16.078, 16.086] | 14.996 [14.489, 15.504] | -4.81% / -3.85% |

| Generator | CPU full wall | OpenCL full wall | Automatic full wall |
|---|---:|---:|---:|
| watershed | 41.760 [41.197, 42.324] | 41.985 [41.512, 42.457] | 44.011 [43.137, 44.885] |
| bajada | 46.490 [45.649, 47.331] | 46.648 [46.168, 47.129] | 46.325 [46.307, 46.342] |
| contested-commons | 48.079 [45.163, 50.994] | 47.615 [46.102, 49.128] | 49.330 [45.866, 52.795] |
| islands | 27.047 [25.271, 28.823] | 27.773 [26.541, 29.005] | 26.282 [25.693, 26.870] |
| maze | 12.639 [12.446, 12.832] | 16.089 [15.788, 16.389] | 13.085 [12.873, 13.296] |
| canals | 17.546 [17.415, 17.677] | 19.091 [18.774, 19.408] | 18.134 [17.881, 18.387] |
| even-ground | 12.917 [11.814, 14.019] | 16.030 [14.893, 17.166] | 15.016 [13.644, 16.387] |
| swamp | 16.512 [15.812, 17.212] | 16.930 [16.925, 16.935] | 15.877 [15.340, 16.414] |

## Paired-pass sensitivity

| Slice | Pass | Auto vs CPU runtime / wall | OpenCL vs CPU runtime / wall | Auto vs best forced runtime / wall |
|---|---:|---:|---:|---:|
| all | 1 | +2.10% / +2.45% | +9.09% / +8.93% | +2.13% / +2.45% |
| 512-priority | 1 | +1.57% / +1.64% | +0.76% / +1.18% | +1.62% / +1.64% |
| all | 2 | +3.02% / +3.28% | +6.59% / +6.57% | +3.95% / +4.03% |
| 512-priority | 2 | +3.50% / +3.37% | -1.34% / -1.13% | +5.21% / +4.81% |

Mode order rotates by scenario and pass; map order also changes on pass two. Both observations remain in every cell. No slow valid observation was removed. The raw rows include process CPU, tick p99, gradient activity/wait, clocks, temperature, and background inventory; aggregate activity counters overlap and cannot be added to wall time.

## Exactness and reproducibility

All 48 completed matches match original-CPU initial and final checksums, tick count, termination, and SHA256 of the compressed final save. The audit re-read every final save. All requested/resolved compute counts are eight, and every measured tick histogram contains 19,000 ticks. The seven gradient job/publication/discard/synchronous-work counters agree across modes/repetitions for each map. 0 timing samples were excluded in this final campaign; valid rows contain zero reserved-core interference events.

The CPU15 observer consumed 47.413s separately from 1366.415s summed game-process wall. Child process CPU accounting uses wait4 on the game PID and excludes observer CPU. These checks do not capture per-tick checksums or establish cross-platform determinism; those require the separate integration validation.

The eight generator types are even-ground, islands, maze, canals, watershed, bajada, swamp, and contested-commons. Original manifest and fixture hashes, game/map seeds, terrain coverage, original references, full commands, source snapshot, and final saves are retained. The cohort was not used to select map-specific thresholds or change seeds; the final epilogue candidate was screened on independent captured fields. Results apply to headless execution on this host/GPU, with a warm driver cache, not interactive rendering or other hardware.

Artifacts: `activation12-two-pass/metadata.json`, `activation12-two-pass/results.json`, `activation12-analysis.json`, `activation12-audit.json`, `source/activation12/manifest.json`, `activation12-plan.json`, `holdout-fixtures/prepared-manifest.json`, `holdout-fixtures/coverage.json`, and `activation12-two-pass-table.csv`. Earlier source01abc screen, including its three timing exclusions and environment strata, is documented separately in `holdout-screen-report.md`.
