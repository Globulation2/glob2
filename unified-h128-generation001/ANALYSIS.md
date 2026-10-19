# Round001 terminal research analysis

The data and inference plumbing pass the read-only audit. This is a research candidate, not evidence of improved in-game CPU or pathfinding yet.

128 frozen exploration games contributed complete original episodes:53 finished and75 hit the30-minute cap. The model used31,427 known episodes from128 maps; development103 maps and held-out25 maps. Markets remain unmeasured by explicit campaign scope. Spatial comparisons use the identical21,918 eligible Materials/Clear rows, with3,717 held-out rows.

## Generic12 H128 model

| Held-out measure | Result |
|---|---:|
| Episodes/maps |5362/25|
| Decision regret at128 units |1.5114|
| MAE |6.1089|
| Actual benefit≥128, weighted |0.7567% (78 raw rows)|
| Model selections at128 |0%|
| Maximum predicted benefit |94.255 units|
| Positive benefit captured at128 |0%|

The same0% selection occurs in all3 development folds and the development refit. Development maximum score114.930 is also below128. About91.5% of weighted labels are zero; positive benefits occur in6.0% and benefits≥128 in0.57–0.76% across development/held-out. The exported model is therefore below the requested threshold on every observed state. Cold/reactive/history recovery remains active; the candidate must not be described as refreshing no fields at all. Scores on future older/unseen states may differ.

No factor-of1000 error was found: the training target is raw direction-cost difference; exported heads multiply by1000; inference returns milli-units; metrics divide by1000; native compares directly with128000. Independent floating evaluation of all4 exported heads agrees with integer inference within0.04453 units on held-out rows, far smaller than the threshold gap. The low scores are learned expected benefits under this target/distribution, not a rounding or unit mismatch. Sparse rewards and regularization are consistent with conservative scores; this audit did not refit alternatives or establish underfitting causally.

Regret is concentrated in Materials (family-conditioned5.6303), with Clear0.3999 and Guard/Forbidden0 at this threshold. Format slices are heterogeneous:8v8 has5.952 regret on2 held-out maps,4ffa2.406 on4,16ffa1.828 on2; these small grouped samples do not establish robust format rankings.1v1 has only1 held-out map.

## Spatial augmentation

All5 variants have identical128-threshold decisions and held-out regret3.01510 on the common Materials/Clear subset. All CV regrets tie, so deterministic development selection remains baseline12. This is not evidence that spatial features never help; the current threshold makes this decision metric unable to distinguish them.

Offline equal-selection-share ranking is mixed. At5% selected episode weight, positive benefit captured is41.72% baseline,43.70% counts+fractions and46.28% directional; at1%, baseline22.11% exceeds counts+fractions19.12%, while fractions alone reaches23.73%. At10%, fractions65.24% exceeds baseline62.39%, while directional reaches63.00%. These are exploratory held-out comparisons, not development-selected winners, actual refresh rates or CPU gains. MAE changes are small (11.069 baseline versus10.958–11.006 augmented). No spatial input promotion is justified by this run.

## Coverage and censoring limits

The capped games are retained, not dropped: they contribute complete H128 episodes with explicit unknown whole-game totals. Largest/late trajectories remain censored:19/20 of512×512,17/18 of256×512 and13/15 of512×256 hit the cap. By format,14/16 of16ffa and13/16 each of8ffa/8v8 were capped. Small maps mostly finished. Training weights balance format/role/cohort/map/family/collection/episode rather than rewarding more complete small-game rows.

High-demand old-field support is thin: withq128≥32, Materials has106 episodes aged≥512 and0 aged≥4096; Clear304 and0; Guard300 and1. Consequently the uncapped candidate can produce age distributions beyond strong current support. Its reserved128 prospective games are needed to measure that behavior and collect the next real data; this is a limit to disclose, not a reason to invent a hidden forced-refresh cap.

Stalls, invalid goals, missed moves, raw reachability and planning outcomes remain separate from direction regret. The reports contain their rates; none was folded into the label to make a model look better. Terminal export recovered original complete rows from all75 capped games. No fitting, policy changes or coefficient emission were performed by this analysis.

See readonly-inference-audit.json for exact head/range verification, CENSOR-SUPPORT.json for full format/size counts, and the immutable generic/spatial research JSON files for grouped metrics and provenance.
