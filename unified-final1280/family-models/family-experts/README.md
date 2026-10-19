# Family experts and conditional1024 age-cap research

Same immutable1280dataset,287045eligible rows,67018heldout rows/315maps. Existing shared12andonehot17 fits reused. Four family experts each retain the same12covariates, four integer heads, regularization and transforms. Training weights preserve original global relative weights within the family and apply only a scalar normalization to mean1; pooled scoring uses original global heldout weights. Markets has no rows/expert and is not imputed.

| Model | MeanCVregret | Heldoutregret | MAE | Selected% | Calibrationdecileerror |
|---|---:|---:|---:|---:|---:|
| baseline12 | 2.441632 | 2.878898 | 7.818300 | 5.5524 | 0.499852 |
| onehot17 | 2.016592 | 2.203938 | 7.307332 | 7.3978 | 0.784846 |
| experts12 | 1.963492 | 2.125068 | 7.222518 | 7.7947 | 0.451949 |

Experts improve heldoutdirectionregret3.58%versusonehot17 and26.18%versusshared12. At the shared12weightedselectionbudget5.5524%, netdirectionbenefit/allweight is1.8374/2.5131/2.6028 for shared12/onehot17/experts. These are offline comparisons, not CPU or prospective quality claims.

## Guard coverage and target blind spot

Heldoutguardrows:13094of15943(82.13%) hadzero capturedgoals, all withzero directionbenefit. Allnonzero guarddirectionbenefit appears on Maxima/Warrush owning-team seats; otherAIs still issue fallbackguardqueries. Exact seat attribution uses episode.team→actualspec.ais[team] and the frozen CLI team=p contract, never whole-map roster presence.

Even among2849positive-captured-goal heldoutrows, the onehotmodel selects just1row (.01284%weighted), versus657(16.847%)for shared12. The expert likewise almostnever selects guard overall (.0062%). Emptyfallbackdominance is therefore only part of the story. The directioncostobjective omits cached-unreachable missedmovement; two zero→positivegoal rows show4missed moves fixedbyrefresh withzero directionbenefit. Actual prospective reachability remains required. No goal-count feature was added by this analysis.

## Conditional age1024 cap

There are13412heldoutrows at servingcaptureage>=1024,15.599%globalweight;11926havefuturehorizon movementqueries. The CSV does not record actual demand on the decision tick. Cap results assume an actual demanded decision on each aged row; they are not measured admissions and do not imply idle-field work.

| Model | Conditionalcap selected% | Conditionalcap directionregret | Newlyforcedrows |
|---|---:|---:|---:|
| baseline12 | 19.9672 | 5.066635 | 12032 |
| onehot17 | 21.8514 | 4.432323 | 12133 |
| experts12 | 22.1755 | 4.345946 | 12048 |

The cap increases direction-only opportunity cost under old trajectories; that objective does not include its intended missed-movement/reachability protection. It does not predict a new policy equilibrium or prove all guard errors are fixed. Mandatory agedrowweight15.599%exceeds the tested1–10%ranking budgets, so those conditionalcap budget comparisons are explicitly marked infeasible rather than dropping required work.

Full per-family/format/generator/size scores, class support, calibration, matched budgets and separate selected-row counterfactual stalls/reachability/planning are inREPORT.json. Episode queries may overlap and are not unique live game outcomes.

Resultsverification: all16fitted expert models were saved. JSONreport serialization initially failed on a NumPy integer; scoring-only recovery reused those models without refitting. The final logging print also failed after valid report/status serialization; parsed outputs and hashes are verified inRESULTS-VERIFIED.json. No canonical/native/model/schema changes occurred.
