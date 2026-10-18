# Independent no-cap research review

**Research results verified; retaining the production128cap is justified pending additional evidence.** This review does not approve a new production policy or estimate general performance from two states.

`review_no_cap.py` independently rebuilds all sixteen reader histograms and checks their exact read-weighted counts,p50/p90/p99,max,threshold fractions,changed/unchanged partitions and unknown counters. It recomputes scalar-oracle classifications and sampled movement metrics from CSV. It independently compares result ticks, final checksums and jobs in all four diagnostic/plain pairs. Result: **PASS**, recorded in no-cap-independent-checks.json.

Source metadata verifies the no-cap control keeps the minimum8-tick spacing, unchanged score threshold32000, the same final coefficients, reactive material FIFO behavior and negative-only scalar demand. Its only additional policy change removes the forced128admission branch. Diagnostic capped/uncapped builds use the same audit header and ordinary release flags. The observer uses private fresh reference computation; its on/off final-state/workload equivalence passes.

## Confirmed observations

Plain late8v8 simulation CPU/tick7.0716→6.8325ms(−3.38%) over a2,048-tick timed suffix following4,096warmup. These are single samples, not a confidence interval. Aggregate gradient job totals include the entire6,144-tick executed prefix; do not divide them by only2,048measured ticks.

Known-age movement-read p50/p90/p99 grows79/163/269→156/3693/5689. This is weighted by **each actual read**, not equally by field or simulation tick. Positive planning p99 grows 889→5651; negative planning p50/p90/p9979/177/1508→1608/4768/5829. The cap is on demanded time since submission; serving capture age can exceed128 due idle periods, delay and queued work, so capped tails above128 are not automatically a broken implementation.

The sampled movement oracle reports extra direction cost/query1.7361→4.3903 and flagged decisions14.903%→35.731%. These879/848observations are from deterministic sampled field keys in the permanent collector's window9, ticks36864..37887, **only the first1,024ticks of the2,048-tick benchmark interval**. Exact-age histograms cover the full audit-gated suffix. These are separate populations and should not be conflated.

River uncapped negative planning read age p50/p90/p99=1762/15832/17587,max17953, versus67/129/984,max7363 capped. Scalar oracle finds15false-unreachable answers among30sampled negative answers uncapped, versus0/17 capped. The15false negatives span **two distinct fields and ten captures**, not fifteen independent maps or fields. They are sampled first scalar reads of1/8keys at deterministic128-tick phases;50% is a description of those samples, not a calibrated population-wide recruitment failure rate. The late8v8 negative scalar samples are only3per policy(1/3vs2/3false negatives), too small for a rate claim.

## Unknown ages and instrumentation boundary

River has no excluded unknown-age reads. Uncapped8v8excludes57movement reads(0.2927%),1556positive-planning reads(0.1310%),zero negative-planning reads and one warm refresh. The instrumentation uses `found policy field && serving.sources.count>0` as its known-age predicate. Consequently unknown means missing metadata **or zero-source capture**, not proven exclusively legacy metadata. A valid capture can have zero sources. The exclusions are small and disclosed, so they do not explain the large known-age tail; they should not be relabelled as guaranteed legacy epochs.

Source-change splits test global positive-stock tile epochs, not whether anything relevant to this particular route changed. They are useful descriptive partitions but not direct causal utility labels. The scalar oracle tests fresh natural-field reachability at the same actual planning point, while movement regret uses the same actual state/RNG; neither rolls out counterfactual games.

## Interpretation

The cap removal saves a little CPU in the late8v8sample while causing much older guidance and materially worse sampled movement quality. River also exhibits genuine stale negative planning answers. These measurements support keeping the cap until the model and training task explicitly cover that regime.

The related training-support audit finds no frozen512episodes older than4096 and **zero episodes with q128≥8 and age>256**. The uncapped runtime reaches multiple thousands of ticks with recent demand. That is a real joint-support gap; heldout errors under capped collection cannot certify uncapped deployment. Negative recruitment liveness additionally lies outside the movement-regret label. More data/appropriate outcomes and policy-aware validation are required before drawing conclusions about an alternative model family.
