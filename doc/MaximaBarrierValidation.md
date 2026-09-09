# Barrier redesign validation — 5 September 2026

The access failure is repaired in the saved Garden 3 position and the tested map suite. Gate siting and tower coverage improve; the live comparison does not establish stronger combat performance. See [the design rationale](MaximaBarrierRedesign.md).

## Saved Garden 3 position

Original tick: **144720**. Team 0 has **23 completed physical buildings**. The original policy leaves **16** without a beach route even if every promised gate/channel is cleared. The new policy plans **three local gate pairs** and complete internal connections; the same audit finds **zero** buildings trapped after clearing those contracts.

A real 20,000-tick replay with the final merged executable reopened the first main gate at **152331**, 7,611 game ticks after loading. At tick **164720**, the engine movement audit found **0/23 buildings trapped**, with **zero resources in the replanned maintained routes**. This measurement uses actual resource and forbidden-area state, not hypothetical clearing. Repeated clearing completions show that the routes needed maintenance during the replay; the final snapshot is not a guarantee that no transient blockage ever occurred.

The colony still has economic trouble at that checkpoint: population 127, 100 workers, 8 hungry units and 34 critical-food cases. Restoring movement does not by itself repair all late-game economic problems. The original save was not modified.

## All positions on eight real maps

36 starting positions, 85 buildings. The tower trials provide ample resources to isolate placement quality. The regrowth trial injects wood into empty maintained grass routes and recomputes the policy.

| Measurement | Original | Revised |
|---|---:|---:|
| Buildings trapped after promised clearing | 0 | 0 |
| Buildings trapped after regrowth and promised clearing | 3 | 0 |
| Positions relocating gates after regrowth | 35/36 | 0/36 |
| Resource tiles in selected routes | 155 | 17 |
| Protected odd/odd wheat seeds in initial plans | 1017 | 1061 |
| Funded tower placements covering a complete mouth | 4/36 | 20/36 |
| Funded tower placements covering two complete mouths | 0/36 | 10/36 |
| Tower placements overlapping maintained channels | 7/36 | 0/36 |
| Buildings forced through gates by a fully grown proposed wall | 2/85 | 8/85 |

The final row is a separate geometric stress experiment. All other potential interior ground is open, the proposed coastal wall is physically blocked, and friendly forbidden masks are ignored. Even under that mature-wall assumption, most fresh positions have natural routes around the scoped barrier. On the saved Garden 3 geometry, the corresponding number improves from **0/23 to 16/23**. This is why improved gate coverage must not be described as guaranteed enemy funneling.

## Paired live matches

16 pairs against Nicowar, first and last position on each map, seeds 42 and 74241, up to 40,000 ticks. All **32 runs completed successfully**, with **zero reported seed-stability violations**. The revised policy reported **zero unresolved access repairs** across its recorded policy samples.

Original: **2 wins, 8 losses, 6 unfinished**. Revised: **1 win, 8 losses, 7 unfinished**. Results are mixed: the last Garden 3 position survives where the baseline lost, while the first Wild River position loses where the baseline was still alive. Isles changes from an early-tower loss in the intermediate experiment to survival in the final policy, but its baseline had already won. These runs are regression evidence, not a statistically meaningful win-rate claim.

| Map | Team position | Original population / result | Revised population / result |
|---|---:|---|---|
| Holiday_Island_2 | 0 | 1 / lost | 3 / lost |
| Holiday_Island_2 | 3 | 5 / lost | 2 / lost |
| Archipelago | 0 | 7 / won | 9 / won |
| Archipelago | 4 | 4 / lost | 4 / lost |
| Isles | 0 | 54 / won | 58 / unfinished |
| Isles | 3 | 24 / unfinished | 25 / unfinished |
| Migration | 0 | 77 / unfinished | 46 / unfinished |
| Migration | 3 | 133 / unfinished | 122 / unfinished |
| Garden_3 | 0 | 9 / lost | 5 / lost |
| Garden_3 | 3 | 3 / lost | 65 / unfinished |
| A_big_pond | 0 | 0 / lost | 2 / lost |
| A_big_pond | 2 | 7 / lost | 5 / lost |
| Wild_River | 0 | 26 / unfinished | 2 / lost |
| Wild_River | 5 | 9 / unfinished | 4 / unfinished |
| Sand_River | 0 | 10 / lost | 8 / lost |
| Sand_River | 5 | 34 / unfinished | 71 / unfinished |

Policy timing from emitted samples: median **1.88 → 2.11 ms**, 95th percentile **5.58 → 7.41 ms**, maximum **92.70 → 15.70 ms**. Runs share a machine and differ in game trajectory, so these are diagnostic timings, not an isolated microbenchmark. The additional building access audit and tower feasibility scans have a measurable cost.

## Regression checks and build

After merging with the concurrent colonization changes, the following passed with assertions enabled:

- `MaximaBarrierScenarioTest` — every position on the eight-map suite, plus the long-branch clearing-radius/gate-key regression.
- `MaximaFarmingIntegrationTest` — including 12 adversarial seed harvest/regrowth cycles per map.
- `MaximaCombatIntegrationTest` and `MaximaDirectorRegressionTest`.
- `MaximaPlacementStandaloneTest` and `MaximaFarmingStandaloneTest`.
- `MaximaFarmingPolicyTest.py` — 26 structural tests.
- `scons -j4 build/src/glob2` — merged game executable.

Two broader native suites fail identically on the isolated original and revised code: `MaximaEconomyRegressionTest::birthBudgetScalesBeyondTwenty` rejects a swarm intent, and `MaximaImplementationIntegrationTest::executionRegressions` fails a tactical rally assertion. These baseline failures were reproduced, not suppressed or rewritten. The targeted placement, farming, combat and director checks above pass.

## Evidence and reproducibility

The local evidence bundle is [tournament-results/barrier-redesign-20260905](../tournament-results/barrier-redesign-20260905/). It contains the paired run manifest, binary hashes, raw logs, map audits, original/candidate experimental sources and shared strategy data, the implementation diff, and the replayed checkpoint. Large generated logs and checkpoints remain in the repository’s ignored results directory.

- [Machine-readable map audits](../tournament-results/barrier-redesign-20260905/map-audits.json)
- [Live comparison manifest](../tournament-results/barrier-redesign-20260905/live/results.json)
- [Saved-position replay log](../tournament-results/barrier-redesign-20260905/garden-live-merged.log)
- [Final actual-engine access audit](../tournament-results/barrier-redesign-20260905/garden-after-merged-audit.log)
- [Replayed Garden 3 checkpoint](../tournament-results/barrier-redesign-20260905/garden-after-merged.game)
- [Merged native regression log](../tournament-results/barrier-redesign-20260905/merged-regressions.log)

The before/after matches used one isolated common engine/data snapshot, excluding unrelated concurrent colonization changes from both sides. The final comparison pins a frozen base and format layer with absolute paths, because the macOS game changes its working directory on startup. Current merged sources therefore differ from those exact experimental binaries; the merged native suite, game build and 20,000-tick saved-game replay separately validate integration. Reproduction commands are in the design document.
