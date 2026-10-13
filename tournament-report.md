# Aggregate gameplay tournament: conditional planned-step adoption

**Recommendation: fail; do not adopt this movement policy.** In this local research build, food delivery per worker time falls 30.36%, worker starvation per worker time rises 66.19%, and actual worker moves per food delivery rise 19.20%. These effects are significant after Holm correction across eight prespecified endpoints. Improved conditional direction agreement on unchanged histories is insufficient evidence of acceptable gameplay.

This is a separate headless, unversioned research executable, not production movement or a complete implementation of the planned subsystem. Both arms run the same frozen binary (`f476f788eabcc5996310bec54d342c40a437125ffb1559f77d090b6384f5a541`, policy source `9edaadfc459c9de84c14f5f02c93548a08c9af0a`). The before arm runs reference movement with shadow planning. The after arm conditionally replaces final travel steps after original medical/activity/displacement/movement preparation; invalid or missing suggestions use reference actions. Service, target acquisition, claims, harvesting, delivery, attack and entry/exit handlers remain serial. Replaced movement actions skip reference movement RNG draws. The trial therefore measures this conditional policy including RNG divergence, not a clean isolated change to route fields or a full live implementation. No production experiment is enabled and no trial saves/replays are exported.

## Complete cohort and method

**240/240 games completed successfully**, 120 independent gameplay seeds per arm, 40 maps across eight generator families, three balanced/rotated AI matchups per map: Cortex/Warrush, Cortex/Nicowar and Nicowar/Warrush. Both teams receive the same policy in a game; this is not an old-vs-new head-to-head win-rate comparison. The frozen protocol declared a 30,000 tick cap and a 12,288 tick analysis horizon before running. Initial states/maps, commands, binary hashes and logs are archived. Match seeds are independent between arms; the map/AI inventory and position rotation are balanced.

One game is one statistical observation, with both teams combined. Bootstrap resamples whole map clusters independently in each arm within generator strata (20,000 draws). Unpaired permutations exchange map clusters within generator strata (20,000 draws). This is an exchangeability-based comparison, not exact randomization inference on matched trajectories; independently bootstrapping the shared map inventory is conservative for common map effects. Random seed 20261013. Eight primary comparisons receive Holm correction. Intervals below are **marginal 95%**, not simultaneous familywise intervals; a marginal interval near zero need not agree with Holm-adjusted significance. Endpoint sample counts are 120 per arm. Nonsignificance does not establish equivalence.

## Results

| Prespecified endpoint | Before | After | Relative point change | After − before, marginal 95% CI | Holm p |
|---|---:|---:|---:|---|---:|
| food delivered per 1000 worker ticks | 1.15027 | 0.801067 | -30.36% | -0.349198 [-0.396771, -0.302418] | 0.00039998 |
| transport walk fraction | 0.565565 | 0.551863 | -2.42% | -0.0137025 [-0.027094, -7.64795e-05] | 0.110694 |
| meal walk fraction | 0.0178876 | 0.0148656 | -16.89% | -0.00302194 [-0.00523408, -0.000866375] | 0.0386981 |
| idle worker fraction | 0.0824221 | 0.0609163 | -26.09% | -0.0215058 [-0.0331279, -0.0101602] | 0.00494975 |
| worker starvation per 1000 worker ticks | 0.0141488 | 0.0235138 | +66.19% | +0.00936505 [+0.00654382, +0.0122204] | 0.00039998 |
| unit damage per 1000 ticks | 143.142 | 87.5423 | -38.84% | -55.5998 [-86.1587, -26.9665] | 0.00419979 |
| worker moves per food delivery | 33.978 | 40.5015 | +19.20% | +6.52353 [+4.65097, +8.45847] | 0.00039998 |
| population at horizon | 64.3833 | 43.7167 | -32.10% | -20.6667 [-25.1835, -16.0583] | 0.00039998 |

![Gameplay effect intervals](tournament-effects.png)

Food and starvation are economy regressions. The move/delivery ratio is actual observed worker cell moves divided by food deliveries, pooled as a ratio of totals; it includes workers' other travel and is not a direct resource-route path-length measure. Reduced idle and meal-walk fractions are not automatically benefits when the population and food economy deteriorate. Lower unit damage coincides with fewer units and cannot establish better combat behavior. The transport-walk comparison is not significant after multiplicity correction.

Worker-time denominators combine the mutually exclusive recorded labour/filling categories. Food is `MaterialId::Food` (index 1, not wood index 0). Existing telemetry's geometric distance-to-building fields are not actual travel distances and were not used as such. A separate non-mutating observer counts wrapped cell moves at action completion. Zero-food games would remain in the ratio-of-total calculation; none occur at the horizon in this cohort. Games ending before the horizon retain their final state as absorbing; rates use explicit observed exposure. Early resolutions are retained, not discarded.

- **before:** {'engine_end': 63, 'tick_cap': 57}; 3 games ended before the analysis horizon; 0 adopted / 10,227,407 eligible final travel actions over entire games.
- **after:** {'engine_end': 89, 'tick_cap': 31}; 12 games ended before the analysis horizon; 5,802,854 adopted / 6,242,680 eligible final travel actions over entire games.

## Interpretation and limits

The results reject the assumption that remaining disagreements are harmless conflict resolution. They do not isolate the cause of the economy loss. Remaining known goal-model differences (building perimeter versus footprint, flag/clearing radius and combat approach semantics), waiting/suffix behavior under congestion and changed wandering/RNG trajectories need separate controlled ablations before another adoption trial. No claim that worse pathfinding alone caused these changes.

Six concurrent processes used disjoint CPU affinities (two executor participants each), not exclusive core reservation. Host builds overlapped part of execution; all games completed within the retained process deadline. Counts and logical outcomes are the subject of this study. Different histories/populations/termination and uncontrolled host activity make wall-time comparisons unsuitable for performance claims.

Linux x86-64 only. These AI/map populations do not cover all original workload classes or human play. Cross-platform adopted execution, save/resume, replay/network compatibility, footage review, independent path-quality oracles and allocator-inclusive memory/performance qualification are unverified. The conditional patch is not a shipping trial and retains synchronous fallback searches. None of the original Stage A/B/C gates or thresholds are relaxed or qualified. Production remains shadow-only and disabled by default; keep the PR draft.

[Protocol](tournament-protocol.json) · [Statistics](tournament-statistics.json) · [Game observations](tournament-game-observations.csv) · [Evidence metadata/scripts](tournament-metadata.zip)

Raw logs, results and generated maps are split by generator:
- [symmetric-arena](tournament-symmetric-arena.zip)
- [river](tournament-river.zip)
- [isles](tournament-isles.zip)
- [swamp](tournament-swamp.zip)
- [fingerprint](tournament-fingerprint.zip)
- [maze](tournament-maze.zip)
- [stone-highlands](tournament-stone-highlands.zip)
- [canals](tournament-canals.zip)
