# Provisional124-game contradiction-feature comparison

This is an exploratory comparison on124 completed games from the fixed128-game plan. The four missing games are all512x512 (three16-player FFA and one8-player FFA), so completion selection biases large/late-game coverage. It is not the final balanced128 result. Source f65c8d1698f6deefc4820209e69ad704f91d5776; exact command, frozen manifest/hash, missing slots and split-registry hash in provenance.json. Production coefficients and policy remain unchanged.

262203 complete episodes yielded186479 eligible known-capture movement rows. Every variant uses identical eligible rows, weights and persistent grouped splits; the holdout contains40416 rows from31 maps.17 feature variants were explored. Refitted baseline7 is distinct from existing production coefficients. Lower decision regret at threshold32 is better; this is an offline label-based proxy, not measured engine CPU or pathfinding improvement.

| Model | Held-out regret | Change versus refitted baseline7 |
| --- | ---: | ---: |
| Existing production coefficients |1.19136|+1.42%|
| Refitted baseline7 |1.17472|reference|
| Counts |1.18384|+0.78%|
| Fractions |1.18782|+1.12%|
| Counts and fractions |1.13039|−3.77%|
| Fractions and log counts |1.15545|−1.64%|

Counts+fractions improves all three development CV folds and five of eight held-out formats, but8FFA,1v1 and3FFA regress; the512x512 slice is3.0% worse across only three held-out maps. Fractions-only ranks best among the main variants in development CV but is worse on holdout. These differences make model selection provisional; inspecting17 heldout variants is exploratory, not independent confirmation.

Empty goals and reachable-no-move are promising standalone signals. Persistent-blocker importance depends on the other features and split. Unit occupancy is common, but frequency does not establish refresh value. High-demand veryold fields remain scarce despite many old low-demand episodes. Future stalls/planning outcomes are separate labels, not improvements measured from deploying these models. Full128 refitting and prospective matched-game CPU/quality tests are still required before promotion.

Full comparison.json includes fitted research models, development folds, heldout metrics, feature ablations, grouped slices and separate outcomes. summary.json includes incumbent evaluation and explicit support/limitations. No production header is emitted. The immutable compressed episode dataset is retained onRig at the path recorded in provenance; this checkpoint publishes analysis/provenance, not the complete training corpus.

Additional analysis: summary.md and exploratory-paired-map-bootstrap.json record paired whole-map bootstrap uncertainty (10000 resamples within format, fixed models). Regret difference−0.04433 has exploratory95%percentile interval[−0.08472,−0.00635]. This excludes training uncertainty, selection among17 variants and missing-game bias. It is not independent confirmation. coefficient-directions-and-importance.md/.json report conditional local sensitivities, paired count/fraction history changes, baseline within-map permutations and all four fitted component coefficients. Every40416 heldout prediction in the vectorized analysis matches permanent integer inference. These sensitivity/permutation/deletion measures are distinct; none establishes causation. Artifact-only analysis scripts are included; no fitting, production changes or split changes occurred during these analyses.
