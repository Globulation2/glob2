# Resource growth measurements

All ratios below are candidate/control elapsed time: greater than 1 means slower. Component rows cover 64 growth passes; engine rows cover 256 ticks. One warm-up and ten rotated paired measurements. Bootstrap intervals describe these runs on this shared Linux host; they do not establish platform-independent performance.

## Component measurements

| Size | Scenario | Legacy ms | Immediate split ms | Delayed owner ms | Delayed shared ms | Shared/legacy ratio (95% CI) | Shared/owner ratio (95% CI) |
|---:|---|---:|---:|---:|---:|---|---|
| 128 | sparse | 0.281 | 1.077 | 3.799 | 2.146 | 7.61 (5.70–16.76) | 0.64 (0.41–0.82) |
| 128 | dense | 0.889 | 3.052 | 13.275 | 5.309 | 5.75 (5.14–9.57) | 0.41 (0.33–0.60) |
| 128 | saturated | 1.595 | 3.136 | 13.995 | 6.462 | 3.42 (2.92–4.79) | 0.43 (0.29–0.55) |
| 128 | harvested | 1.623 | 5.767 | 12.882 | 8.238 | 5.13 (3.68–5.87) | 0.59 (0.48–0.77) |
| 128 | blocked | 0.826 | 2.447 | 8.606 | 5.116 | 5.94 (5.49–7.06) | 0.54 (0.51–0.66) |
| 128 | multi | 1.937 | 7.241 | 21.717 | 9.689 | 4.89 (3.20–7.08) | 0.43 (0.39–0.55) |
| 128 | disabled | 0.001 | 0.002 | 0.003 | 0.003 | 3.67 (2.36–5.65) | 0.99 (0.77–1.08) |
| 256 | sparse | 1.843 | 8.125 | 17.549 | 8.326 | 4.17 (3.33–6.25) | 0.48 (0.40–0.65) |
| 256 | dense | 10.710 | 50.459 | 70.343 | 43.050 | 3.79 (2.56–4.36) | 0.58 (0.55–0.67) |
| 256 | saturated | 10.195 | 41.617 | 69.480 | 36.916 | 3.43 (3.03–3.99) | 0.53 (0.49–0.59) |
| 256 | harvested | 8.236 | 55.100 | 70.699 | 53.119 | 6.81 (5.04–7.80) | 0.72 (0.69–0.82) |
| 256 | blocked | 8.778 | 35.123 | 50.582 | 31.386 | 3.76 (3.06–4.51) | 0.65 (0.48–0.72) |
| 256 | multi | 13.243 | 80.897 | 104.489 | 68.672 | 5.59 (4.42–6.07) | 0.65 (0.59–0.72) |
| 256 | disabled | 0.001 | 0.002 | 0.003 | 0.004 | 2.44 (1.92–3.14) | 1.19 (0.94–1.50) |
| 512 | sparse | 26.456 | 45.151 | 90.962 | 36.774 | 1.41 (1.32–1.64) | 0.41 (0.37–0.43) |
| 512 | dense | 61.121 | 225.094 | 302.168 | 193.512 | 3.19 (2.78–3.51) | 0.64 (0.60–0.68) |
| 512 | saturated | 56.795 | 200.427 | 297.518 | 150.867 | 2.56 (1.98–2.92) | 0.50 (0.41–0.58) |
| 512 | harvested | 63.508 | 256.286 | 312.533 | 212.189 | 3.42 (2.64–3.61) | 0.73 (0.57–0.76) |
| 512 | blocked | 46.718 | 150.627 | 201.159 | 126.051 | 2.75 (2.51–3.02) | 0.63 (0.60–0.69) |
| 512 | multi | 59.897 | 344.665 | 440.781 | 271.090 | 4.58 (4.06–4.76) | 0.65 (0.57–0.72) |
| 512 | disabled | 0.002 | 0.002 | 0.004 | 0.004 | 1.71 (1.48–1.92) | 1.04 (0.89–1.15) |

The legacy component path runs the retained immediate algorithm in the candidate binary; the engine comparison below uses the actual preserved master executable. New/old trajectories intentionally differ. All attempts in the new kernel read one immutable snapshot, and delayed cases leave the final eight batches un-published after draining computation. Disabled-growth ratios are dominated by sub-microsecond bookkeeping and should not be interpreted as useful speed changes.

## Ecology: 20 paired seeds, 512 ticks

This controlled 128² uniform-crop fixture harvests every eight ticks, either preserving a one-unit reserve or allowing removal. It measures replenishment, spread, depletion and sustained harvest; full-match balance still requires play assessment. Both algorithms run in the candidate executable.

| Measure | Legacy mean | Delayed mean | Median paired ratio (95% CI) |
|---|---:|---:|---|
| reserve/food | 7947.85 | 7642.80 | 0.9642 (0.9464–0.9828) |
| reserve/deposits | 5799.15 | 5720.45 | 0.9848 (0.9786–0.9951) |
| reserve/harvested | 16261.00 | 15999.30 | 0.9848 (0.9754–0.9942) |
| reserve/depleted | 0.00 | 0.00 | n/a (zero baseline) |
| reserve/seeded | 1703.15 | 1624.45 | 0.9482 (0.9287–0.9830) |
| reserve/replenished | 12261.00 | 11772.95 | 0.9593 (0.9433–0.9773) |
| deplete/food | 244.20 | 203.25 | 0.8847 (0.6741–0.9967) |
| deplete/deposits | 90.55 | 77.05 | 0.9382 (0.7015–1.0421) |
| deplete/harvested | 10477.45 | 10351.95 | 0.9890 (0.9849–0.9922) |
| deplete/depleted | 4114.40 | 4112.40 | 0.9991 (0.9982–1.0012) |
| deplete/seeded | 108.95 | 93.45 | 0.9209 (0.6942–1.1415) |
| deplete/replenished | 368.00 | 217.05 | 0.5664 (0.4508–0.7344) |

## Whole engine: default delay 8, executor size 4

Every other delay/thread combination is retained in `engine-timing/summary.json`. Owner placement uses the same executor size as shared placement, keeping AI/gradient settings fixed.

| Scenario | Legacy run ms | Delayed owner run ms | Shared run ms | Shared/legacy ratio (95% CI), delta ms | Shared/owner ratio (95% CI), delta ms |
|---|---:|---:|---:|---|---|
| ai128 | 33.70 | 45.28 | 40.79 | 1.187 (1.011–1.348), +5.43 | 0.842 (0.715–1.098), -7.15 |
| ai256 | 204.62 | 231.72 | 218.08 | 1.033 (0.936–1.267), +7.06 | 0.956 (0.841–1.111), -10.26 |
| ai512 | 555.10 | 628.94 | 584.33 | 1.117 (1.044–1.243), +58.06 | 0.937 (0.836–1.069), -42.90 |
| disabled512 | 817.27 | 826.01 | 861.93 | 1.093 (0.839–1.574), +68.99 | 1.203 (0.979–1.305), +146.89 |
| idle128 | 55.05 | 63.19 | 55.32 | 1.074 (0.942–1.218), +3.84 | 0.951 (0.780–1.062), -2.97 |
| idle256 | 200.63 | 189.15 | 186.03 | 0.993 (0.879–1.076), -1.37 | 0.932 (0.849–1.141), -13.72 |
| idle512 | 432.73 | 454.67 | 424.64 | 1.018 (0.928–1.130), +7.56 | 0.933 (0.711–0.971), -29.90 |

`measurements.jsonl` retains process wall/CPU/RSS, exact command lines, input/binary hashes, load averages, full snapshot/AI/gradient metrics, growth counters, queue/compute/publication/join times, pending-buffer peaks, and candidate tick percentiles. The original executable supplies a tick histogram, not exact percentiles. Growth wait time combines publication waits and final draining; it is not a pure deadline-stall counter.


## Refreshed default timings on final integration revision

The full matrix above measured the feature before the master abort-session fix. This separate campaign remeasures delay 8/thread count 4 on the final integrated executable. The fix changes the failed-session path; all fourteen final integration traces match the earlier feature revision. Both campaigns are retained.

| Scenario | Legacy run ms | Owner run ms | Shared run ms | Shared/legacy ratio (95% CI), delta ms | Shared/owner ratio (95% CI), delta ms |
|---|---:|---:|---:|---|---|
| ai128 | 103.33 | 119.42 | 106.45 | 1.034 (0.817–1.255), +3.40 | 0.897 (0.770–0.958), -12.36 |
| ai256 | 330.88 | 363.60 | 433.50 | 1.128 (0.972–1.190), +35.82 | 0.978 (0.799–1.055), -5.49 |
| ai512 | 1033.97 | 1077.44 | 992.47 | 0.916 (0.807–1.016), -91.83 | 0.882 (0.820–1.010), -121.11 |
| disabled512 | 808.13 | 845.01 | 784.99 | 1.000 (0.888–1.106), -0.56 | 1.001 (0.862–1.110), +4.77 |
| idle128 | 103.43 | 112.41 | 106.71 | 0.988 (0.815–1.304), -1.21 | 1.024 (0.789–1.345), +3.06 |
| idle256 | 229.24 | 272.89 | 253.49 | 1.026 (1.002–1.177), +5.42 | 0.988 (0.917–1.106), -3.16 |
| idle512 | 707.42 | 750.36 | 709.49 | 0.990 (0.928–1.106), -6.95 | 0.942 (0.856–1.000), -44.17 |

| Scenario | Shared process wall ms | Process CPU ms | Peak RSS MiB | Tick median / p95 / p99 µs | Compute / queue / joins / publication ms |
|---|---:|---:|---:|---|---|
| ai128 | 543.88 | 600.70 | 61.41 | 209.3 / 1611.9 / 3366.9 | 7.10 / 264.83 / 0.14 / 0.65 |
| ai256 | 943.37 | 1051.75 | 73.17 | 360.3 / 7293.9 / 10975.9 | 29.21 / 1413.94 / 0.41 / 1.86 |
| ai512 | 1637.93 | 2861.29 | 169.91 | 1213.8 / 14039.4 / 38050.4 | 132.50 / 2911.93 / 0.09 / 7.83 |
| disabled512 | 1419.86 | 2149.07 | 136.25 | 287.7 / 12037.9 / 36931.5 | 0.00 / 0.00 / 0.00 / 0.00 |
| idle128 | 580.96 | 670.67 | 61.41 | 175.7 / 1802.7 / 3044.9 | 8.24 / 427.03 / 0.53 / 0.65 |
| idle256 | 749.42 | 1185.57 | 70.39 | 364.7 / 4038.9 / 6217.2 | 32.52 / 891.84 / 0.12 / 2.12 |
| idle512 | 1324.41 | 2916.14 | 153.10 | 995.1 / 10147.2 / 17181.4 | 113.68 / 2436.90 / 0.08 / 5.82 |

The disabled-growth owner/shared comparison submits no growth jobs. Its variability is a negative control for host/scheduling noise; small apparent gains or regressions elsewhere cannot be treated as definitive. No overall end-to-end speedup is established. Shared placement remains the requested default.

Snapshot copying, proposal retention and publication add substantial standalone cost. Shared execution reduces some delayed-owner costs, but does not produce a consistent engine win over the original algorithm in these workloads. A quiet, isolated run is still needed to qualify small engine differences.

## Controlled full-engine scenarios

These additional starting saves were emitted by the fixture generator compiled against the baseline. They preserve buildings/teams but replace the field with uniform crops: sparse/blocked 128², dense/low-stock active-AI/disabled 256², saturated/multi-material 512². Each comparison uses delay 8, four executor slots, one warm-up and ten paired measured runs of 256 ticks. Fourteen separate owner/shared checksum runs passed. The low-stock case retains active Nicowar/Warrush controllers; other cases retain idle controllers.

| Scenario | Legacy run ms | Owner run ms | Shared run ms | Shared/legacy ratio (95% CI), delta ms | Shared/owner ratio (95% CI), delta ms |
|---|---:|---:|---:|---|---|
| blocked | 91.69 | 82.75 | 92.69 | 1.071 (0.781–1.898), +4.43 | 1.236 (0.876–1.644), +11.96 |
| dense | 336.04 | 405.91 | 312.57 | 0.952 (0.888–1.075), -16.71 | 0.836 (0.726–0.885), -66.15 |
| disabled | 149.38 | 141.42 | 139.30 | 0.957 (0.840–1.069), -5.88 | 0.972 (0.947–1.035), -3.90 |
| harvested | 411.98 | 574.07 | 431.11 | 0.945 (0.843–1.327), -21.01 | 0.693 (0.610–0.949), -182.99 |
| multi | 1129.71 | 1455.82 | 1248.13 | 1.022 (0.942–1.124), +24.87 | 0.833 (0.759–0.915), -243.90 |
| saturated | 1063.76 | 1349.51 | 1081.69 | 0.984 (0.942–1.090), -16.18 | 0.787 (0.726–0.822), -272.61 |
| sparse | 96.22 | 85.37 | 96.48 | 1.095 (0.773–1.128), +8.35 | 1.054 (0.740–1.231), +4.42 |

All controlled shared/legacy intervals include parity. Dense, low-stock active-AI, multi-material and saturated fields show lower shared costs than delayed owner execution, but that does not establish an improvement over legacy growth. This directly illustrates the split overhead consuming the recovered parallel benefit in these runs.
