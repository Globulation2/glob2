# real-engine-probability-contract

Policy: win_probability, version 2. Results: 1/1.

Duels: order-independent Bradley–Terry fit, mean 1500, 400 points per tenfold odds, draws scored 0.5. No prior or K factor. Other formats retain sequential Elo with K=32.

| Format | Competitor | Elo |
| --- | --- | ---: |

Uncertainty uses 1 complete map/seed blocks, seed 1.
Duel intervals resample whole swapped-side pairs within each generator. Missing/unidentifiable fits are explicit in duel_rating_fit; sequential diagnostics are retained separately.
Raw observations, failures, configurations, pairing and distribution summaries are in the adjacent JSON and CSV files.

## Mean whole-game win probability

Trapezoidal area divided by observed duration; allies counted once. Higher is better.
Early stopping changes the observed duration. Compare matched stopping rules and models.

| Format | Build | Variant | Competitor | Role | Stop (permille) | W / L / D | Scored / games | Mean | 95% block interval |
| --- | --- | --- | --- | --- | ---: | --- | ---: | ---: | --- |
| 1v1 | 0f714eae83d8 | baseline | cortex | opponent | 950 | 0 / 0 / 1 | 1 / 1 | 0.5000 | None |
| 1v1 | 0f714eae83d8 | baseline | cortex | subject | 950 | 0 / 0 / 1 | 1 / 1 | 0.5000 | None |
