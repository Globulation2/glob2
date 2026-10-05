# Integrated cache measurements

Control: current direct kernel with cache dispatch and mutation notifications disabled; all other objects and settings are identical. Seven alternating pairs per fixture, 2,048 ticks per run, 2 gradient workers and 4 compute threads. Correctness exports are disabled.

| Fixture | Whole CPU direct/cache (s) | Tick elapsed direct/cache (µs) | Paired median CPU change | Paired median tick change | Peak RSS direct/cache (MiB) |
|---|---:|---:|---:|---:|---:|
| land-maxima-early | 3.002 / 2.694 | 748.4 / 700.6 | -5.9% | -7.4% | 203.6 / 203.6 |
| land-maxima-middle | 7.986 / 7.779 | 3014.1 / 2931.9 | -1.9% | -4.3% | 203.6 / 203.6 |
| land-maxima-late | 3.699 / 3.780 | 1067.4 / 1017.8 | +2.2% | -2.2% | 203.6 / 203.6 |
| water-nicowar-early | 1.811 / 1.660 | 376.2 / 310.3 | -10.2% | -17.7% | 203.6 / 203.6 |
| water-nicowar-middle | 1.770 / 1.570 | 448.7 / 340.5 | -19.4% | -24.3% | 203.6 / 203.6 |
| water-nicowar-late | 2.526 / 2.522 | 679.4 / 585.4 | -0.8% | -11.5% | 203.6 / 203.6 |
| small-mixed-early | 0.489 / 0.504 | 83.9 / 89.1 | +0.9% | +9.8% | 203.6 / 203.6 |
| small-mixed-middle | 0.716 / 0.726 | 155.7 / 160.3 | +3.0% | +2.0% | 203.6 / 203.6 |
| land-512 | 12.328 / 10.924 | 3293.9 / 2791.0 | -13.2% | -14.5% | 325.6 / 326.1 |

Changes use the median of the seven within-pair ratios. CPU/time columns show separate medians for scale; their ratio can differ from the paired estimator. Full paired ranges are retained in paired-summary.json. Original results remain the primary matrix; any longer pinned confirmation is separate and does not replace an unfavorable sample.

## Preparation including maintenance

| Scenario | Direct/cache thread CPU (ms) | Paired CPU reduction | Cache bytes |
|---|---:|---:|---:|
| land-maxima-early | 92.83 / 7.40 | 92.1% | 865088 |
| land-maxima-middle | 95.16 / 7.57 | 92.0% | 865088 |
| land-maxima-late | 80.01 / 7.45 | 91.9% | 865088 |
| water-nicowar-early | 118.28 / 9.35 | 92.1% | 865088 |
| water-nicowar-middle | 135.56 / 10.86 | 92.0% | 865088 |
| water-nicowar-late | 140.63 / 11.64 | 91.3% | 865088 |
| small-mixed-early | 48.68 / 49.78 | -0.5% | 88 |
| small-mixed-middle | 48.52 / 50.40 | 0.1% | 88 |
| land-512 | 1112.18 / 47.58 | 95.6% | 3457856 |
| dense-resources-512 | 1110.04 / 82.94 | 92.4% | 3457856 |
| dense-areas-512 | 1077.31 / 66.02 | 93.9% | 3457856 |
| sparse-changes-512 | 1170.28 / 129.59 | 89.0% | 3457856 |
| medium-changes-512 | 1193.98 / 350.10 | 70.9% | 3457856 |
| heavy-changes-512 | 2005.48 / 2016.89 | 0.9% | 88 |
| bursts-512 | 1329.28 / 919.44 | 32.8% | 3457856 |
| oscillating-512 | 1579.93 / 1541.79 | -0.0% | 88 |
| mixed-blockers-512 | 1214.81 / 141.85 | 88.3% | 3457856 |
| terrain-changes-512 | 1135.13 / 98.26 | 91.1% | 3457856 |

Raw per-pair measurements retain process CPU, simulation-interval CPU, elapsed time, worker waits, peak resident memory, commands and host load. Preparation measurements include cache construction, its initial direct fields, allocation, updates, rebuilds, copying and destruction. Timing runs omit cell-by-cell comparisons; separate verification runs compare every seeded cell.

## Separate longer confirmation

These runs use 8,192 ticks and affinity to physical CPUs 8–15. Both workload duration and scheduling differ from the primary matrix. SMT siblings 24–31 remain available to other work: this is a pinned shared-host confirmation, not isolation. Original results above are retained.

| Fixture | Paired median CPU change | CPU pair range | Paired median tick change | Tick pair range |
|---|---:|---:|---:|---:|
| land-maxima-late | -3.6% | -16.4% to +2.6% | -2.0% | -10.0% to +4.2% |
| small-mixed-early | +2.3% | -6.8% to +17.2% | +2.8% | -4.6% to +13.1% |
| small-mixed-middle | -1.7% | -3.7% to +4.0% | +2.6% | -10.3% to +9.2% |

## Interpretation

Across the seven large-map windows, the equal-weight geometric mean of each fixture’s median paired ratios is 7.3% lower whole-process CPU and 12.0% lower tick elapsed time. Including both original small controls gives 5.3% lower CPU and 8.3% lower tick elapsed time.

The cache is a clear preparation winner on retained large maps and sparse/moderate mutation, comfortably exceeding the extra 20% seeding target. Heavy mutation and rapid alternation select direct preparation and are approximately neutral. The original late-game CPU result (+2.2%) and short small-map timing result (+9.8%) remain visible; the longer confirmations do not establish a persistent material regression, but do not prove identical performance. The longer small-map median differences are a few percent, with mixed per-pair signs. This is evidence for an overall improvement, not a promise that every game state runs faster.

Owned cache capacity is 865,088 bytes at 256×256 and 3,457,856 bytes at 512×512, including fixed cache state and vector capacities. Bypassed maps retain only the 88-byte cache object. Peak process RSS is reported separately; it does not measure cache ownership or allocator overhead in isolation.

The direct control already contains the optimized direct resource and clearing kernels. These figures measure the additional cache gain; they are not a full PR-versus-master speedup. The control keeps the Map mutation helpers and removes only cache dispatch/notifications. Both binaries use the same object set, compiler and flags otherwise.

No native Windows, macOS, Android or ARM execution was available. Browser validation covers Chromium, Firefox and WebKit on Linux, serial plus 1/2/4 compute-thread variants, using two fixtures and the golden match. Market-mode correctness is covered, but a dedicated market-heavy performance scenario was not measured. Allocation-unavailable fallback is injected through internal state rather than exhausting the OS allocator. Manual gameplay feel evaluation was not performed; seed contents, continuation outputs and automated editor/touch/rendering checks remain unchanged.
