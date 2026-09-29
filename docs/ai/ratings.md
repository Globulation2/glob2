# AI ratings

The custom-game selector shows measured relative AI strength, sorted weakest first, with inactive last. Difficulty bands are below 1450 Easy, 1450–1699 Medium, and 1700+ Hard. Saved AI identities do not depend on this ordering.

The current ratings measure source `d37c0c353`, including Maxima fixes #367/#368. They do **not** measure the newer Maxima changes in #370/#371. They use 19,948 successful games in 9,974 complete swapped-side pairs, from a planned 20,000-game tournament across 60 compatible generators. Previous-version outcomes are kept separate.

| AI | Elo | 95% paired-bootstrap interval |
| --- | ---: | ---: |
| Maxima | 1873 | 1858–1889 |
| Cabino | 1680 | 1668–1693 |
| Nicowar | 1653 | 1641–1665 |
| Cortex | 1601 | 1588–1612 |
| Warrush | 1401 | 1388–1413 |
| Econo | 1310 | 1298–1322 |
| Castor | 1280 | 1268–1292 |
| Numbi | 1204 | 1189–1218 |

Games are 128×128 duels with five candidate map rolls, a 90,000-tick cap, prestige adjudication and probability victory disabled. Rice Terraces uses `slant=0`; other controls use defaults. The compatible pool excludes Emoji, Gauntlet, Comb, Encircled Kingdom, Faulted City and Bastion Keys. Uniform is editor-only; Last Treeline was excluded to retain the previous cohort’s map pool.

The final fit excludes 48 invalid-map results and two games that repeatedly exhausted memory, plus their two successful counterparts to preserve side balance. Failed games are not losses. No replacements were run. Generator counts range from 286 to 334; this missing-result selection bias is not covered by the uncertainty intervals.

Every retained game has equal weight in an unregularized Bradley–Terry fit, centred at 1500. Draws score 0.5 and 400 points represent tenfold odds. Intervals refit 1,000 resamples of whole pairs within generators, seed 1. Input order has no effect. Pooling platform builds does not establish cross-platform simulation checksum equivalence.

[Outcomes, summary, exclusion audit and reproduction scripts](https://github.com/Globulation2/glob2/blob/evidence/ai-ratings-final-20260920/docs/validation/ai-elo-maxima-d37c0c353-20000-20260920) are preserved on the dedicated evidence branch, along with [previous-cohort evidence](https://github.com/Globulation2/glob2/blob/evidence/ai-ratings-final-20260920/docs/validation/ai-elo-20000-20260920). Do not combine results across AI revisions.
