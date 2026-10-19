# Family identity main effects (research only)

Existing final1280 dataset,287045eligible rows;67018heldoutrows/315maps; unchangedweights,persistentmapsplits,H128target,A16threshold,andfour-headfit. Baseline12notrefit. Augmented17adds5raw0/1familyindicatorinputs withidentitytransforms,nointeractions. Marketsisconstantzeroandunlearned, withzeroaddedrows.

| Metric | Existing12 | Family17 |
|---|---:|---:|
| decision_regret | 2.878898 | 2.203938 |
| mae | 7.818300 | 7.307332 |
| selected_pct | 5.552406 | 7.397794 |
| positive_benefit_captured_fraction | 0.455901 | 0.686000 |
| decision_regret_p99 | 60.000000 | 44.000000 |
| mse | 2136.174906 | 2067.412675 |

Grouped-CV regret improves inall3folds(mean17.41%). Heldoutregretimproves23.45%. Atmatched5.5524%weightedselectionbudget, netdirectionbenefit/allweightimproves1.8374→2.5131; positivebenefitcapture45.59%→61.22%. Thisdescriptiveheldoutrankingisnotadeploymentthresholdtunedonheldout.

## Safety and interpretation

The model all but stops selecting guard refreshes in the observed heldoutrows:6.449%→0.00207%. Materialsselectionrises11.63%→26.08%;clearfalls3.50%→1.32%. Guarddirectioncostlabelsmostlyzero, while the prior seven-input prospective failure showed that this objective can miss cached-unreachable movement loss. The apparent offlinegain therefore needs an actualprospectivereachability test before production use. No CPUclaim is made.

Calibration is mixed: weighted signedbias improves-.3453→-.3324, but prediction-decile calibrationerror worsens.4999→.7845. MaterialsMAE rises20.62→22.98 despite lowerdirectiondecisionregret. The explicitfamilyidentitycanlearncurrentfamily-specificlabelscale; futureunmeasuredfamiliesremainunvalidated.

## Per-family heldout behavior

| Family | Rows | Regret12 | Regret17 | Selected12% | Selected17% |
|---|---:|---:|---:|---:|---:|
| clear | 20591 | 1.718366 | 1.421578 | 3.50366 | 1.32326 |
| forbidden | 4437 | 0.000000 | 0.000000 | 0.00000 | 0.00000 |
| guard | 15943 | 1.068750 | 0.076387 | 6.44918 | 0.00207 |
| materials | 26047 | 8.054730 | 6.690194 | 11.62818 | 26.08060 |

Fullformat,generator,size,age,calibration,budgetandselected-counterfactualoutcometablesareinREPORT.json. Stalls,reachability,planningremainseparateoutcomes,notaddedthedirectioncosttarget. SelectedoutcomeratesusepotentiallyoverlappingepisodequeriesandareNOTuniquelivegamestallrates. No canonical, native, schema, version or coefficient changes were made.
