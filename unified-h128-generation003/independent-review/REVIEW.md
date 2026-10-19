# Independent cumulative512 generic12 review

No technical blocker found for a research-only gen003 coefficient iteration. This is an inference/data-integrity review, not native qualification or evidence of better gameplay or stable-promotion readiness. No fit, emission, source edit or game was launched by this review.

The existing generic stage succeeded with model SHA `7ea3871b3a575745edb822fb319c62ac0ad81fdd21282e20b91d8e75fa70c85f`. Every sealed input file and stage output hash checked; terminal-input SHA is `f78be1b7fdabd8eecce00fb74b853afe6c909a390086edff82d7dce4d0d3fa67`. The artifact-only pre-fit path rebasing repair was independently verified against the exact prior input proof and all six inherited manifest identities/hashes, preserving accounting bytes. All 17,137 historical registry assignments remain unchanged.

Strict loading produced 125,662 complete episode rows and 122,901 eligible rows; 2,761 unknown-history or movement-label-unavailable rows were excluded rather than imputed. Split: 392 development maps / 120 heldout maps, with 27,669 identical heldout rows for both models. Both use the same equal format/role/cohort/map/family/collection/episode weights. Each displayed subgroup recomputes that hierarchy within its population; subgroup metrics are not additive.

| Same new heldout rows | Gen002 | Cumulative512 refit |
|---|---:|---:|
| Decision regret at 128 | 2.653688 | 2.652598 |
| Raw refresh selections | 25 | 23 |
| Weighted selection percent | 0.04689% | 0.04537% |
| Selected actual mean benefit | -69.14 | -73.32 |
| MAE in direction-cost units | 9.22320 | 9.18064 |

Refit regret is only 0.041% lower. Never refreshing scores 2.561250; both models remain worse on this decision metric. No uncertainty interval was estimated, and 23 versus 25 selected rows is too sparse to establish a meaningful improvement. Refit development-fold regrets were 1.242072 / 1.583453 / 1.256423; no new selection or tuning was performed in this review.

Near-threshold calibration remains poor: refit predictions in [128,160) units cover only 14 raw heldout rows and have weighted actual mean -16.20; its 9 predictions at >=160 have actual mean -143.98. There are 556 actually >=128 rows (1.1316% weighted), yet ordinary predicted admissions remain rare. Predictions are signed milli direction-cost units; labels are raw defer_extra minus refresh_extra direction-cost units; threshold is exactly 128000 milli. The horizon remains capture offsets 8 through 127 inclusive. Exact generic12 feature/transformation/contract checks passed; all four accumulator bounds for both models remain below 2^61 (refit maximum 12,602,456,314,884,500). Minimum interval8, publication delay8 and no forced agecap remain the declared policy.

Coverage is uneven. Heldout threshold-positive rows: Materials493, Clear60, Guard3, Forbidden0; Markets has no measured rows. Refit slightly improves Materials regret (9.601960 vs9.609932), slightly worsens Clear (0.719516 vs0.715902), and changes neither Guard nor Forbidden. All eight formats are represented, with mixed small changes. High demand q128>=32 contains329 of556 tail rows; age4096+ has only12 tail rows. These are support limits, not evidence that missing families or old fields are safe.

All512 declared slots are accounted:202 normally completed,310 user-capped. Complete horizons from capped trajectories are retained (14,861 of27,669 heldout rows); incomplete horizons are not completed with zeros. These are overlapping counterfactual episode observations, not independent games or whole-game outcome estimates. The large capped share and policy-dependent sampling limit generalization.

`REVIEW.json` contains exact hashes, bounds, same-row metrics, calibration bins, coverage and family/format/role/cohort/status/age/demand slices. `review.py`, `PLAN.json` and `REVIEW-PREPARED.json` record the inference-only method. Default12 feature/model paths were left unchanged; the separate spatial fit was not needed for this review.
