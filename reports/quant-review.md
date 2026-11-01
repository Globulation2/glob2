# Independent completed-cohort review

**Keep prestige policy disabled by default on this evidence.** The adaptive implementation passes the observed request/helper contracts, but it does not establish an effectiveness benefit over the single-pipeline policy or over policy off. The four-pipeline setting was never selected in this cohort. Retaining the opt-in configurable route is reasonable; changing defaults is not justified by a positive point estimate alone.

All288 freshly played games succeeded on the corrected SIM117/save193/net75 bundle. The coordinator exited0 and both dedicated idle worker daemons were confirmed stopped. All96 original map/rotation triples remain. The aborted v1 contributes no controls or outcomes. No efficacy was inspected before full completion.

## Primary effectiveness

I independently reconstructed every retained integer integral and the six score contrasts from raw committed results. All960 team histories begin at0, reach their actual terminal tick, are strictly increasing in time, and reproduce their retained areas exactly. A separately implemented equal-seed-block bootstrap matches the main report means and intervals within1e−14. It uses Python random seed19,10,000 draws and16 whole seed blocks per format, retaining dependent seat rotations together.

| Format | Adaptive − off, pp | Single − off, pp | Adaptive − single, pp |
| --- | --- | --- | --- |
| Duels | +0.723 [−2.902,+4.006] | +0.723 [−2.902,+4.006] | 0.000 [0.000,0.000] |
| FFA | +1.087 [−0.146,+2.443] | +1.114 [−0.118,+2.454] | −0.027 [−0.100,+0.019] |

These are changes in a frozen **relative model score**, integrated over observed match time and normalized separately for each game. They are not calibrated marginal probabilities of any victory when separate teams can co-win. Both policy-versus-off intervals span zero. Failure to detect a difference is not a noninferiority proof or an equivalence guarantee.

| Format/arm | Raw engine W/L/unresolved | Tick caps | Sole/shared engine wins | Sole/shared natural prestige wins | Model-adjudicated W/L/D |
| --- | --- | ---: | --- | --- | --- |
| Duel off | 13/13/6 | 6 | 13/0 | 1/0 | 17/15/0 |
| Duel single | 15/10/7 | 7 | 13/2 | 1/2 | 15/15/2 |
| Duel adaptive | 15/10/7 | 7 | 13/2 | 1/2 | 15/15/2 |
| FFA off | 10/29/25 | 35 | 6/4 | 0/4 | 11/49/4 |
| FFA single | 9/33/22 | 32 | 7/2 | 3/2 | 15/47/2 |
| FFA adaptive | 10/32/22 | 32 | 8/2 | 4/2 | 16/46/2 |

Adaptive gains one completed FFA victory relative to single, without a corresponding positive mean model change. That disagreement is a reason to retain both metrics. Cap adjudication itself uses this same model and is not independent completed-victory confirmation. Capped survivors remain censored, not certain eventual winners or losers.

## Exposure and the actual bottleneck

Raw helper decisions—not sparse director snapshots—show selected capacity above1 in **3/96 adaptive games**, for **12 decisions:9 choices of2 and3 choices of3; no choice of4**:

- Duel Islands1012/seat1: one choice of2.
- FFA Islands1010/seat0: six choices of2 and three choices of3.
- FFA Islands1012/seat0: two choices of2.

Only one capacity2 observation appears in the roughly1,000-tick director snapshots. Using that sparse series to claim exposure in only one game would be wrong. Director-request and helper-decision distributions answer different questions; neither equals funded simultaneous construction.

The independent full-log gate audit reconstructed all13,845 sampled adaptive decisions. **13,833/13,845 (99.9133%) had worker_limit1**. All12 decisions with worker_limit above1 actually selected capacity above1; no remaining/material gate suppressed those allowances to1. The precise conclusion is that enough **net qualified spare headroom for two crews** was rarely measured, not that spare workers were absent.

Material limit was4 in every sampled adaptive decision, so material limits did not bind. Worker limit alone was the strict minimum in13,570 decisions. Worker/remaining limits tied in273 decisions (270 at1,three at2). Remaining steps alone bound two decisions, reducing worker allowance3→chosen2 in the duel and4→chosen3 in FFA. Another84 adaptive Finish decisions bypassed sampling and deliberately retained1. All13,940 single-arm decisions used its base1 bypass, including41 Finish decisions. These are repeated decision observations, not independent trials.

The gate facts subtract training reserve, ordinary unmet builder demand and unobserved committed crews from qualified idle/root-assigned workers. The observed policy therefore usually retains its base pipeline despite configuration cap4. A future predefined assessment would need substantially more qualified-headroom exposure before claiming broad effectiveness of parallel preparation. No games were dropped or parameters changed in this assessment.

## Outcomes and diagnostic cases

All32 duel pairs have identical adaptive/single terminal ticks and all-team probability histories. Their final world checksums match31/32: Islands1012/seat1 differs, with small food-statistic changes. Thus the **model trajectories and outcomes** are identical; execution is not identical in every duel.

Only two FFA adaptive/single pairs differ in probability history and final checksum; the other62 match. They account for the complete adaptive-versus-single mean difference:

| Pair | Adaptive − single model score | Raw outcome single→adaptive | Terminal ticks |
| --- | ---: | --- | --- |
| FFA Islands1010/seat0 | −2.132 pp | sole win→sole win | 89,314→84,866 |
| FFA Islands1012/seat0 | +0.406 pp | loss→sole win | 57,410→58,722 |

In Islands1010/seat0, the common-time score change is−0.463pp; the different-duration normalization/tail component is−1.669pp. The adaptive game finishes its prestige win4,448ticks earlier. In Islands1012/seat0, the common-time score is−0.107pp and the tail component+0.514pp. These exact descriptive decompositions show why score and completed victory need not move together; they do not replace the primary score or identify a causal duration correction.

Adverse policy-versus-off cases persist in both enabled arms: duel Islands1010/seat1−22.059pp (off natural win→enabled unresolved cap), duel Continents1007/seat1−19.509pp (off natural win→enabled unresolved cap), and FFA Continents1006/seat3−28.506pp (off natural win→enabled unresolved cap, ending with9 units). FFA Islands1009/seat1—one retained diagnostic replay pair—ends with the subject eliminated in both enabled arms versus unresolved off. Conversely, Arena1004/seat0 improves+10.560pp versus off and reaches a natural win. These selected cases illustrate heterogeneity, not a selected acceptance sample; all96 triples determine the estimates.

## Coverage, cancellations and timing

The helper/request audit reports no contract violations, no missing helper fields and no same-tick race-alignment gaps. All960 team streams have director observation at tick0. The maximum final director sample-hold interval is1,267ticks across all teams (1,231 for subjects). Economic summaries are sampled-state time integrals through survival end, not continuously observed worker actions or spending. The two root-added food-active fractions remain intact.

There are exactly two cancellation issues and two resolutions, both `parallel_commitment`, in single and adaptive FFA Islands1010/seat2. They issue at game_tick49,830 and resolve49,839, nine ticks later. No unresolved cancellations or potential terminal unsafe-own-gain flags were observed. These screens do not prove safety for every possible interleaving or custom catalogue; dedicated correctness/save/network tests provide separate evidence.

| Natural prestige wins observed | Off mean ticks (n) | Single mean ticks (n) | Adaptive mean ticks (n) |
| --- | --- | --- | --- |
| Duel | 72,194 (1) | 75,959.33 (3) | 75,959.33 (3) |
| FFA | 58,586 (4) | 68,968.40 (5) | 66,519.33 (6) |

These completion means condition on different sets of games becoming prestige wins, including shared wins. They are not paired speed effects, and capped games are not assigned invented finish times. The adaptive FFA mean being lower than single is descriptive, not proof that it wins faster in general.

## Evidence and limits

- [Primary assessment](assessment.md) and [complete assessment JSON](assessment.json).
- [Independent raw-integral/bootstrap/equality/cancellation reconstruction](independent-review.json), generated by `independent_review.py`.
- [Full helper gate histograms and binding definitions](capacity-gates.json), generated by `audit_capacity_gates.py` from hash-verified compressed logs.
- [Confirmed worker shutdown](worker-shutdown.json), [immutable registration](registration.json) and [frozen manifest](results/experiment.json).

This is one fixed model,one AI matchup,four map families,four seeds per family and only16 blocks per format. Fairness-selected original maps,cyclic seats and absent cap4 exposure restrict generalization. Source/binary validation is Linux-only; fresh-game results do not establish legacy save migration or cross-platform determinism. No model refit,post-hoc tuning,new match or production change was performed in this review. The appropriate conclusion is a validated configurable implementation with **limited parallel-capacity exposure and uncertain efficacy**, keeping default activation off.
