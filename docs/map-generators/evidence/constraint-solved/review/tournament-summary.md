# Map fairness tournament: constraint-solved-review

Revision-2 smoke tournament: two seeds per generator, all four rotations, one engine seed per rotation. Checks playable games and parsing; too small to establish fairness.

- Revision `c3cf78569c376806383cdbf81ca50b389f001555`, generated 2026-09-19T09:04:19, preset `docs/map-generators/evidence/constraint-solved/review/preset.json`.
- 128x128 maps, 4 colonies, `nicowar` in every slot, free for all (prestige victory on, as in the lobby), tick cap 90000.
- Each map is the best of 5 lobby rolls of its seed; 4 rotation(s) x 1 engine seed(s) = 4 games per map.
- 24 of 24 games completed (1 cap, 23 elimination); wall time per game mean 205.7 s, median 171.0 s, max 438.2 s (4.26 s per 1000 ticks); median game length 43858 ticks.
- A game that reaches the tick cap goes to the surviving colony with the most prestige, then units, then finished buildings; an exact tie stays unresolved and is left out of win counts.
- Reproducibility: 2 of 2 re-run games matched their first run exactly (ticks, winner, order count, every team result).

## Headline by generator

Position bias is the root-mean-square gap between each start's true win rate and the fair 1/4, corrected for the scatter raw win shares show even on a fair map and averaged over maps; the bracket is a 95% t interval over maps. Fair-map floor is the 95th percentile that headline reaches when every map is perfectly fair, with the same decided games per map: a headline below it cannot be told apart from a fair generator. Decisive only repeats the position bias counting just games won outright, so tick-cap adjudication cannot drive it. Biased maps counts maps whose wins by start reject a uniform split, raw p < 0.05 and after Benjamini-Hochberg across that generator's maps. Any bias p tests whether any of its maps is biased. Scorer rho is the within-map rank correlation between a colony's start-quality score and its win share, with a permutation p-value.

| Generator | Maps | Games | Cap | Position bias (pp) [95%] | Fair-map floor (pp) | Decisive only (pp, games) | Biased maps p<.05 / BH | Best start / fair (median) | Any bias p | Scorer rho (wins) |
| --- | ---: | ---: | ---: | --- | ---: | --- | --- | ---: | ---: | --- |
| Symmetric arena (15) | 2/2 | 8 | 12% | 20.4 [0.0, 55.4] | 25.0 | 20.4 [0.0, 55.4] (7) | 0 / 0 (chance 0.1) | 2.50 | 0.112 | n/a |
| Even Ground (59) | 2/2 | 8 | 0% | 32.3 [0.0, 107.8] | 25.0 | 32.3 [0.0, 107.8] (8) | 1 / 1 (chance 0.1) | 3.00 | 0.011 | 0.59 (p 0.159) |
| Marchland (60) | 2/2 | 8 | 0% | 0.0 [0.0, 51.4] | 25.0 | 0.0 [0.0, 51.4] (8) | 0 / 0 (chance 0.1) | 2.00 | 0.531 | -0.25 (p 0.620) |

## Engine team-index bias

Wins by team index, pooled over rotations, so every index played every start equally often. A skew here is the engine's processing order (team 0 also carries the passive local player of `-test-games-nox` and its AI polls last), not the map.

| Scope | Decided games | Wins by team 0 / 1 / 2 / 3 | p (uniform) | Top team share [95%] | Bias (pp) |
| --- | ---: | --- | ---: | --- | ---: |
| Symmetric arena (baseline) | 8 | 3 / 3 / 1 / 1 | 0.654 | team 0: 37.5% [13.7%, 69.4%] | 0.0 |
| All generators | 24 | 10 / 7 / 5 / 2 | 0.115 | team 0: 41.7% [24.5%, 61.2%] | 8.5 |

## Symmetric arena (generator 15, id `symmetric-arena`, revision 1)

| Map seed (roll) | Games (cap) | Wins by start 0 / 1 / 2 / 3 | p | BH q | Best start share [95%] | Best / fair | Bias (pp) | Quality by start | Quality rho |
| --- | --- | --- | ---: | ---: | --- | ---: | ---: | --- | ---: |
| 2001 (546346630) | 4 (0) | 0 / 3 / 0 / 1 | 0.203 | 0.344 | start 1: 75.0% [30.1%, 95.4%] | 3.00 | 25.0 | 0.37 / 0.36 / 0.36 / 0.37 | -0.32 |
| 2002 (3034067026) | 4 (1) | 0 / 0 / 2 / 2 | 0.344 | 0.344 | start 2: 50.0% [15.0%, 85.0%] | 2.00 | 14.4 | 0.25 / 0.24 / 0.25 / 0.25 | 0.58 |

- Wins by generator colony index, pooled over maps: 0 / 3 / 2 / 3 (p 0.398); a skew means the generator favours a colony it places early or late.
- Wins by team index, pooled: 3 / 3 / 1 / 1 (p 0.654).
- Maps significant after Holm: 0; best-start dominance min / median / max: 2.00 / 2.50 / 3.00.
- Scorer check: left out. Symmetric arena colonies are identical by construction; the small score differences between them come from the build-site count, which is not symmetric under rotation.

## Even Ground (generator 59, id `even-ground`, revision 2)

| Map seed (roll) | Games (cap) | Wins by start 0 / 1 / 2 / 3 | p | BH q | Best start share [95%] | Best / fair | Bias (pp) | Quality by start | Quality rho |
| --- | --- | --- | ---: | ---: | --- | ---: | ---: | --- | ---: |
| 2001 (1188153717) | 4 (0) | 4 / 0 / 0 / 0 | 0.016 | 0.031 | start 0: 100.0% [51.0%, 100.0%] | 4.00 | 43.3 | 0.24 / 0.27 / 0.17 / -0.04 | 0.26 |
| 2002 (23265159) | 4 (0) | 0 / 2 / 0 / 2 | 0.344 | 0.344 | start 1: 50.0% [15.0%, 85.0%] | 2.00 | 14.4 | 0.16 / 0.34 / 0.17 / 0.53 | 0.89 |

- Wins by generator colony index, pooled over maps: 4 / 2 / 0 / 2 (p 0.295); a skew means the generator favours a colony it places early or late.
- Wins by team index, pooled: 3 / 3 / 1 / 1 (p 0.654).
- Maps significant after Holm: 1; best-start dominance min / median / max: 2.00 / 3.00 / 4.00.
- Scorer check: rho with win share 0.59 (p 0.159), with placement 0.29 (p 0.525); the start the scorer rated best won 2 of 8 games on 2 maps (25.0%, fair 25.0%; one-sided permutation p 0.611). By factor (wins rho): win_probability 0.59.

## Marchland (generator 60, id `marchland`, revision 2)

| Map seed (roll) | Games (cap) | Wins by start 0 / 1 / 2 / 3 | p | BH q | Best start share [95%] | Best / fair | Bias (pp) | Quality by start | Quality rho |
| --- | --- | --- | ---: | ---: | --- | ---: | ---: | --- | ---: |
| 2001 (844529007) | 4 (0) | 2 / 0 / 1 / 1 | 0.906 | 0.906 | start 0: 50.0% [15.0%, 85.0%] | 2.00 | 0.0 | -0.06 / 0.06 / -0.18 / -0.02 | -0.63 |
| 2002 (23265159) | 4 (0) | 0 / 0 / 2 / 2 | 0.344 | 0.687 | start 2: 50.0% [15.0%, 85.0%] | 2.00 | 14.4 | -0.10 / -0.02 / -0.13 / 0.01 | 0.00 |

- Wins by generator colony index, pooled over maps: 2 / 0 / 3 / 3 (p 0.398); a skew means the generator favours a colony it places early or late.
- Wins by team index, pooled: 4 / 1 / 3 / 0 (p 0.198).
- Maps significant after Holm: 0; best-start dominance min / median / max: 2.00 / 2.00 / 2.00.
- Scorer check: rho with win share -0.25 (p 0.620), with placement 0.26 (p 0.504); the start the scorer rated best won 2 of 8 games on 2 maps (25.0%, fair 25.0%; one-sided permutation p 0.612). By factor (wins rho): win_probability -0.25.

## Scorer check across generators

Within-map rank correlation between start-quality score and win share over 16 colonies on 4 maps, Symmetric arena left out: rho 0.33 (p 0.280); with placement 0.39 (p 0.222). Across the same maps, the start the scorer rated best won 4 of 16 games on 4 maps (25.0%, fair 25.0%; one-sided permutation p 0.530).
