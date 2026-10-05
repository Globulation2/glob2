# Independent full-game ecology assessment

All 96 baseline/candidate pairs completed (24 frozen maps, four seed/AI-seat configurations). Setup and telemetry audits found no mismatched inputs, decreasing cumulative counters, inconsistent duplicated global growth, or violations of nested local growth coverage. These checks establish measurement consistency, not spatial crop containment or balance equivalence.

## Resource supply and overall pacing

At the primary tick-8192 horizon (all 96 pairs), global net natural regeneration changes were wheat +0.17%, wood +0.13%, algae +0.49%; each paired 95% interval includes zero. Local radius-16 wheat was +1.12% and wood +1.18%, also unresolved. Wheat harvesting/delivery were -1.51%/-1.56% with intervals including zero. Wheat delivered per 1000 worker-ticks fell 1.47% (1.8749 to 1.8472; difference CI -0.0468 to -0.00675). This small early efficiency change merits reporting, but does not persist at later pooled horizons.

At each game's last common observed sample, wheat delivery was +0.54%, local radius-16 wheat regeneration +0.48%, and starvation rate -2.4%; all intervals include zero. Capped game duration averaged 47,415 versus 47,143 ticks (-0.57%; difference CI -3,286 to +2,539). Natural endings were 63 versus 69 of 96; the difference is unresolved. These are capped durations, not uncensored natural game lengths.

Fruit regeneration involves low event counts and wide relative swings. Radius-16 cherry net regeneration at8192 fell 0.1099 to0.0868 amount/team/1000ticks, an unadjusted resolved decrease of0.0231, but fruit delivery did not show a resolved loss. Papyrus has zero growth/harvest observations here; this corpus supplies no papyrus gameplay coverage. Stone is non-growing as expected.

## Substantiated midgame warning

The same 63 pairs are observed through tick32768. Between512 and32768, starvation deaths increase765 to1146 (+381), while sampled unit exposure increases270.79M to277.17M (+2.35%). Rates increase2.825 to4.135 deaths per million unit-ticks (+46.4%; difference CI +0.110 to +2.632), accompanied by hungry exposure14.85% to15.86% (+1.01 percentage points; CI +0.126 to +1.754). Population at32768 differs only+0.86% (unresolved).

This is not explained by worker/non-worker composition: holding the baseline worker exposure share fixed gives2.825 to4.127 deaths/M (difference CI +0.078 to +2.613). Worker deaths increase315 to592 while exact worker exposure increases168.19M to170.87M. Non-worker exposure here is sampled total exposure minus exact worker exposure; separate explorer/warrior standardization is unavailable from the retained condensed telemetry.

Keeping those same63 games and restricting to16384–32768 gives3.293 to4.952 starvation deaths/M, hungry exposure15.94% to17.18%, but wheat/worker delivery0.7449 to0.7489 (unresolved). Even the fixed18 pairs observed all the way to65536 show a midgame increase1.886 to4.806/M (difference CI +0.245 to +5.848). Thus changing survivor membership alone does not explain the warning. In that same18-pair cohort, the later32768–65536 rate is9.624 versus8.477/M (difference CI -3.715 to +1.169). The data do not establish a sustained overall starvation increase.

Observed AI-team deaths through32768 are Maxima189→481, Cortex189→301, Nicowar175→159, Cabino212→205. Maxima's pooled rate rises2.125→5.320/M (unadjusted difference CI +0.114 to +6.700). These are exploratory AI strata, and AI seats are linked to fixed game seeds.

Three maps dominate the extra deaths: canals-2857 +150, last-treeline-1427 +109, isles-2857 +97. Removing any single map leaves a positive pooled rate difference (+0.84 to +1.47/M); this sensitivity check does not assert significance after removal.

## Concrete follow-up cases

- canals-2857/seed19/Maxima: at32768, local radius-16 wheat net growth is2902 versus2900 and delivered wheat2301 versus2317, but starvation12 versus162 and no-inn worker-ticks201,396 versus1,061,388. Candidate growth was much faster by16384 (117 versus57 units), with starvation beginning before significant food-building destruction. This points toward timing/feeding access or allocation pressure, not measured crop-regeneration depletion; telemetry does not locate the responsible units/inns.
- isles-2857/seed19/Maxima: at32768, candidate local wheat growth and deliveries are higher, but starvation8→94 and no-inn ticks363,408→977,744, with fewer food buildings19→16. Combat and settlement progression differ too.
- last-treeline-1427/seed101/Cortex: candidate has one food building versus two; starvation1→52 by32768 and7→164 by65024, without food-building destruction. This specific run has a sustained food-service problem despite only modest local-growth differences. It must not be hidden by pooled late-game averages.

These cases warrant inspection of settlement placement, actual inn reachability/capacity, spawning versus meal allocation, and repeat paired runs with additional independently chosen seeds. No ecology coefficient change is justified by these observations alone; no source tuning was performed.

## Interpretation limits

Intervals use2000 paired map-cluster bootstrap replicates and are unadjusted for exploration across many resources, horizons and subgroups. Borderline intervals are evidence to investigate, not a family-wise significance claim. The24 chosen maps are an empirical coverage set, not a random population sample. Later fixed horizons condition on both games surviving; last-common horizons depend on termination. Snapshot hunger/total exposure is trapezoidal at512-tick samples, while worker exposure uses exact counters. Critical hunger includes hungry units with combat damage.

The logs do not contain resource cell coordinates, a full habitat inventory, inn/worker paths, or per-unit meal-denial reasons. Nested local≤global growth is consistent, but physical food containment cannot be proven here. Individual trajectories, winners and RNG sequences were never required to match. The isolated ecology calibration and expected-probability oracles are separate evidence; they do not erase this observed midgame gameplay warning.

Artifacts: report.json/report.md and metrics.csv contain the primary analysis; paired-metrics.json preserves numerators/denominators. posthoc.json, investigate.json and outlier-timelines.json preserve cohort, AI, composition and individual-case follow-ups, generated by the adjacent analysis scripts.
