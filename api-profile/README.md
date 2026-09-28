# Building gradient API cleanup and remaining-cost profile

The original lazy optimization saved **2–8% total CPU** versus the original baseline in the [earlier 162-run controlled study](../timing-study/README.md). This follow-up compares **the existing lazy implementation with the safer lazy API** and profiles its remaining costs. An inconclusive incremental API timing result does not mean the original optimization had no benefit.

## Implementation

[7d2dcaf1c](https://github.com/Globulation2/glob2/commit/7d2dcaf1c) removes the public partial-array switch and separate resolve method. `buildingGradient` always returns a complete field. Private preparation applies the existing refresh/use policy; private scalar and direction adapters resolve their input before accessing it. `buildingAvailable`, `pathfindBuilding`, and cached round-trip queries retain lazy behavior. Save layout, cache deadlines and eviction are unchanged. The uninstrumented before/after runs also retain exactly the same `gradient.building` and `gradient.building_resume` call counts in every workload.

This tightens the Map query API without introducing a view framework or rewriting the legacy Building cache storage. Internal serialization/rendering code still accesses that storage after completion. A new integration regression exercises scalar, movement and complete-array reads in all seven swim classes; existing oracle tests cover weighted snapshots, equal-cost neighbors and query order.

Native release client/server builds and all seven selected mode/harness runs pass. Four retained games match the original baseline's per-tick checksums and initial/final save bytes in both modes (65,442 ticks per mode). The 1,024-tick old-save continuation also matches checksums and final save bytes in this run. Earlier evidence retains the pre-existing Maxima telemetry caveat; this successful continuation does not fix that separate diagnostic bug. Cross-platform full-game equivalence and interactive play are not established by these local checks.

## Remaining building-gradient costs

Three instrumented runs per original workload. Percentages are fractions of measured building-field work, **not fractions of whole-game CPU**. Initialization includes filling the field, classifying obstacles/goals and checking reachability. Search setup includes queue clearing and water-buffer resizing. Scanning includes finding seeds and, for weighted classes, recording frozen water costs. Propagation includes actual resumed/full completion work.

| Workload | Initialization | Seed + water scanning | Search setup | Propagation | Queue growth (subset) |
| --- | ---: | ---: | ---: | ---: | ---: |
| arena128-2 | 33.2% | 6.8% | 0.10% | 59.9% | 5.19% |
| arena256-2 | 33.4% | 6.9% | 0.03% | 59.7% | 1.76% |
| arena512-4 | 34.8% | 8.1% | 0.02% | 57.1% | 0.46% |
| lakes256-4 | 44.0% | 9.7% | 0.05% | 46.2% | 1.95% |

Queue-growth time is already inside scanning/propagation and must not be added again. It measures `push_back` only when capacity is exhausted, including allocation/copy and clock overhead. This is not a pure allocator measurement, and queue destruction/freeing was not isolated separately. There are no per-cell clock calls except at capacity growth, but per-cell counters and branches still perturb the kernel. Phase shares are diagnostic approximations, not an exact uninstrumented CPU budget; the instrumented binary is never used to claim an API speedup.

| Workload | Init ms | Scan ms | Queue growth ms | Pushes | Capacity-growth events | Growth/push | Cumulative added capacity MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| arena128-2 | 107.7 | 22.2 | 16.8 | 12,195,847 | 411,827 | 3.38% | 9.47 |
| arena256-2 | 211.0 | 43.7 | 11.1 | 32,745,527 | 256,090 | 0.78% | 11.54 |
| arena512-4 | 2253.3 | 524.1 | 30.1 | 340,403,202 | 614,743 | 0.18% | 48.02 |
| lakes256-4 | 1088.3 | 241.0 | 48.3 | 99,922,354 | 1,236,179 | 1.24% | 40.39 |

Capacity bytes are cumulative increases across allocations, including queues subsequently destroyed. They are not peak/live retained memory. Weighted scan timing combines seed collection and water copying; it does not isolate the marginal cost of water alone. Work counts (builds, scanned cells, pushes, pops, stale entries, layers and growths) are recorded in `profile-analysis.json` and identical across each scenario's three repetitions.

The actionable ranking is:

1. **Field initialization is the largest remaining preparation cost.** An experiment that reduces repeated obstacle classification could matter; it must preserve the original frozen snapshot and invalidation semantics.
2. **Folding seed collection and water recording into existing initialization traversal** is a narrower next experiment. Scanning accounts for only 7–10% of building work, so that is the relevant ceiling before accounting for work that still must happen and the rest of the game.
3. **Custom bucket storage is lower priority.** Growth events are frequent, but their measured time is a small part of building work; queue clearing/resizing is smaller still. Inline storage or an arena would add retained memory and lifetime complexity for a comparatively small potential gain.

No further algorithm or allocation optimization is included in the PR from this study.

## API before/after timing

Old and new **uninstrumented** lazy binaries use the same retained maps, seeds, AI lineups and tick targets as the parent timing study. Three initial paired repetitions per workload used alternating order. AC power was connected during a 512² pair; all original measurements are retained. Before examining the final result, two opposite-order AC-only pairs per workload were declared (`ac-followup-plan.json`). They did not run: power had switched back to battery before the first additional trial could start (`ac-driver.log`). The user then chose to stay on battery, and the same two opposite-order pairs per workload ran on battery (`battery-followup-plan.json`), with two additional excluded warmups. There are 52 measured runs: 40 uninstrumented comparisons and 12 diagnostic profiles, plus five excluded warmups.

The table separates all pairs from pairs whose two processes both began and ended on battery. Approximate 95% Student-t intervals are computed on paired log CPU ratios. Positive means the safer API used less CPU. These small samples remain sensitive to timing drift; they do not supersede the earlier 162-run baseline study.

| Workload | All-pair CPU saved (95% interval) | Battery-only pairs | Battery-only CPU saved (95% interval) |
| --- | ---: | ---: | ---: |
| arena128-2 | +0.22% [-6.34, +6.38] | 3 | -1.07% [-14.50, +10.79] |
| arena256-2 | +2.19% [+1.06, +3.29] | 3 | +1.58% [+0.64, +2.51] |
| arena512-4 | +0.40% [-7.64, +7.83] | 3 | -2.39% [-20.02, +12.65] |
| lakes256-4 | -1.81% [-15.57, +10.31] | 3 | +3.42% [-6.65, +12.53] |

Low-power mode was disabled for both power sources. Power snapshots and thermal messages are retained per run. A separate running Glob2 app was stopped before these measured trials; no other Glob2 process was present at any measured run's start. All measured game outcomes match the baseline. Battery/AC transitions and substantial timing variation make the all-run estimates unreliable for a small API effect. The battery-only subset keeps those transitions out; it is still a small, single-machine sample. The full JSON also retains AC-only estimates.

## Hardware counters

A separate 90-second Instruments CPU Counters trace of the clean API binary on arena512-4 completed and produced the full game result. It ran before the separate Glob2 app was stopped, so it is **not a controlled before/after performance comparison**. The complete native trace stays local because it contains broader system metadata. Exported relevant data and the analysis are described in `hardware-analysis.md`; no cache-efficiency claim should be inferred from the phase timings alone.

## Reproduction and retained evidence

`api.patch` and source commit identify the production change. `tests.json`, `validation.json`, `continuation.json`, and compressed logs/checksums/saves retain the checks. `profile-plan.json` and `ac-followup-plan.json` fix the workloads and sequence; `profile-measurements.jsonl` and `profile-warmups.jsonl` preserve every command, result, counter and timing. `profile-analysis.json` and `analyze_profile.py` retain the calculations.

`Profile.h`, `instrument.py`, and `instrumented.patch` retain the temporary measurement changes. To reproduce, build the production commit, save an immutable copy of the release executable, run `instrument.py` from its original artifact layout, build/copy the instrumented binary, then restore the two `.cpp` files saved by that script. The driver uses explicit pinned binary paths and original fixtures from the parent evidence. No instrumentation is committed to the implementation branch. Run serially on stable power without competing jobs, and never compile or compress during measurements. The provided scripts record the original local paths; adapt only their path configuration when reproducing elsewhere.
