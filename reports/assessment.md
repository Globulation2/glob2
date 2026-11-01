# Adaptive prestige frozen three-arm assessment

All288 games succeeded on one new engine. Each of the96 original map/rotation pairs has newly played off, single-cap1 and adaptive-cap4 arms; opponents stay off/cap1. Historical game outcomes are not controls.

| Format | Contrast | Integrated relative model change, pp | 95% seed-block interval, pp |
| --- | --- | ---: | --- |
| 1v1 | adaptive_minus_baseline | +0.723 | [-2.902, +4.006] |
| 1v1 | single_minus_baseline | +0.723 | [-2.902, +4.006] |
| 1v1 | adaptive_minus_single | +0.000 | [+0.000, +0.000] |
| ffa | adaptive_minus_baseline | +1.087 | [-0.146, +2.443] |
| ffa | single_minus_baseline | +1.114 | [-0.118, +2.454] |
| ffa | adaptive_minus_single | -0.027 | [-0.100, +0.019] |

Standard Python random bootstrap:10,000 draws,seed19,16 seed blocks per format. All96 pairs remain in every contrast; observed whole-game integer integrals are primary.

| Format/arm | Engine W/L/unresolved | Caps | Sole/shared natural prestige wins | Adjudicated W/L/D |
| --- | --- | ---: | --- | --- |
| 1v1/baseline | 13/13/6 | 6 | 1/0 | 17/15/0 |
| 1v1/single | 15/10/7 | 7 | 1/2 | 15/15/2 |
| 1v1/adaptive | 15/10/7 | 7 | 1/2 | 15/15/2 |
| ffa/baseline | 10/29/25 | 35 | 0/4 | 11/49/4 |
| ffa/single | 9/33/22 | 32 | 3/2 | 15/47/2 |
| ffa/adaptive | 10/32/22 | 32 | 4/2 | 16/46/2 |

## Capacity and coverage

All960 team histories integrate exactly. Helper facts reconstructed in 13845 observations; unavailable in 0. Contract violations: `{}`.

[Complete JSON: outcomes,completion times,economic proxies,cancellations,all pairs and capacity distributions](assessment.json)
[Frozen manifest](results/experiment.json)

## Limits

- All controls are newly played on this same SIM117 engine; old records supply exact map bytes and frozen parameters only.
- Model score is the time-normalized observed-game integral of a frozen relative softmax model, not generally a marginal probability of any victory with co-wins.
- Model-adjudicated capped outcomes are not independent completed victories. Natural completion means exclude capped games; capped outcomes remain explicitly censored.
- 16 seed blocks per format; all rotations retained, dependent seats resampled as whole blocks. No efficacy early stopping or activation-conditioned selection.
- Capacity audit assumes the frozen stock catalogue: school level3 gives50 prestige; request minus completed prestige schools measures selected unfinished capacity, not actual funded simultaneous spending. Snapshot maxima and counts are observations, not continuous proof.
- Helper reconstruction uses emitted qualified_idle+root_assigned-training_reserve-ordinary_unmet-unobserved_crews; clamp(division by crew,1,configured), then remaining/material gates; Finish and configured1 retain1. No-headroom still permits base1.
- No claim that telemetry diagnostic flags prove an unsafe engine finish; cancellation telemetry lacks explicit generation/receipt lifecycle.
- Legacy save193 strategy serialization and older-save default1 are implementation-validation evidence, not identifiable from fresh-game efficacy results.
