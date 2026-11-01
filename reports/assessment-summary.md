# Maxima adaptive prestige assessment

The adaptive 1–4 allowance works, but it changes play rarely under this cohort. It selected more than one pipeline in only three of 96 paired setups. Two FFA cases benefited in actual play: one loss became a sole prestige win, and another sole prestige win finished 4,448 ticks earlier. The aggregate model-score comparison does not establish an overall improvement. The user subsequently directed adaptive prestige to become the new-match default. `prestige.enabled=true` and `prestige.max_pipelines=4` now provide that default when normal prestige victory is enabled; the frozen results and independent recommendations below remain unchanged.

The effective bottleneck is net qualified builder headroom. In 13,845 sampled adaptive decisions, the worker limit was one in 13,833 decisions (99.913%). This means insufficient headroom for **two full crews after reservations**, rather than an absence of builders. The material gate returned four throughout these observations. Remaining prestige steps reduced two above-one allowances, but neither materials nor remaining steps reduced any above-one worker allowance all the way to one. A further 84 winning-finish decisions deliberately retained one pipeline without sampling capacity.

## What changed in the matches

The helper selected two pipelines nine times and three pipelines three times. It never selected four. All 12 above-one decisions occurred on Islands maps, in three games. The four-pipeline path has focused scenario coverage, not empirical match coverage in this cohort.

Director snapshots are emitted only every 1,000 AI ticks, whereas helper decisions are emitted at each strategic evaluation. The sparse director series captured only one above-one observation. The exposure counts here use **all helper decisions**; both series remain available in the JSON. An allowance is a development request, not proof that every requested project was funded simultaneously.

Only three adaptive/single pairs ended with different world checksums. Duel model histories and outcomes were identical, although one duel's world checksum changed. The two changed FFA trajectories were:

| Setup | Single pipeline | Adaptive allowance | Observed result |
| --- | --- | --- | --- |
| Islands seed1010, rotation0 | Sole prestige win at89,314 ticks; own prestige300 | Sole prestige win at84,866 ticks; own prestige300 | Same victory,4,448 ticks earlier |
| Islands seed1012, rotation0 | Loss at57,410 ticks; own prestige150, winning opponent200 | Sole prestige win at58,722 ticks; own prestige200, opponents150/150/100 | Loss converted to a prestige win |

The first case nevertheless loses2.132 percentage points on the primary model average. A post-hoc common-prefix diagnostic attributes1.669 points of that difference to the longer single-pipeline game's additional strong tail, and0.463 points to the observed common prefix. This illustrates a limitation of the time-normalized model average: a faster prestige victory can reduce the average by ending the observation earlier. The diagnostic does not replace the prespecified primary metric or establish a general causal mechanism. See [timing diagnostic](timing-diagnostic.json).

## Full paired results

All288 games succeeded:96 exact original map/rotation configurations, each freshly played with policy off, single-cap1 and adaptive-cap4 on the same frozen SIM117 engine. All opponents used Maxima with prestige policy off/cap1. There were16 seed blocks per format, all seat rotations, game seed19, a90,000-tick cap, normal prestige victory and probability early victory explicitly0. No historical outcomes or interrupted-v1 records were used as controls.

| Format | Adaptive − off model score, pp | Adaptive − single, pp |
| --- | --- | --- |
| Duels | +0.723 [−2.902,+4.006] | 0.000 [0.000,0.000] |
| Four-player FFA | +1.087 [−0.146,+2.443] | −0.027 [−0.100,+0.019] |

Intervals are95% whole-seed-block bootstrap intervals:10,000 draws, Python random seed19. All960 retained team histories integrate exactly and cover the whole observed game. The frozen relative softmax model score is averaged over observed game time; it is not a calibrated probability of any victory when shared wins are possible.

| Format/arm | Engine wins/losses/unresolved | Sole/shared natural prestige wins | Normal adjudicated W/L/D |
| --- | --- | --- | --- |
| Duels/off | 13/13/6 | 1/0 | 17/15/0 |
| Duels/single | 15/10/7 | 1/2 | 15/15/2 |
| Duels/adaptive | 15/10/7 | 1/2 | 15/15/2 |
| FFA/off | 10/29/25 | 0/4 | 11/49/4 |
| FFA/single | 9/33/22 | 3/2 | 15/47/2 |
| FFA/adaptive | 10/32/22 | 4/2 | 16/46/2 |

Raw engine wins include shared victories. Normal tournament adjudication resolves tick-cap survivors with the frozen probability model and represents joint winners as ties. It is not independent evidence of completed victories. Natural prestige wins total5/8/9 for off/single/adaptive.

Economic costs remain relevant. In FFA, adaptive versus off raised mean sampled school stock by0.120 [0.046,0.199] and time with a director-reported food shortfall by2.877 percentage points [1.033,4.930]. Adaptive versus single changed those measures by only0.005 schools [0.000,0.012] and−0.071 food-shortfall-time points [−0.193,0.000]. These are exploratory sample-and-hold diagnostics with unadjusted intervals, not causal productivity or spending measurements. Completion-time means condition on the winners actually observed; their changing populations do not establish a general speed gain. Full conditional times, coverage and economic intervals are in the linked data.

Adverse enabled-versus-off cases remain in the analysis. Examples include duel Islands1010/rotation1 (−22.059 model points, win→cap), duel Continents1007/rotation1 (−19.509, win→cap), FFA Continents1006/rotation3 (−28.506, win→cap), and FFA Islands1009/rotation1 (−16.029, unresolved→loss). All four stayed at allowance1 and matched the single policy, so those costs cannot be attributed to the extra parallel allowance.

## Safety, validation and limits

The request/helper audit found no contract violations. One parallel-commitment cancellation occurred in each enabled arm, in the same FFA setup; both resolved after9 ticks. No unresolved cancellation or terminal-loss-own-gain screen flags remained. This is an observational screen, not proof against every possible completion interleaving. Focused tests cover unsafe starts, cancellation acknowledgement, stale identities, custom catalog boundaries and save continuation.

The strategy preserves ordinary resource arbitration, placement, population, qualification, training-reserve and service-continuity gates. It permits one positive prestige completion commitment at a time. Dedicated investment uses reversible upgrade chains with zero-prestige prerequisite buildings/sites. Unsupported first-level or positive-origin custom layouts do not receive dedicated investment; new/repair prestige restorations still count as pending. No demolition protocol was added.

The algorithm passed508 native cases, two additional online-version cases and76 Python checks. Current193 full-game continuation matches uninterrupted play, and128 per-tick world/replay/snapshot checksums match Linux development and optimized builds. Real191/192 loading and migration are covered. Other supported-platform execution and manual play were unavailable.

During assessment, master advanced toSIM117 for unrelated localization work. Final integration reservesSIM118 and regenerates its golden identity; the comparative cohort remains honestly labeledSIM117. The prestige algorithm is unchanged, and the user-requested default is enabled. The cohort explicitly pinned enabled/max-pipeline parameters, so its arm definitions do not depend on the later default flip. The version-only source/binary comparison and final boundary/checksum checks are recorded separately in [validation](final-verification.md).

## Evidence

- [Canonical outcomes, intervals and complete data](assessment.md)
- [Economic intervals and capacity plot](supplement.md)
- [Independent quant review](quant-review.md)
- [Independent director review](director-review.md)
- [Capacity gates](capacity-gates.json)
- [Frozen cohort source/binary hashes](source.json)
- [Worker shutdown](worker-shutdown.json)
- [Excluded interrupted first cohort](../adaptive/ABORTED.md)
