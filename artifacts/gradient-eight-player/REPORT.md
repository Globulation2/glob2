# Eight-player gradient validation — gameplay results

Frozen baseline 37ffbcb05 versus candidate 9e62e9c75. Protocol: PROTOCOL.md. 512 attempted games, 255 completed matched pairs and one pair with failures in both versions. The failed Nicowar pair (family 15, seed 10508) remains in gameplay-summary.json; completed-game estimates below are conditional on completion and do not substitute the separately fixed diagnostic run.

All eight teams are aggregated within each game. Intervals use 10,000 hierarchical bootstrap draws over map families and then paired seeds, equally weighting families. Players and repeated timing runs are not independent gameplay observations.

| AI | Metric | Estimate | 95% interval |
|---|---|---:|---:|
| maxima | common_wheat (percent change) | 1.94 | [-1.14, 4.92] |
| maxima | common_meals (percent change) | 2.11 | [-1.18, 5.17] |
| maxima | common_starved (per million unit-ticks) | -0.23 | [-0.85, 0.31] |
| maxima | common_critical_ticks (percentage points) | 0.05 | [-0.29, 0.34] |
| nicowar | common_wheat (percent change) | 0.22 | [-3.10, 3.78] |
| nicowar | common_meals (percent change) | 0.02 | [-3.71, 3.85] |
| nicowar | common_starved (per million unit-ticks) | 0.18 | [-0.67, 1.10] |
| nicowar | common_critical_ticks (percentage points) | 0.08 | [-0.26, 0.42] |

Both AI configurations clear the prospective −5% lower-bound guardrail for wheat and meals in the completed-game analysis. Neither shows a statistically clear improvement. Starvation and critical hunger intervals include zero; that is not proof of equivalence, and no numeric noninferiority margin was specified for these outcomes. All completed pairs also survive the early 16,384-tick window; early wheat/meal intervals include zero.

Exploratory map checks: Canals Nicowar (eight pairs) total wheat 109415→121066, meals 33729→37594, unit-time 254733312→280559104, starved 1397→1155. Plantations Maxima: wheat 351874→357412, meals 45542→49272, unit-time 784656896→820455936, starved 27614→28448. Plantations starvation per unit-time falls slightly even though raw deaths rise. These totals are descriptive, not multiplicity-adjusted subgroup claims. Earlier two-player results are separate evidence and are not pooled.

CPU measurements remain in progress. No final CPU conclusion yet. Both machines are benchmarking the same 64 source states (both baseline- and candidate-origin), in two disjoint batches, four randomized paired repetitions per state. Extraction and transfers were separated from timing. Full trajectories may differ after loading identical states, so CPU changes can include changed AI workload as well as direct gradient savings.

Nicowar runaway-memory fix is a separate branch, excluded from the frozen cohort. Candidate-overlay diagnostic completed 65536 ticks at 278560KiB peak RSS. Linux/macOS full checksum files match exactly; both save/resume checks match 32768 ticks. Baseline-branch release validation remains pending.

## Separate profiling diagnostics

After devlaptop timing finished, ran both versions from eight identical selected source states: Canals and Plantations, both AI configurations and both source origins. Profiles are diagnostic, not substitutes for randomized timing.

| State | Building field calls, baseline → candidate | Population-time change |
|---|---:|---:|
| g29-s10501-r0-maxima-baseline | 26020 → 19348 | -0.17% |
| g29-s10501-r0-maxima-candidate | 24648 → 21545 | +0.85% |
| g29-s10501-r0-nicowar-baseline | 15994 → 11675 | +0.20% |
| g29-s10501-r0-nicowar-candidate | 20013 → 13826 | +0.98% |
| g48-s10501-r0-maxima-baseline | 29575 → 22566 | +1.01% |
| g48-s10501-r0-maxima-candidate | 31350 → 22121 | -0.02% |
| g48-s10501-r0-nicowar-baseline | 58361 → 46928 | +0.11% |
| g48-s10501-r0-nicowar-candidate | 51152 → 37893 | -1.92% |

Building-gradient calls fall 13–31% across these states while population-time differs by approximately −2% to +1%. This supports a direct reduction in gradient work; it does not causally attribute the entire CPU difference or control every changed building/AI decision. Raw extracted counters are in profile-summary.json; original commands and gzip telemetry remain on devlaptop.

## Completed CPU study

1,024 measured runs, 512 per Linux host: same 64 states from 16 map families, two AI configurations, two source origins, four randomized paired repetitions. Source seed 10501 only; this is broad map coverage, not independent replication across CPU-study seeds. Both original and header-compatible input hashes match across hosts. All measured endpoints/tick counts match within pairs and across hosts. Pilots and profiles are excluded from timings.

| Host | Configuration | CPU change | 95% interval |
|---|---|---:|---:|
| therig | all | -6.93% | [-8.52%, -5.41%] |
| therig | maxima | -2.10% | [-3.96%, -0.19%] |
| therig | nicowar | -11.53% | [-13.44%, -9.59%] |
| therig | baseline-origin | -6.52% | [-8.22%, -4.80%] |
| therig | candidate-origin | -7.34% | [-9.12%, -5.51%] |
| devlaptop | all | -7.53% | [-9.21%, -5.86%] |
| devlaptop | maxima | -3.09% | [-5.23%, -0.89%] |
| devlaptop | nicowar | -11.76% | [-13.80%, -9.67%] |
| devlaptop | baseline-origin | -7.30% | [-9.16%, -5.41%] |
| devlaptop | candidate-origin | -7.76% | [-9.65%, -5.56%] |

Estimates average paired log CPU ratios, equally weighting families. Bootstrap resamples map families and within-state paired repeats, keeping the source-origin/AI groups together within each family. Confidence intervals describe the sampled developed states; not every map or game benefits. Behavior diverges after identical starting saves, so not all CPU improvement can be causally assigned to gradient computation. Separate profiles support a direct reduction in building-field work.

Conclusion: the behavior-changing candidate shows repeatable CPU savings on both hosts, smaller for Maxima than Nicowar. Completed-game wheat/meal results clear the prespecified −5% guardrail, but do not prove unchanged behavior, universal map-level noninferiority, or player-perceived equivalence. The shared Nicowar failure remains a recorded failure. PR remains draft pending compatibility/CI fixture resolution and human gameplay review. Nothing has been merged or installed.
