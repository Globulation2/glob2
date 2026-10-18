# Independent review of reconciliation controls

**Research conclusion approved with qualifications. Production changes are not approved by this review.** The experiments support concrete surrounding-policy differences; they do not establish a broad failure of the generic learned model.

## Source and observational-control verification

Compared the exact per-binary diffs and build metadata for instrumented final and preserve-turns. Same df253base, ordinary release command and audit-header SHA. Only variant source difference is10added lines in MapStep.cpp restoring visits to allocated, active, source-bearing natural ground fields and marking their round-robin turn consumed without propagating. No model, feature, probe, query or coefficient changes are hidden in this control. See preserve-turns-independent-review.json.

The negative-only plain variant changes only the MapResources.cpp scalar enrollment condition to require the current cell's field value<=GRADIENT_UNREACHABLE. Its combined variant additionally restores the same phantom turns. This leaves negative/unreachable-answer demand available; it is not the unsafe no-distance diagnostic.

Independently compared result ticks, final checksum and aggregate jobs for all6followup diagnostic/plain pairs: **PASS** (negative-followup/independent-equivalence.json). Earlier12pairs also match. This does not independently prove every per-tick checksum matches instrumentation on/off, but confirms the reported end-state/workload equivalence in these runs.

## What the River turn-preservation control establishes

- Periodic guard admissions11,175→2,842; periodic clear14,918→4,093.
- Guard+clear seed/propagation scope wall2.260644→0.726789s, a67.85% drop.
- Natural material seed+propagation scope wall0.555617→0.557156s, essentially unchanged(+0.28%).
- Final policy loop47.85ms versus8.539s measured simulation CPU, approximately0.56% in this instrumented one-thread run. The tiny query bookkeeping measured separately does not explain the whole regression.

Thus removing skipped natural rotation turns permits other periodic families to backfill the removed work. The profiler and deterministic category counts agree. These are scoped wall durations, not independently measured producer CPU, and the two complete games have different trajectories/ticks(28,738vs27,394). Do not claim an exact universal causal percentage or that every additional guard/clear refresh was useless. Historical resource-quality telemetry did not score the quality effects of slower guard/clear updates.

## Warmup and scalar-demand followup

The proper8v8control runs the same released-save state from32768to35840, excludes2,048warmup ticks from benchmark CPU, and enables audit only from34816. All model-triggered accepted admissions remain above32 in **both** old and final models. There are **zero final-only selected rows** in any of the six followup CSVs.

Even after2,048ticks, one previously idle field in each variant receives its first demand with submitted_age>32768. These three individual first-admission cases are explicitly separated; a fixed warmup cannot guarantee all dormant metadata has been touched.

|Accepted warm adaptive rows after separating that one first-demand row|Final|Negative-only scalar demand|Negative-only + preserved turns|
|---|---:|---:|---:|
|Cap admissions|966|556|590|
|Model admissions|13|17|10|
|Cap q32=0|535(55.38%)|100(17.99%)|116(19.66%)|
|Cap q32=q128=0|442(45.76%)|88(15.83%)|96(16.27%)|
|Actual final-only model selections|0|0|0|

In the full fresh River game, final has3,379cap+73model admissions; negative-only has2,126cap+24model. Distance calls actually enrolled by the final scalar block are2,029,005reachable and34,542unreachable. The negative-only version enrolls0reachable and23,827unreachable on its changed trajectory. This directly confirms that the broad final distance enrollment is dominated by positive answers and that restricting enrollment materially reduces capped work while preserving the negative-answer demand mechanism. It does not verify all recruitment behavior/quality under the proposed restriction.

The frozen512training corpus contains q128=0 in only432/476,107complete episodes(0.09074%raw,0.11761%balanced). Its movement-based eligibility hardly represents the runtime scalar-only cap population. However the hard cap bypasses the learned model anyway; retraining experts cannot suppress those forced cap admissions without a scheduling-policy change.

## Timing interpretation

Plain one-thread8v8 CPU/tick after2,048warmup: master6.3515ms; final7.3203; old coefficients7.3009; preserve-turns7.3718; no-distance6.7661; negative-only6.7627; negative-only+turns6.9449. These are single controlled samples on a short suffix, not a new performance tournament or uncertainty estimate. Preserving turns does not universally improve this workload. The actual old-coefficient control stays close to final here; full River changes more, so refit can still alter closed-loop outcomes.

Audit CSVs include the final observation boundary tick35840, while the benchmark records1,024measured simulation advances. Consequently do not treat audit counts/scoped wall as exactly the same endpoint-inclusive timing window or derive precise per-producer CPU shares from them. The additional boundary is immaterial to the qualitative count/cause result but should remain disclosed.

## Approved explanation

The earlier CPU gain and later mixed/regressing result are not the same experiment. Historical natural-field suppression also consumed idle rotation turns, whereas final integration backfills those turns with other periodic families. Final scalar distance requests expand the demanded field population and drive many128-tick capped rebuilds that the movement-trained model does not control. The new model is more conservative on observed frozen inputs and actual accepted runtime inputs here; it did not broadly become more eager or simply stop generalizing.

This supports investigating **separate refresh policies/cadences for other periodic families and scalar recruitment demand**, then measuring their quality and correctness. It does not justify automatically restoring all old behavior, dropping negative-demand liveness, adding generator-specific experts, or asserting CPU savings before a fresh matched quality/performance comparison.
