# Final rendering versus pre-change master: measured performance

Baseline **a60018aa0066d1f4dc5e733cb86d0085b80bd604**; final merged revision **4c3f6074ff1fd1c5f91dc9a7e1fac448d274d98e**. Both include the preceding snapshot-gradient optimization. The comparison spans the rendering stack plus an unrelated browser shell change and native-equivalent wasm32 compile fix.

**The large-map rendering-active workload regressed:** across six pairs, CPU/tick increased 27.6%, TPS decreased 15.6%, and p95 tick-completion latency increased 10.1% (median paired changes). Every large-map pair regressed on all three metrics. Median RSS increased about 20 MiB. Small-map effects were much smaller. The client/render-disabled large-map control did not show this regression; profiling is needed to identify its cause. These results do not validate the intended whole-engine rendering speedup.

## Primary results: four reserved physical cores

The initial shared-host campaign was too noisy for a useful wall-time conclusion. We then ran a separate, explicitly retained campaign using the repository's audited cgroup CPU partition wrapper: engine affinity 12–15, reserved 12–15 and SMT siblings 28–31; other workloads retained the other twelve physical cores. No existing process affinity or cgroup controls were edited. The audit confirms partition validity and restoration. Frequency governors were not changed; shared memory bandwidth, caches, package power and kernel activity remain potential sources of variation. Xvfb/controller launcher ran on other CPUs, while the lightweight Python child controller shared the reserved set.

Same binaries, fixtures, settings and timing boundaries as the initial campaign: native Linux/GCC15.2 release, Threadripper2950X, software rendering via Mesa llvmpipe/Xvfb at 800×600 and a 60FPS target. Four compute participants including owner. Two four-AI spectator saves, fixed mouse-motion input, no gameplay orders. 20 static graphics warmup frames, 500 simulation warmup ticks, then 2000 measured ticks per fresh process. Six counterbalanced pairs per map; separate four-pair client/render-disabled controls. This is not a physical-GPU benchmark or a dense late-game workload.

Table entries are **baseline → final**, medians of six run-level values. CPU cost sums all engine-process threads per simulated tick. RSS includes the whole process. Completion p95 includes owner work, parking and scheduling gaps; owner p95 excludes gaps between iterations. Median paired changes are reported separately because they need not equal the ratio of independent medians.

| Metric | Balanced 128×128 | Large 512×512 |
|---|---:|---:|
| Total CPU ms/tick | 1.286 → 1.347 | 8.189 → 10.346 |
| Simulation-owner CPU ms/tick | 0.570 → 0.581 | 3.305 → 4.003 |
| Median process RSS MiB | 1195.0 → 1196.1 | 1608.0 → 1628.3 |
| Peak process RSS MiB (includes setup) | 1207.0 → 1209.1 | 1633.1 → 1668.9 |
| Ticks/second | 1657.6 → 1628.0 | 259.1 → 219.4 |
| Simulation work p50 ms | 0.292 → 0.311 | 1.456 → 2.513 |
| Simulation work p95 ms | 2.354 → 2.317 | 12.125 → 13.461 |
| Owner iteration p50 ms | 0.299 → 0.316 | 2.012 → 2.710 |
| Owner iteration p95 ms | 2.421 → 2.351 | 12.879 → 14.174 |
| Completion interval p50 ms | 0.299 → 0.316 | 2.013 → 2.710 |
| Completion interval p95 ms | 2.430 → 2.351 | 12.885 → 14.174 |

Paired percentage changes: median [observed min, max]; these are not confidence intervals.

| Metric | Balanced 128×128 | Large 512×512 |
|---|---:|---:|
| CPU per tick | +5.3% [+0.7, +7.8] | +27.6% [+21.4, +31.7] |
| Owner CPU per tick | +3.6% [-3.5, +5.6] | +19.3% [+13.9, +25.9] |
| RSS | +0.1% [-0.6, +0.7] | +1.4% [-0.8, +3.6] |
| TPS | -2.3% [-3.8, +2.5] | -15.6% [-17.4, -12.1] |
| Owner iteration p95 | -4.0% [-5.8, +3.9] | +10.4% [+6.6, +12.6] |
| Completion interval p95 | -4.0% [-6.6, +3.9] | +10.1% [+6.1, +12.1] |

## Four-pair controls

These suppress client servicing/input and drawing after graphics warmup, not the headless CLI. CPU differences should be interpreted as a diagnostic comparison, not an attribution to one function.

| Map | CPU ms/tick baseline → final | Paired CPU change | TPS baseline → final | Paired TPS change |
|---|---:|---:|---:|---:|
| balanced | 0.787 → 0.802 | +2.0% | 1876.009 → 1840.331 | -2.0% |
| large512 | 5.713 → 5.565 | -2.6% | 306.996 → 319.270 | +4.4% |

## Validation, scope and raw evidence

All 40 reserved-core runs finished successfully and reached identical initial/final heavy state checksums, tick counts and entity counts to the original campaign. Six separate baseline/final per-tick verification pairs from the original campaign match at all 2500 ticks for both maps and 2/4/8 CPU allocations. Verification overhead is excluded from reported timings. No production code changed during this task. These are performance harness checks, not a new full correctness suite.

All timing definitions, instrumentation patches, dependency versions, source/executable hashes, fixture saves, exact per-run commands/environment, build logs, limits and reproduction details are in [the full methodology and initial shared-host report](shared-host-report.md). That initial campaign is retained in full and must not be pooled with this reserved-core campaign. Its 2/4/8-CPU results had large wall-time variation; large-map aggregate CPU rose consistently in all eighteen pairs. Physical-GPU, browser, Windows, turn/network and dense late-game performance remain unmeasured.

Reserved-core data: [summary](isolated/summary.json), [per-run table](isolated/all-results.csv), [raw logs and tick timings](isolated/raw-runs.tar.gz), [CPU partition audit](cpuset-isolated.json). CPU/tick includes differing frame counts when runs take different wall time; it excludes Xvfb's separate process and setup/teardown. RSS samples every ~20ms include graphics/assets/allocator retention, not just snapshots. Phase lengths are short for this faster reserved-core campaign; repeated fixed tick windows give a reproducible state interval, not a long steady workload. No root cause is asserted without profiling.

To reproduce the reserved campaign, use the same patched builds and fixture layout, then run the included run_isolated.py through the included cpuset wrapper, adjusting topology-specific IDs only after checking SMT siblings:

```sh
taskset -c 0-7,16-23 xvfb-run -a -s '-screen 0 1024x768x24 -noreset' \
  python3 run_with_benchmark_cpuset.py --audit cpuset-isolated.json \
  --cpus 12-15 --reserve-cpus 12-15,28-31 --timeout-seconds 1800 -- \
  python3 run_isolated.py
```

Run directories must be fresh; the runner refuses existing directories. Copy analyze.py into isolated/ and run it there after completion. The complete original wrapper command is recorded in isolated.log / cpuset-isolated.json and this report. SHA256SUMS covers both campaigns.
