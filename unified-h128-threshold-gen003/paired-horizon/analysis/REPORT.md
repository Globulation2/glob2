# Paired H32/H128 result: no empirical conversion near 32

**160 units is the constant-accrual arithmetic, not a measured equivalent threshold.** The paired natural-ground Materials sample contains 34 complete episodes across 3 mature heldout maps, but its short-horizon benefit never reaches 32: B32 ranges−8…14. There are **zero observations in either predeclared near-cutoff band [24,40] or [16,48]**, so the empirical conversion at 32 is not identifiable. No threshold change or model fit follows from this measurement.

Old usable capture offsets 8…31 contain 24 ticks; current 8…127 contain 120 ticks. Under a constant rate of signed benefit accrual, 32×120/24=160. Real benefit need not accrue steadily: query demand changes, early gains can cancel, and later gains can appear after a zero prefix. This study checks realized direction-cost labels from the same captures and counterfactual fields; it does not measure the old model’s expected prediction.

The old scheduler is also different. At git 72d2f99, ResourceGradientPolicy.h 17–18 declares minimum 8, maximum 128 and predicted-benefit threshold 32000 milli; line 65 refreshes a demanded eligible field when age>=128 **or** the model predicts at least 32 units. The current policy has no forced maximum age, per the user’s explicit change. Even a perfectly matched horizon-only benefit cutoff cannot reproduce the old scheduling frequency or quality. This is not a proposal to restore the cap, and old adaptive32 is not the fixed-period32 comparison arm. SOURCE-EVIDENCE-ADDENDUM.json and old-cap-addendum.txt preserve the exact source.

The closest common scope is natural ground Materials under the **current** oracle. Its short prefix uses current selectors/scoring and H128 episode cadence, rather than replaying the old 72d simulator or collector. All other families remain separate, and no pooled generic-family ratio is called an old-policy conversion.

## Paired natural-ground Materials

B32=defer_extra_32−refresh_extra_32 over offsets 8…31; B128 analogously over 8…127. Both are signed direction-cost units. Existing hierarchy weights balance format/role/cohort/map/family/collection/episode. Each of the three formats here has one map, so primary map weight is equal. These are overlapping counterfactual episodes, not whole-game benefit totals.

| Quantity | H32 prefix | H128 full |
|---|---:|---:|
| Weighted mean signed benefit |1.2980|2.9545|
| Weighted median benefit |0|0|
| Benefit p10…p90 |0…8|−12…22|
| Benefit range |−8…14|−40…62|
| Weighted mean query count |3.4066|16.3485|
| Weighted benefit per usable tick |0.05408|0.02462|
| Weighted sum benefit / weighted sum queries |0.38102|0.18072|
| Zero-query episode count |7|5|

There are 7 positive B32 labels, 26 zeros and 1 negative. Ratios B128/B32 are undefined for the 27 nonpositive denominators, so they are never zero-imputed or used to scale a threshold. Among the 7 positive-prefix pairs only, the weighted median ratio is 1.0, IQR 1.0…7.25, range−5.67…11. This sparse, sign-changing distribution is not a usable conversion rule.

Examples: one pair changes from +6 to −34; another grows from +14 to +62; a third stays +14 to +14. Five episodes have a negative late increment; four nonpositive prefixes develop positive full-horizon benefit. The labels show why multiplying every prefix by 5 is unsafe, but do not estimate behavior specifically around32.

## Map and family heterogeneity

| Natural Materials map | Paired rows | Mean B32 | Mean B128 | Mean Q32 | Mean Q128 |
|---|---:|---:|---:|---:|---:|
|4FFA / polder128×64|12|3.1667|14.5000|4.5833|19.5000|
|2v2 / islands64×128|11|0.1818|-3.2727|2.5455|11.3636|
|8FFA / fjord-continent128²|11|0.5455|-2.3636|3.0909|18.1818|

The late-benefit increment has weighted mean 1.6566. Exact paired whole-map resampling over all 10 multinomial count vectors gives a discrete 95% interval [−3.4545,11.3333]. With only 3 maps, this is essentially the range of map-level behavior, not strong population evidence. Near32 conditional quantiles and an empirical matched-cutoff interval are undefined because there is no supporting episode. Leave-one-map-out results are preserved in RESULTS.json and cannot repair the missing threshold support.

| Family (ground only observed) | Paired rows | Mean B32 | Mean B128 | B32>=32 rows |
|---|---:|---:|---:|---:|
|materials|34|1.2980|2.9545|0|
|markets|0|unmeasured|unmeasured|—|
|guard|19|0.0000|0.0000|0|
|clear|15|-0.2667|14.0000|0|
|forbidden|6|0.0000|0.0000|0|

Clear has large late variation (full labels −50…174), while the measured Guard and Forbidden direction labels are zero. Neither zero direction benefit nor missing Markets/AIR establishes that those fields need no refresh: other outcome types and unobserved states are outside this label-conversion question. No AIR pairs were observed.

## Oracle decision matching is uninformative here

The predeclared oracle target B32>=32 selects no primary episodes. Any integer full-label cutoff>=63 also selects none in this sample. Thus 128 and 160 both have zero measured oracle disagreement, **vacuously**. The data identify no unique cutoff, and 63 is not an estimated equivalent or a recommendation. An old expected-benefit model could make different decisions even when every realized short label in this sample is below 32; the old 128 age cap could also trigger refreshes.

## Integrity, censoring and limits

- Exact frozen map/warm-save/runtime/plan/input-lock/gate identities were verified. All 3 runs used the unchanged 128000 policy, advanced 1024 ticks from 16384 to 17408, completed without failures or wall caps, and cleared threshold override environment variables. No replacement was made for the predeclared 3FFA checkpoint that ended before the warm target.
- All 45 raw artifact hashes verified. Sidecar and original H128 CSV join one-to-one on run/tick/family/team/movement/material/key/serving-capture tick. Full sums, query counts and completion flags match exactly; short prefixes satisfy the same-delay/window/range checks. Collection also preserves byte-identical original four H128 CSVs, world/snapshot/replay vectors and final saves versus matching original 128 pilot runs.
- All 74 paired rows were complete, history-known and movement-labelled:29/24/21 by map. There were no short-only-complete, both-censored or unknown-history rows in these mature measurements. Separate qualification fixtures previously covered censoring boundaries; this report performs no new simulation tests.
- Per-query rates are undefined when queries=0. RESULTS.json retains these counts, conditional per-episode rates and the paired positive-query subset; the displayed pooled rate divides weighted benefit by weighted query count. Planning and other event outcomes are not added to this signed direction target.
- This is three surviving mature states and one short diagnostic continuation per map. It cannot identify a universal horizon conversion, reproduce old fitted-policy behavior or establish native CPU/quality effects. The current default model, contracts, horizon, scheduler and thresholds were unchanged; diagnostics are not training data.

![Paired labels and the unobserved cutoff region](paired-benefit.png)

![Map heterogeneity](map-heterogeneity.png)

PLAN.json / INPUTS-LOCK.json retain the pre-outcome protocol. RAW-INPUTS-LOCK.json records hashes and joins; RESULTS.json contains full distributions, signs, ratios, rate denominators, family/movement coverage, fixed-cutoff confusion, discrete map uncertainty and leave-one-map-out summaries. paired-rows.csv is the exact joined analysis table. No additional games, refit or threshold promotion were performed.
