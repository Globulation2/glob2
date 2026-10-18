# Provisional124 model directions and importance

The model is two logistic probability heads and two ridge conditional-magnitude heads, not one linear equation. Each head is linear in transformed inputs before its link/clamp. Fitting standardized those transformed inputs; exported coefficients undo that standardization. The final score subtracts expected negative magnitude from expected positive magnitude, so one input has four coefficients and may have opposite effects in different states.

All40416 heldout predictions for both fitted models were checked bit-for-bit against permanent scalar integer inference. Regret aggregates reproduce the original report. No refit, coefficient emission, split change, or promotion.

## Conditional final-score response

Rows sorted by mean absolute local response per one weighted-heldout standard deviation. Central steps use1%SD (minimum1Q12unit) with valid bounds; results are rescaled to1SD. Signed mean is average net direction. Holding all other transformed inputs fixed can break count/rate correlations; this is mathematical local sensitivity, not an actual simulation intervention or unique predictive importance. All scores use movement-benefit units.

| Feature | Mean signed /SD | Mean absolute /SD | Positive / negative / zero response weight |
|---|---:|---:|---:|
| q32 | +3.0904 | 3.1028 | 72.4% / 6.8% / 20.9% |
| source_removed_frac_q12 | +1.0498 | 1.0554 | 71.5% / 4.2% / 24.3% |
| since_capture_movement_queries | -0.8850 | 0.8850 | 0.0% / 76.2% / 23.8% |
| source_removed | -0.8411 | 0.8502 | 7.1% / 71.9% / 20.9% |
| since_capture_unit_progress_blocked | +0.8420 | 0.8447 | 71.6% / 2.1% / 26.3% |
| q128 | +0.7881 | 0.8396 | 55.0% / 22.4% / 22.7% |
| source_added | +0.8055 | 0.8079 | 71.9% / 1.8% / 26.3% |
| since_capture_reachable_no_move_frac_q12 | -0.6994 | 0.7034 | 2.4% / 71.2% / 26.4% |
| since_capture_reachable_no_move | +0.4231 | 0.4319 | 69.6% / 4.5% / 25.9% |
| age | +0.3718 | 0.3855 | 55.0% / 7.0% / 38.0% |
| source_added_frac_q12 | +0.3312 | 0.3362 | 64.1% / 3.9% / 32.1% |
| since_capture_empty_goal | +0.2633 | 0.2633 | 58.7% / 0.0% / 41.3% |
| since_capture_unit_progress_blocked_frac_q12 | -0.2320 | 0.2324 | 0.2% / 68.4% / 31.4% |
| since_capture_empty_goal_frac_q12 | -0.2084 | 0.2084 | 0.0% / 52.8% / 47.2% |
| since_capture_persistent_progress_blocked_frac_q12 | +0.1635 | 0.1659 | 49.7% / 1.3% / 49.0% |
| since_capture_persistent_progress_blocked | -0.1603 | 0.1646 | 2.4% / 51.9% / 45.7% |
| since_capture_sidestep_used | +0.0637 | 0.0933 | 23.3% / 13.0% / 63.6% |
| since_capture_sidestep_used_frac_q12 | +0.0273 | 0.0836 | 22.3% / 20.1% / 57.6% |

Count and corresponding fraction frequently point in opposite conditional directions. In particular, reachable-no-move count raises average score while its fraction lowers it; persistent-blocker count lowers average score while its fraction raises it. One overall plus/minus label per event would conceal that behavior.

## Coherent counter-history changes

Replace one existing non-event query with an event: eventcount+1, querycount fixed, matchingfraction recomputed. Only rows withQ>0 andeventcount<Q qualify; their original weights are renormalized. Other seven original features remain fixed, so this is conditional counter-history inference, not a real time transition.

| Counter change | Mean score change | Positive / negative response weight |
|---|---:|---:|
| since_capture_empty_goal+1,queriesfixed,matchingfractionrecomputed | +0.4725 | 59.8% / 38.7% |
| since_capture_persistent_progress_blocked+1,queriesfixed,matchingfractionrecomputed | +1.0277 | 66.1% / 33.1% |
| since_capture_unit_progress_blocked+1,queriesfixed,matchingfractionrecomputed | +0.0943 | 59.1% / 39.1% |
| since_capture_sidestep_used+1,queriesfixed,matchingfractionrecomputed | +0.1975 | 44.6% / 55.3% |
| since_capture_reachable_no_move+1,queriesfixed,matchingfractionrecomputed | +0.0926 | 45.1% / 54.7% |
| onecleanquery:queries+1,eventcountsunchanged,allfractionsrecomputed | -0.0132 | 18.4% / 72.5% |

Reachable-no-move and sidestep have positive mean changes despite more negative than positive responses: magnitudes and current state matter. New query without a contradiction usually lowers score. None of these signs establishes that congestion causes refresh benefit.

## Baseline7 prediction importance, with no refit

Independent feature permutations within each heldout map, five deterministic repeats, preserve map marginals and equal-format/map weights. They break feature correlations and temporal relationships, so these are inference perturbations rather than leave-feature-out retraining. Negative/small changes are not proof a feature is useless.

| Feature | Mean regret increase | Five-repeat range |
|---|---:|---:|
| q32 | +0.52800 | +0.48998..+0.55069 |
| q128 | +0.06855 | +0.04409..+0.09006 |
| source_added | +0.05802 | +0.04027..+0.07337 |
| source_removed_frac_q12 | +0.03314 | -0.00977..+0.05637 |
| age | +0.00550 | -0.01169..+0.02325 |
| source_added_frac_q12 | +0.00476 | -0.00862..+0.01656 |
| source_removed | -0.00326 | -0.01754..+0.00974 |

The strongest demand signal is q32 under both measures. Existing six new-event retraining ablations answer a different question; original-seven deletion ablations were not run. Do not treat sensitivity, permutation, and retraining rankings as interchangeable.

## Head coefficients in human input units

Count/age inputs and originalsourcefractionQ12 inputs use log2(1+raw). The five new event fractions use fraction0..1 directly. Probability columns are logit coefficients; mean columns are conditional movement-benefit magnitude coefficients. Each mean is clamped nonnegative before combining heads; probability uses the integer sigmoid table. The actual engine predictor still uses integerQ12 transforms, integercoefficients/shifts and truncation.

| Feature | Positive logit | Negative logit | Positive mean | Negative mean |
|---|---:|---:|---:|---:|
| age | +0.13164 | -0.03785 | -1.15083 | +0.25892 |
| q32 | +0.54932 | +0.12442 | +14.18149 | +1.59133 |
| q128 | +0.33067 | +0.41913 | -1.30772 | +0.40485 |
| source_added | +0.07473 | +0.06549 | +3.07724 | -0.22503 |
| source_removed | +0.08219 | +0.04134 | -7.86967 | -1.69762 |
| source_added_frac_q12 | +0.04197 | -0.10306 | +0.45977 | +1.30556 |
| source_removed_frac_q12 | +0.11775 | +0.10571 | +2.79897 | -0.13838 |
| since_capture_empty_goal | +0.32788 | -0.12968 | +1.00375 | -1.18988 |
| since_capture_persistent_progress_blocked | -0.06842 | +0.01313 | -4.40270 | -3.37740 |
| since_capture_unit_progress_blocked | +0.13753 | +0.12251 | +3.97440 | -0.48788 |
| since_capture_sidestep_used | +0.14132 | -0.05463 | -0.87772 | +1.50862 |
| since_capture_reachable_no_move | -0.01731 | -0.16153 | +13.93011 | +7.25630 |
| since_capture_movement_queries | -0.17764 | +0.04970 | -0.67996 | +0.54202 |
| since_capture_empty_goal_frac_q12 | -0.98301 | +1.25483 | -13.59142 | +5.31447 |
| since_capture_persistent_progress_blocked_frac_q12 | +1.94668 | +1.64705 | +46.47729 | +13.92668 |
| since_capture_unit_progress_blocked_frac_q12 | +0.22981 | +0.53180 | -14.65658 | +0.63940 |
| since_capture_sidestep_used_frac_q12 | +1.42175 | +0.63738 | -23.49807 | +0.92783 |
| since_capture_reachable_no_move_frac_q12 | -0.94639 | +0.30154 | -107.53567 | -18.86065 |
| Bias | -6.10581 | -6.15758 | +3.30454 | +6.01501 |

All findings remain provisional124: four missing largegames, only31heldout maps, sparse highlydemanded veryold fields, and previously inspected variant selection. No CPU or deployed-policy benefit claim.
