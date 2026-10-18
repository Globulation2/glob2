# PROVISIONAL124 contradiction-feature comparison

Counts plus fractions show a modest incremental prediction benefit on this dataset. This is promising provisional evidence, with uneven generalization; it does not establish improved engine CPU usage or pathfinding under a deployed policy. No model was promoted.

The dataset contains124 of128 games,186479 eligible movement episodes, and31 heldout maps (40416 eligible heldout episodes). Every variant uses identical rows, role/cohort/map weights, and frozen splits. Four absent games are all512×512: concrete-islands16FFA, isles16FFA, swamp16FFA, concrete-islands8FFA. The completion subset underrepresents expensive large/late trajectories. Full128 refitting remains required.

## Offline scores

Regret measures threshold32 opportunity cost in movement-direction benefit units; lower is better. Selection is an offline episode decision rate, not an observed refresh frequency or engine compute cost. Baseline7 is refitted on the new development data; the incumbent row uses unchanged production coefficients on exactly the same heldout rows.

| Model | Mean CV regret | Heldout regret | Heldout MAE | Selected episodes |
|---|---:|---:|---:|---:|
| Existing incumbent7 | — | 1.191355 | 5.1717 | 1.977% |
| baseline7 | 1.166893 | 1.174718 | 4.8914 | 1.885% |
| counts | 1.145418 | 1.183836 | 4.7980 | 1.986% |
| fractions | 1.118565 | 1.187825 | 4.9077 | 1.916% |
| counts_fractions | 1.127202 | 1.130388 | 4.7909 | 1.935% |
| fractions_log | 1.135530 | 1.155445 | 4.8421 | 2.012% |

Counts+fractions lowers heldout regret3.77% against refittedbaseline7 and5.12% against incumbent7. Its CV regret improves in all three folds (mean3.40%). Mean absolute error falls4.8914→4.7909 against refittedbaseline7; positive benefit captured rises34.35%→36.23%.

Ranking is unstable: fractions-only is best among the five main variants in development CV, but its heldout regret worsens1.12%. Across all17 variants, the two best CV scores are fractions-only and full-minus-querycount; counts+fractions ranks fifth in CV and first on heldout. Do not select a final candidate because it won this inspected holdout.

## What the counters add

Adding reachable-no-move count alone gives the largest heldout gain (regret−2.62%), but slightly worsens mean CV. Empty-goal count alone improves heldout regret2.04% and mean CV, a more consistent standalone signal. Persistent-blocker count has little standalone gain; removing it from the full counts+fractions model loses the most heldout benefit, but improves CV. These conditional importance differences are unstable, not causal attribution.

Unit-blocker count alone worsens heldout regret1.89%; dropping it from the full model barely changes heldout regret. Frequent congestion flags do not imply refreshing helps. Counter fractions, history exposure and raw counts can share information with demand/age; addition and deletion tests retrain correlated predictors.

## Heterogeneity and support

Counts+fractions improves5/8 heldout formats:16FFA−12.8%,2v2−10.4%,4FFA−15.5%,4v4−1.2%,8v8−3.6%; worsens8FFA+5.3%,1v1+2.2%,3FFA+0.6%. There are only3–4 heldout maps per format.

The512×512 subset worsens3.0% and contains only3 heldout maps;128×128 worsens17.2% on3maps. Improvements include128×256 and256×512, but neither map size nor generator should be declared generally better: many generator slices contain only one map. Full JSON includes every subgroup rather than only favorable ones.

Eligible support contains6394 episodes with age≥128 and q128≥32, but only945 with age≥512 and q128≥32. Heldout has985 and137 respectively. Neither eligible population nor heldout has examples combining age≥1024 with q128≥128, or age≥4096 with q128≥32. Extremely old fields are mostly idle/low-demand; this fit cannot validate highly demanded veryold states.

## Separate stall and planning labels

On shared heldout query horizons, deferred-field no-move labels total6142/136516 queries (4.499%) versus4750 (3.479%) for the reference refreshed field. That is a22.7% reduction in these fixed-trajectory oracle labels, not a measured improvement from deploying any fitted model.

Planning false-positive reachability labels fall208150→81629, while false-negative labels rise44808→54893, across6667952 planning queries. These planning outcomes are separate from the movement-cost target. Their mixed changes must not be presented as uniformly better planning, or as effects of a fitted policy.

Use live_event_totals for unsampled game rates; since-capture histories overlap and must not be summed as actual movement totals.

## Integrity and scope

Frozen f65 tools/tests snapshot verified against843 Git blobs; unchanged production modelSHAf954f80f..., map split registrySHA15c6670f.... Prior126 incumbent holdouts remain excluded. No C++ coefficient/header emission, split rerandomization, simulation change, or automatic promotion. Only Linux x86_64 telemetry contributed to this provisional fit. Future Mac pooling still requires qualified common source/model/measurement contract.

Artifacts: comparison.json (all17 fits/metrics/heads), provisional-summary.json (rankings/slices/support), incumbent-same-heldout.json (unchanged coefficients), provenance.json (four missing games), exploratory-paired-map-bootstrap.json (fixed-model map uncertainty).

## Exploratory paired-map uncertainty

10000 paired whole-map bootstrap resamples withinformat (seed713), fixed models, reproduce the reported baseline1.1747184657 and augmented1.1303877384 point scores within1e-9. Estimated delta is−0.04433; exploratory95% percentile interval[−0.08472,−0.00635] (approximately0.54%–7.21% lower regret relative to baseline). This interval excludes model-training uncertainty and does not adjust for inspecting17variants or the four missing largegames. It is not evidence of an equivalent CPU/pathfinding-policy gain.
