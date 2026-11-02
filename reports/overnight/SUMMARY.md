# Overnight Maxima policy tournament

Snapshot: 2026-11-02T08:45:48.360068+00:00 through 2026-11-02T08:51:29.688074+00:00. Successful games 3432/3456; accepted jobs 3624/3648.

Archived tournaments.analysis observations/probability_scores/paired_effects; no unbounded reanalyze telemetry export.
Primary adjusted trajectory score log_time_outcome_v1,c=2,fixed90000horizon, from archived official group_score via observations; actual/probability-stop terminal credits labeled; tickcaps have no invented tail. Legacy linear area/mean is secondary only; metric,horizon,model,stopping threshold must match.
2v2 subject alliance scored once through archived group_summary; ally probabilities never summed.
Primary paired clusters require all4scenario pairs for each format/geometry-seed. Aggregate equalweights3formats and12generators; missing intended clusters/strata explicitly unavailable.
Primary intervals resample generators then geometry-seed clusters; fixed-generator seed-only sensitivity is also reported. Scope remains the declared maps/seeds; no original-cohort pooling.
Standard score-function W/L/D uses adjudicated placements. Raw engine outcomes and termination reasons remain separate in games.
Final-probability adjudicates tick-cap survivors; no economic-score adjudication.
Counter totals use actual publication window; lasttickgap is unavailable, not zero.
Late role means use only the manifest-selected diagnostic subset; no forward fill or simultaneous inventory claim.
Original compressed logs retained. Derivedreporthardbudget256MiB; no longform/perobjectCSV.

| Format | Complete geometry/seed clusters | Mean paired score change(pp) | 95% interval(pp) |
| --- | ---: | ---: | --- |
| 1v1 | 94 | 0.111 | [-0.057, 0.338] |
| 2v2 | 96 | 0.081 | [-0.251, 0.463] |
| ffa | 96 | 0.708 | [-0.275, 1.869] |
| equal-format aggregate | 94 | 0.279 | [-0.095, 0.691] |

## Manifest-matched direct control contrasts

| Candidate minus control | Matched pairs | Equal-generator paired score change(pp) |95% interval(pp)|
|---|---:|---:|---|
| inn8-swarm8 | 1144 | -0.058 | [-0.76, 0.593] |

## All-case direct policy contrasts

| Candidate minus control | Matched / expected | Complete clusters | Mean change(pp) | 95% interval(pp) |
| --- | ---: | ---: | ---: | --- |
| inn8-swarm8 minus baseline | 1144/1152 | 94 | 0.336 | [-0.276, 1.043] |
| inn8-swarm12 minus baseline | 1144/1152 | 94 | 0.279 | [-0.096, 0.683] |
| inn8-swarm12 minus inn8-swarm8 | 1144/1152 | 94 | -0.058 | [-0.76, 0.593] |

## Completion and food measurements (descriptive available games)

|Format|Variant|Success/planned|Actual engine-end outcomes|Predicted probability-stop outcomes|Tick caps|Mean population|Mean starvation|Mean meals wheat|
|---|---|---:|---|---|---:|---:|---:|---:|
| 1v1 | baseline | 376/384 | {'lost': 5, 'won': 3} | {'won': 300, 'lost': 54} | 14 | 122.3 | 5.6 | 223.7 |
| 1v1 | inn8-swarm8 | 376/384 | {'won': 5, 'lost': 3} | {'won': 300, 'lost': 53} | 15 | 119.4 | 5.2 | 240.2 |
| 1v1 | inn8-swarm12 | 376/384 | {'lost': 4, 'won': 3} | {'won': 301, 'lost': 53} | 15 | 121.2 | 5.5 | 221.6 |
| 2v2 | baseline | 384/384 | {'won': 3, 'lost': 4} | {'won': 270, 'lost': 85} | 22 | 121.1 | 9.0 | 275.9 |
| 2v2 | inn8-swarm8 | 384/384 | {'lost': 5, 'won': 3} | {'won': 275, 'lost': 84} | 17 | 120.0 | 8.7 | 263.5 |
| 2v2 | inn8-swarm12 | 384/384 | {'lost': 4, 'won': 2} | {'won': 272, 'lost': 85} | 21 | 121.7 | 8.2 | 273.8 |
| ffa | baseline | 384/384 | {'lost': 3, 'won': 4} | {'won': 232, 'lost': 111} | 34 | 185.9 | 15.3 | 563.1 |
| ffa | inn8-swarm8 | 384/384 | {'won': 2, 'lost': 5} | {'won': 237, 'lost': 108} | 32 | 186.5 | 16.1 | 587.7 |
| ffa | inn8-swarm12 | 384/384 | {'lost': 3, 'won': 3} | {'won': 237, 'lost': 109} | 32 | 190.9 | 14.2 | 570.4 |

Counter totals cover actual published windows, which vary with completion time; endpoint gaps and unavailable measurements are explicit in summary.json. Counts are not adjusted for different exposure durations.
