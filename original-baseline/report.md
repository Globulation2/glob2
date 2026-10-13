# CPU backend before/after measurements

Positive changes mean more CPU/time. Linux x86_64 AMD Ryzen 7 6800H, release GCC13 -O3; CPU automatic; four frozen fixtures; counts 1/2/4/8; eight alternating triples per cell; 256 warmup + 2048 measured ticks. Process CPU includes all threads and final drain. Other workloads active: load observations ranged from 6.3 to 41.5. Wall times especially are contention-sensitive. This is a headless CPU comparison, not graphical or cross-platform qualification.

Before = original 8862b0930. Control = pre-merge master 2d5cf67e8. After = final tested PR head 3f4a275d6 (tree merged as c7d688e9c). Control/after isolates feature changes from intervening master updates.

48 untimed runs matched per-tick checksums across revisions and counts. All 384 timed runs matched final checksums and recorded CPU-only execution. No production code was edited for measurement. Protocol, binary hashes, runtime library inventories, fixtures, exact commands, results and logs are retained beside this report.

The main CPU window excludes setup and warmup, includes final drain, and uses process CPU time across all threads. Seed/solve counters below cover the full 2304-tick simulation, including warmup, and are normalized by completed periodic fields. They exclude building/AI traversals and are supplementary diagnostics; they do not establish a causal breakdown of the total regression.

## before versus after

| Metric | Cohort change | Paired 95% CI |
|---|---:|---|
| cpu_per_tick_ms | +2.52% | [+0.24, +4.79]% |
| startup_inclusive_cpu_seconds | +1.94% | [-0.06, +3.94]% |
| setup_cpu_seconds | +0.07% | [-2.42, +2.55]% |
| run_wall_seconds | +3.73% | [+0.27, +7.20]% |
| tick_p95_ms | +3.58% | [-1.28, +8.43]% |
| periodic_seed_cpu_us_per_field | +12.04% | [+8.65, +15.42]% |
| periodic_solve_cpu_us_per_field | +1.28% | [-0.72, +3.27]% |

Fixture aggregates give each compute count equal weight; confidence intervals use eight pair-level averages.

| Fixture | Before CPU ms/tick | After CPU ms/tick | Change | Paired 95% CI |
|---|---:|---:|---:|---|
| 128-early | 0.4497 | 0.4697 | +4.56% | [-0.05, +9.18]% |
| 128-established | 0.5443 | 0.5481 | +1.06% | [-1.48, +3.60]% |
| 256-early | 0.9375 | 0.9537 | +1.47% | [-1.12, +4.07]% |
| 512-early | 3.4280 | 3.5312 | +2.97% | [-0.55, +6.50]% |

Individual compute counts:

| Fixture | Compute count | Before CPU ms/tick | After CPU ms/tick | Change | Paired 95% CI |
|---|---:|---:|---:|---:|---|
| 128-early | 1 | 0.3814 | 0.3806 | +0.26% | [-8.61, +9.14]% |
| 128-early | 2 | 0.3995 | 0.4329 | +8.29% | [-2.26, +18.85]% |
| 128-early | 4 | 0.4407 | 0.4635 | +5.43% | [+1.69, +9.16]% |
| 128-early | 8 | 0.5772 | 0.6019 | +4.26% | [-0.78, +9.30]% |
| 128-established | 1 | 0.4318 | 0.4502 | +4.51% | [-4.27, +13.29]% |
| 128-established | 2 | 0.4862 | 0.4790 | -1.01% | [-5.78, +3.75]% |
| 128-established | 4 | 0.5539 | 0.5704 | +2.82% | [+0.08, +5.57]% |
| 128-established | 8 | 0.7052 | 0.6926 | -2.09% | [-5.57, +1.40]% |
| 256-early | 1 | 0.8147 | 0.8056 | -1.15% | [-7.72, +5.42]% |
| 256-early | 2 | 0.9120 | 0.8920 | -2.29% | [-9.31, +4.73]% |
| 256-early | 4 | 0.9383 | 1.0097 | +7.58% | [+4.79, +10.37]% |
| 256-early | 8 | 1.0852 | 1.1076 | +1.76% | [-1.95, +5.46]% |
| 512-early | 1 | 2.9521 | 3.1491 | +7.05% | [+1.85, +12.25]% |
| 512-early | 2 | 3.1896 | 3.2530 | +1.79% | [-2.28, +5.86]% |
| 512-early | 4 | 3.6625 | 3.6555 | -0.58% | [-9.60, +8.45]% |
| 512-early | 8 | 3.9078 | 4.0671 | +3.63% | [-0.94, +8.21]% |

## control versus after

| Metric | Cohort change | Paired 95% CI |
|---|---:|---|
| cpu_per_tick_ms | +4.30% | [+2.12, +6.47]% |
| startup_inclusive_cpu_seconds | +3.68% | [+1.39, +5.96]% |
| setup_cpu_seconds | +1.35% | [-1.75, +4.44]% |
| run_wall_seconds | +3.91% | [+0.56, +7.25]% |
| tick_p95_ms | +5.93% | [-1.43, +13.29]% |
| periodic_seed_cpu_us_per_field | +17.31% | [+13.34, +21.27]% |
| periodic_solve_cpu_us_per_field | +5.29% | [+2.76, +7.82]% |

Fixture aggregates give each compute count equal weight; confidence intervals use eight pair-level averages.

| Fixture | Before CPU ms/tick | After CPU ms/tick | Change | Paired 95% CI |
|---|---:|---:|---:|---|
| 128-early | 0.4464 | 0.4697 | +5.06% | [+1.71, +8.41]% |
| 128-established | 0.5198 | 0.5481 | +5.29% | [+2.33, +8.25]% |
| 256-early | 0.9227 | 0.9537 | +3.20% | [-1.50, +7.90]% |
| 512-early | 3.4052 | 3.5312 | +3.63% | [+1.35, +5.90]% |

Individual compute counts:

| Fixture | Compute count | Before CPU ms/tick | After CPU ms/tick | Change | Paired 95% CI |
|---|---:|---:|---:|---:|---|
| 128-early | 1 | 0.3739 | 0.3806 | +1.75% | [-5.58, +9.08]% |
| 128-early | 2 | 0.3975 | 0.4329 | +8.59% | [-0.18, +17.36]% |
| 128-early | 4 | 0.4460 | 0.4635 | +3.92% | [-0.94, +8.78]% |
| 128-early | 8 | 0.5681 | 0.6019 | +5.99% | [+0.34, +11.63]% |
| 128-established | 1 | 0.4381 | 0.4502 | +2.02% | [-4.85, +8.88]% |
| 128-established | 2 | 0.4716 | 0.4790 | +2.23% | [-4.13, +8.59]% |
| 128-established | 4 | 0.5152 | 0.5704 | +10.84% | [+4.66, +17.02]% |
| 128-established | 8 | 0.6545 | 0.6926 | +6.08% | [+2.94, +9.21]% |
| 256-early | 1 | 0.8328 | 0.8056 | -3.02% | [-14.79, +8.74]% |
| 256-early | 2 | 0.8810 | 0.8920 | +1.49% | [-5.99, +8.97]% |
| 256-early | 4 | 0.9172 | 1.0097 | +10.12% | [+6.90, +13.34]% |
| 256-early | 8 | 1.0598 | 1.1076 | +4.22% | [+0.29, +8.16]% |
| 512-early | 1 | 2.9693 | 3.1491 | +6.40% | [+2.03, +10.77]% |
| 512-early | 2 | 3.2021 | 3.2530 | +1.35% | [-2.79, +5.50]% |
| 512-early | 4 | 3.5315 | 3.6555 | +3.25% | [-0.85, +7.36]% |
| 512-early | 8 | 3.9177 | 4.0671 | +3.50% | [+0.72, +6.28]% |

