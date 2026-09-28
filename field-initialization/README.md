# Single-pass ordinary-building field initialization

Source base: `7d2dcaf1c67ce902f9c1d721ed4f610f96aba667`. Implementation commit: `c2e817411e0b6beaeebc5022e9696730092e7356`. `change.patch`, `before.cpp`, and `after.cpp` identify the exact change. Both timed variants have lazy gradients enabled; these are incremental savings over the existing lazy implementation, not the original eager-to-lazy comparison.

Ordinary buildings now write goal, forbidden or unreachable directly in one linear traversal. They no longer clear the entire field before classifying it. The separate path also removes flag policies and coordinate wrapping from their inner loop. Flag rules, seed scanning, propagation, field lifetime, snapshot semantics and serialization are unchanged. Both eager and lazy modes use the initializer. No new cache or persistent allocation is added.

## Whole-game timing

32 uninstrumented release runs: four paired comparisons per workload, alternating AB/BA. Four warm-ups (one per binary, including profile binaries) excluded. All runs started and ended on AC power, with no competing Glob2 process. Thermal status and process CPU usage were recorded. Ordinary desktop background activity was not disabled. Profiles ran separately and are excluded from whole-game speedup estimates.

CPU reduction is the geometric mean of paired process-CPU ratios. Intervals are paired Student-t intervals on log ratios (four pairs, three degrees of freedom). They describe repeatability of these retained workloads on this Mac, not all games/platforms. Small samples and background activity limit precision.

| Workload | Before CPU (s) | After CPU (s) | CPU reduction | 95% interval |
|---|---:|---:|---:|---:|
| arena256-2 | 9.014 | 8.966 | 0.53% | 0.17 to 0.90% |
| arena128-2 | 2.983 | 2.933 | 1.69% | 1.15 to 2.22% |
| lakes256-4 | 13.762 | 13.553 | 1.52% | 1.07 to 1.97% |
| arena512-4 | 42.281 | 41.680 | 1.42% | 0.82 to 2.02% |

## Initialization diagnostic

16 additional runs: two alternating before/after pairs per workload. Lightweight timers measure all initialization, split by ordinary buildings versus virtual flags; a nested timer measures the old field fill. No per-cell counters are used in timing binaries. Timers measure wall duration, not hardware cycles. Small clock overhead and compiler/layout differences mean these are diagnostic phase estimates. The total includes locked fields and perimeter checks. Counts and cell totals match across before/after profiles.

| Workload | Ordinary init before/after (ms) | Ordinary reduction | All initialization reduction | Old ordinary fill (ms) |
|---|---:|---:|---:|---:|
| arena256-2 | 201.8 / 134.1 | 33.5% | 33.5% | 2.1 |
| arena128-2 | 85.1 / 54.2 | 36.3% | 32.4% | 0.7 |
| lakes256-4 | 969.8 / 664.3 | 31.5% | 29.4% | 10.8 |
| arena512-4 | 2156.3 / 1475.6 | 31.6% | 30.9% | 31.7 |

The old fill accounted for only 0.8–1.5% of ordinary initialization time. This suggests the simpler ordinary-building loop is the main benefit, rather than the fill alone; the combined changes were not benchmarked individually.

`analysis.json` retains both ordinary and virtual timings, setup/propagation scope totals, per-pair savings and checks that work counts match. Reductions in initialization time are not percentages of total game time.

## Correctness

Native release client builds pass. Gradient oracle, building invalidation, immobile-unit and save-safety checks pass (both modes where applicable). A validation-only binary runs the old initializer alongside the new initializer and compares every cell before propagation on every rebuild. Four full games in each mode pass this comparison, match original-baseline per-tick checksum sidecars, and produce byte-identical initial/final saves. The old-save continuation also matches checksums and final save bytes. This preserves the earlier Maxima diagnostic caveat rather than fixing that unrelated issue.

The field-comparison code and phase timers are excluded from production. Source copies and build scripts retain them for review. Full-game cross-platform equivalence and interactive play were not verified in this local experiment. The inherited lazy feature remains opt-in; the initializer is shared with eager mode, whose correctness was tested but whose incremental total CPU benefit was not timed here.

## Reproduction and evidence

`profile-plan.json` retains the map/game seeds, players, tick counts, exact sequence and binary hashes. `runs.py`, `profile-measurements.jsonl`, `profile-warmups.jsonl` and the archived logs retain commands/results. Whole-game runs use the original four generated maps from the earlier [retained fixtures](https://github.com/Globulation2/glob2/tree/ecc77866349e3f48ebad5712dff44eeeb08546d3). `build-profiles.py`, `InitProfile.h`, `before-profile.cpp`, `after-profile.cpp` and `verify.cpp` retain the diagnostic builds. Build commands use native release SCons settings. Full local process inventories are excluded from published records.

`tests.json`, `validation.json`, `continuation.json`, logs and archives retain the verification results, checksums and lazy-mode save files. The corresponding eager files have identical hashes and are not duplicated in archives. `manifest.json` hashes all packaged evidence. No binaries or temporary diagnostics enter the implementation branch.
