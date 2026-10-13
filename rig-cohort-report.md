# Fixed independent rig gameplay cohort

All1200attempts finished:600original completed and599cached-material completed, with one cached game OOM-killed and retained without replacement. The fixed cohort did not receive the separate Nicowar repair in PR1170. Successful-match comparisons are conditional on completion; the policy-specific missing outcome prevents clean gameplay-equivalence acceptance.

128×128 river/islands/swamp maps, eight players, four approved mixes (8Nicowar;4Nicowar+4Numbi;4Nicowar+4Castor;2each Nicowar/Numbi/Castor/Maxima),50attempts per policy per generator/mix. All1200map/game seeds are distinct. Inputs were generated before balanced randomized policy assignment; game trajectories are independent across arms, not paired. Players are shuffled across seats. Normal initial stocks/food/demand; natural ending or32768tick cap; zero warmup. Four compute participants per process, four concurrent games.

Cached policy is the isolated material-only consumer:K4/R4, available-only combined lists, observed vacancies+2, round coordination where supported;128is the ceiling rather than the retained suggestion count. Original hiring rounds, quotas, clocks, live admission, subscription and swaps remain. Attraction/flying-carrier search remains original. Production games retain original hiring.

## Primary completed-match results

| Endpoint | Cached versus original | Individual95% match-bootstrap interval | Holm-adjusted p |
|---|---:|---:|---:|
| Starvation deaths / unit-ticks |−3.50%|−10.51% to+4.19%|0.7011|
| Critical-hunger unit-ticks / unit-ticks |−1.75%|−4.38% to+0.99%|0.6152|
| New buildings / worker-ticks |−0.36%|−2.29% to+1.64%|0.7207|

Whole-match numerator/exposure tuples are jointly resampled within arm×generator/mix for10000bootstrap draws. Policy-label randomization uses10000within-stratum shuffles; the three primary tests use Holm correction. Teams and unit-ticks are not independent samples. Individual intervals are not simultaneous family-wise intervals. None of the three corrected tests detects an overall difference. Nonsignificance does not prove equivalence; the missing cached outcome adds completion-selection uncertainty. Construction rate is not construction-completion or staffing-recovery latency.

## Descriptive outcomes and cautions

Original→cached successful totals:22305→21454starvation deaths;81585→80960new buildings;2474→2496lost colonies. Arm sizes differ600/599, so raw totals alone are not treatment rates. Deliveries/worker-tick−0.30%;harvest/worker-tick+1.18%;births/unit-tick+1.11%. Recorded idle-worker share9.45%→10.36%(+0.91percentage points); filling share58.87%→57.84%(−1.03points); food-seeking/feeding share16.10%→15.92%(−0.18points). Labour-counter denominators are distinct from exact unit-sync exposures.

Natural finishes before the cap:47/600(7.83%)→67/599(11.19%). Mean end tick32474→32303. Most games remain capped; late-game balance and eventual winner distributions are not established. A supplemental post-result exploratory analysis tests 15 secondary endpoints and all 36 inspected subgroup endpoints using 10,000 within-stratum whole-match label permutations, with Holm correction across all 51 tests. Idle and filling shares retain adjusted p=0.00510 each. Early finishes have raw p=0.0403 but adjusted p=1; harvest has raw p=0.0169 but adjusted p=0.828. No other secondary or subgroup test is significant after correction. The river/four-AI starvation test has raw p=0.3137 and adjusted p=1. This analysis was added after results were inspected; it supports an idle/filling signal requiring independent confirmation, not a retrospective confirmatory claim. Completed-case selection remains a limitation. The script and complete results are retained separately from the unchanged primary analysis.

River/four-AI starvation is the largest unfavorable subgroup point estimate:+47.43%(602→851deaths over342484881→328397103unit-ticks,50games per arm). A supplemental, post-primary, unadjusted subgroup match bootstrap gives a very wide ratio interval0.761–2.734(−23.94% to+173.42%). This is an exploratory descriptive interval, not a preregistered subgroup test or simultaneous coverage across36subgroup endpoints. It neither establishes harm nor dismisses that group. All12subgroup results and intervals are retained, including unfavorable outcomes; targeted independent follow-up is warranted.

## Failure and provenance

Cached river-nicowar-019 was OOM-killed at107257084KiB anonymous RSS, last reported tick15360. Bounded diagnostics locate std::bad_alloc in existing Nicowar dig_out_enemy, not planner allocation. Original hiring completes the same input under4GiB. A separate fix PR1170 rejects incomplete routes before creating orders; repaired isolated runs complete32768ticks and match across Mac/Linux and1/4participants. They do not replace the cohort failure. Exact original loop state was not inspected; the suspected path-stall/cycle mechanism is supported by source and focused regression cases, not claimed as a captured state from that failed game.

Source c2fa28a103ed0726c57b25770313bc47df3afbb9;runtime source e3e56387f6008dccc6bb4dd1fc165178d40ead1a;Linuxx86-64 GCC15.2 ordinary release. Twenty-four preflight runs validate observer/reference traces and cached1/4participant identity over six8192tick cases. Final integrity audit validates input hashes, all attempts, balanced allocation, complete timelines and cumulative/exposure bounds. No simulation source changed during the fixed cohort.

Protocol SHA941a77adc5701821b4f44db80b7da0dedaa9a173ecc1e4ae07df87b73a5d2dac;manifest SHAaa44f00e9165ee2ed4476845bce23fbb1e95a60bd0211362818b93d79e063402. Protocol and manifest were published before results. Input archive and separate failure/debugger archive remain in this evidence branch. Concatenate the two result archive parts, verify the SHA in rig-cohort-result-archive.json, then extract; per-file hashes are in rig-cohort-result-file-manifest.json. It contains all attempts, successful raw results,512tick telemetry, errors, scripts/commands/provenance and host activity. The original auto-generated report is retained unchanged inside the archive; this curated report makes completion-selection and exploratory-subgroup limits explicit.

## Recommendation

This larger128-map cohort does not reproduce an overall starvation penalty on the primary completed-match measures. It does not prove the earlier256-map screen was noise, certify every subgroup, or qualify a rollout. Repair Nicowar separately, confirm on an independent post-fix cohort, and investigate the idle-time increase and river/four-AI uncertainty. Keep the experiment disabled by default and the PR draft. Eight-tick actual recruitment-delay, completion/recovery-time, live persistence/admission and manual gameplay gates remain unestablished.

Concurrent rig activity included unrelated builds and the separate repair validation. These runtimes are not an authoritative performance delta. Prior quiet-host performance evidence remains separate. This cohort is a capped Linux gameplay experiment, not full-match, general cross-platform or live save/replay/network validation.
