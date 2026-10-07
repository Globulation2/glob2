# Resource growth: shared-snapshot cost attribution

This follow-up corrects the attribution in the original report. Its standalone component benchmark charged all snapshot capture to growth while the legacy control captured none. That measures adopting snapshots, not growth's incremental cost when AI/gradients already capture them. The original full-engine results remain valid observations; the standalone ratios do not explain their overhead.

## Controlled existing-capture experiment

All four variants now capture `SimulationSnapshot::All` once per observation. New growth consumes a projection of that existing handle. Same candidate binary, seed 713, initial fixture and 64 passes; one warm-up plus ten rotated measured repeats for 21 scenarios. Initial capture is warmed outside timing. Timed capture includes ongoing refresh. This benchmark runs no AI jobs and retains no artificial AI leases, so it isolates an existing capture service, not real AI contention. The old and new growth algorithms still follow different trajectories. All variants have the candidate's 16-byte resource cell, so this comparison excludes the old-to-new cell-size penalty.

| Size | Scenario | Legacy+capture ms | Immediate split ms | Delayed owner ms | Delayed shared ms | Immediate/legacy (95% CI) | Shared/legacy (95% CI) |
|---:|---|---:|---:|---:|---:|---|---|
| 128 | sparse | 0.48 | 0.62 | 1.37 | 1.28 | 1.237 (1.196–1.342) | 2.702 (2.234–3.215) |
| 128 | dense | 1.51 | 1.76 | 3.13 | 4.69 | 1.174 (1.095–1.270) | 2.929 (2.768–3.164) |
| 128 | saturated | 1.65 | 1.57 | 2.97 | 4.41 | 0.956 (0.938–0.978) | 2.802 (2.489–2.992) |
| 128 | harvested | 2.11 | 2.04 | 3.16 | 5.55 | 0.964 (0.918–1.054) | 2.685 (2.403–2.884) |
| 128 | blocked | 1.37 | 1.31 | 2.48 | 4.07 | 1.003 (0.876–1.010) | 2.938 (2.857–3.233) |
| 128 | multi | 2.43 | 2.50 | 4.73 | 6.37 | 1.035 (1.008–1.137) | 2.495 (2.448–2.632) |
| 128 | disabled | 0.07 | 0.07 | 0.07 | 0.06 | 0.993 (0.970–1.105) | 1.006 (0.933–1.066) |
| 256 | sparse | 1.97 | 2.08 | 6.09 | 4.73 | 1.070 (1.026–1.079) | 2.347 (2.226–2.476) |
| 256 | dense | 6.18 | 6.80 | 16.57 | 15.91 | 1.060 (1.039–1.081) | 2.666 (2.535–2.754) |
| 256 | saturated | 5.20 | 6.31 | 19.50 | 15.53 | 1.206 (1.162–1.236) | 3.072 (2.923–3.253) |
| 256 | harvested | 7.44 | 8.19 | 20.49 | 20.18 | 1.103 (1.076–1.131) | 2.856 (2.703–2.904) |
| 256 | blocked | 4.45 | 4.90 | 11.48 | 11.89 | 1.052 (1.031–1.103) | 2.734 (2.604–2.794) |
| 256 | multi | 14.76 | 16.07 | 39.21 | 28.39 | 1.075 (1.016–1.126) | 1.910 (1.637–2.010) |
| 256 | disabled | 0.08 | 0.08 | 0.08 | 0.08 | 0.985 (0.889–1.016) | 1.013 (0.948–1.039) |
| 512 | sparse | 10.04 | 16.65 | 39.09 | 19.57 | 1.618 (1.547–1.787) | 1.936 (1.850–2.190) |
| 512 | dense | 70.22 | 80.35 | 104.94 | 93.31 | 1.179 (1.041–1.304) | 1.311 (1.183–1.429) |
| 512 | saturated | 57.08 | 70.64 | 102.24 | 81.68 | 1.212 (1.093–1.294) | 1.397 (1.327–1.472) |
| 512 | harvested | 73.35 | 84.47 | 106.70 | 102.18 | 1.138 (1.084–1.285) | 1.361 (1.231–1.551) |
| 512 | blocked | 42.33 | 57.20 | 78.06 | 65.71 | 1.286 (1.270–1.407) | 1.531 (1.377–1.694) |
| 512 | multi | 134.33 | 163.28 | 198.05 | 151.54 | 1.224 (1.170–1.247) | 1.134 (1.085–1.181) |
| 512 | disabled | 0.08 | 0.08 | 0.08 | 0.08 | 0.982 (0.968–1.008) | 1.002 (0.880–1.019) |

Enabled immediate-split paired median ratios span 0.956–1.618, rather than the earlier standalone 1.69–6.57. Most are approximately 1.00–1.29; sparse 512² is the 1.62 outlier. Disabled cases are a bookkeeping control. These runs occurred at a different host load from the earlier campaign: compare variants within this campaign, not their absolute milliseconds against earlier runs.

## Where the time goes with existing capture

Each entry below is a median over ten measurements, milliseconds per 64 passes. Medians do not necessarily sum. Compute overlaps capture in shared execution; joins overlap compute and must not be added. Timers measure elapsed wall time, including possible descheduling, not per-stage CPU time.

| Scenario | Variant | Total ms | Capture ms | Compute ms | Publish ms | Join ms | Tracked copy MiB | Snapshot peak MiB |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 256/dense | legacy | 6.18 | 3.60 | 0.00 | 0.00 | 0.00 | 30.54 | 5.27 |
| 256/dense | immediate | 6.80 | 4.05 | 1.56 | 1.09 | 0.00 | 34.90 | 5.27 |
| 256/dense | delayed owner | 16.57 | 11.94 | 3.40 | 1.13 | 3.43 | 50.68 | 11.27 |
| 256/dense | delayed shared | 15.91 | 14.29 | 5.26 | 1.15 | 0.21 | 32.41 | 6.27 |
| 512/multi | legacy | 134.33 | 104.95 | 0.00 | 0.00 | 0.00 | 142.09 | 20.08 |
| 512/multi | immediate | 163.28 | 127.50 | 20.92 | 14.68 | 0.00 | 177.18 | 20.08 |
| 512/multi | delayed owner | 198.05 | 159.50 | 24.98 | 12.95 | 25.09 | 258.19 | 44.08 |
| 512/multi | delayed shared | 151.54 | 136.92 | 26.81 | 12.59 | 0.86 | 159.38 | 24.08 |
| 512/saturated | legacy | 57.08 | 39.03 | 0.00 | 0.00 | 0.00 | 90.93 | 20.08 |
| 512/saturated | immediate | 70.64 | 50.20 | 14.13 | 6.04 | 0.00 | 111.68 | 20.08 |
| 512/saturated | delayed owner | 102.24 | 75.69 | 20.61 | 5.37 | 20.68 | 192.49 | 44.08 |
| 512/saturated | delayed shared | 81.68 | 75.05 | 19.08 | 5.40 | 0.61 | 100.10 | 24.08 |
| 512/sparse | legacy | 10.04 | 6.02 | 0.00 | 0.00 | 0.00 | 12.08 | 20.08 |
| 512/sparse | immediate | 16.65 | 8.57 | 7.52 | 0.57 | 0.00 | 17.68 | 20.08 |
| 512/sparse | delayed owner | 39.09 | 28.10 | 10.42 | 0.46 | 10.46 | 47.89 | 40.08 |
| 512/sparse | delayed shared | 19.57 | 18.23 | 10.97 | 0.43 | 0.56 | 23.68 | 28.08 |

For dense 256², legacy non-capture work is approximately 2.58 ms; immediate computation plus publication is approximately 2.65 ms. Capture itself rises from 3.60 to 4.05 ms. For multi-material 512², legacy non-capture work is approximately 29.38 ms, versus 35.60 ms compute plus publication; capture rises from 104.95 to 127.51 ms. Subtraction of independent medians is illustrative, not an exact additive accounting.

Delayed owner execution retains snapshots until its deadline computation; buffers therefore accumulate more changed chunks before reuse. In multi-material 512², tracked copying rises from 177.18 MiB immediate to 258.19 MiB delayed owner, and snapshot peak from 20.08 to 44.08 MiB. Shared execution releases leases earlier and reduces both (see table). However, small jobs and concurrent capture/compute can still cost more elapsed time; the present counters cannot partition that remainder into executor synchronization, cache/memory contention and host scheduling.

## Actual engine: incremental shared capture

Reanalysis of the previously published final-engine campaigns, holding AI/gradient flags, executor size 4, starting save and 256 ticks fixed; growth delay 8. Every baseline and candidate scenario reports 257 captures. Values are baseline/shared medians; deltas and confidence intervals are paired by repeat. Trajectory changes prevent a perfectly causal attribution to growth alone.

| Campaign/scenario | Capture old/new ms | Paired capture delta ms (95% CI) | Tracked copy old/new MiB | Paired simulation CPU delta ms |
|---|---|---|---|---:|
| engine-default-final/ai128 | 21.07 / 26.87 | +6.20 (-0.24–+8.44) | 19.40 / 23.95 | +8.43 |
| engine-default-final/ai256 | 91.61 / 134.00 | +20.38 (+5.67–+39.56) | 70.08 / 82.84 | +27.25 |
| engine-default-final/ai512 | 309.52 / 352.05 | +35.82 (-17.28–+72.73) | 232.36 / 272.69 | +28.15 |
| engine-default-final/disabled512 | 99.81 / 104.32 | -5.76 (-14.81–+3.79) | 120.15 / 122.15 | +21.50 |
| engine-default-final/idle128 | 25.95 / 31.83 | +8.89 (-0.28–+12.26) | 18.16 / 21.88 | +4.22 |
| engine-default-final/idle256 | 79.54 / 104.94 | +25.22 (+13.08–+38.04) | 54.10 / 65.45 | +69.02 |
| engine-default-final/idle512 | 247.19 / 315.22 | +46.00 (+23.71–+84.64) | 169.97 / 221.20 | +39.61 |
| engine-controlled/blocked | 30.50 / 41.23 | +11.50 (+6.82–+15.71) | 24.19 / 31.69 | +17.48 |
| engine-controlled/dense | 197.86 / 228.67 | +39.29 (+25.44–+54.76) | 149.34 / 189.75 | +89.12 |
| engine-controlled/disabled | 25.31 / 25.09 | +0.06 (-4.76–+1.62) | 16.37 / 16.88 | -16.32 |
| engine-controlled/harvested | 197.81 / 242.28 | +28.12 (+11.85–+88.58) | 157.10 / 190.42 | +30.99 |
| engine-controlled/multi | 860.81 / 1052.31 | +121.72 (+67.00–+189.11) | 794.64 / 962.75 | +135.67 |
| engine-controlled/saturated | 709.32 / 875.04 | +158.67 (+111.96–+226.30) | 567.82 / 754.98 | +292.84 |
| engine-controlled/sparse | 19.48 / 19.58 | -0.08 (-2.55–+2.40) | 12.81 / 12.58 | -7.52 |

CPU here uses `benchmark_run_cpu_ns`, which isolates the simulation run across threads, rather than whole-process CPU including setup/save. Stage wall-time deltas cannot be subtracted from CPU deltas as an additive breakdown. The disabled control illustrates host variance.

## What is established, and what remains unmeasured

- Growth shares the existing capture. No additional per-tick capture occurred in these engine runs. Static components can be reused; they are not blindly copied every tick.
- ResourceCell grew from 12 to 16 bytes for the deposit incarnation: 33% more resource-cell bytes for the same copied cells. All existing resource snapshot consumers pay that layout cost. This does not imply 33% more total capture time or prove how much of the observed delta it caused.
- Resource dirty tracking copies 16×16-cell chunks. Different growth/stock updates and older reusable buffers change copied volume. Owner-delayed retention visibly increases volume in the controlled benchmark; actual AI/gradient leases can already retain those buffers, reducing the incremental effect.
- Multi-material stock sidecars are copied in full when Resources refreshes. Inspection also found that the existing bytesCopied counter excludes this sidecar memcpy. All byte tables therefore report tracked copying, not total memory traffic. Capture time includes the sidecar work. This accounting gap existed before this change.
- Compute/proposal emission and publication have measured costs, but legacy growth already performed ecology and mutation work. Their entire cost is not overhead. In immediate dense cases the compute-plus-publish cost is close to legacy non-capture work.
- Whole-engine deadline/final-drain joins in dense/multi/saturated shared cases are only about 0.05–0.08 ms per 256 ticks. Large queue-residence counters measure elapsed waiting while other work proceeds, not CPU usage or equivalent owner stalls.
- Separating incarnation storage from frequently copied stock cells is a concrete optimization candidate. Improving buffer reuse and copy granularity is another. Neither has been implemented or credited with a predicted speedup.
- A definitive fine-grained CPU attribution still needs per-component CPU profiling and same-trajectory controls. This experiment does not establish an end-to-end win, nor that the snapshot architecture is inherently unviable.

## Reproduction

From the implementation checkout, with its recorded release dependencies:

```sh
GLOB2_GROWTH_EXISTING_CAPTURE=1 GLOB2_GROWTH_BENCHMARK_OUTPUT=$PWD/artifacts/resource-growth/attribution/existing-capture.json LD_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib python3 test/run_tests.py --binary engine --filter "ResourceGrowthBenchmark/paired*" --tag benchmark --no-display -j1 --timeout 1800 --junit artifacts/resource-growth/attribution/existing-capture.xml --artifacts artifacts/resource-growth/attribution/test
```

`engine-breakdown.json` retains all engine metrics/deltas. `existing-capture.json` retains all 924 controlled samples. The two summarizer scripts accompany this report. This update changes only benchmark tooling and documentation; no production simulation behavior changes.

Current master was fetched at `4cb2ac058`. It has advanced simulation goldens since the original validation; the recorded merge-tree check now has conflicts. This attribution experiment intentionally preserves the measured production revision. The draft needs a separate integration/golden refresh before merge; the earlier conflict-free statement is historical.
