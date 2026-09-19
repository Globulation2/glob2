> Historical run from before the generators' random streams were renamed. These are not
> the maps produced by the current code. The dirty source revision and temporary preset
> were not preserved here; the tables are historical observations, not reproducible validation
> of the current branch. See [current review evidence](review/README.md).

# Map fairness tournament: constraint-solved-light

Light playtest of Equilibrium (58) and Tug (59) against the Symmetric arena baseline (15).

- Revision `6898cd7e2728c96495d20db803abeb0b9771b22c-dirty`, generated 2026-09-19T07:51:51, preset `/tmp/play/light.json`.
- 128x128 maps, 4 colonies, `nicowar` in every slot, free for all (prestige victory on, as in the lobby), tick cap 90000.
- Each map is the best of 5 lobby rolls of its seed; 4 rotation(s) x 2 engine seed(s) = 8 games per map.
- 144 of 144 games completed (6 cap, 138 elimination); wall time per game mean 450.1 s, median 418.7 s, max 1401.4 s (9.58 s per 1000 ticks); median game length 43442 ticks.
- A game that reaches the tick cap goes to the surviving colony with the most prestige, then units, then finished buildings; an exact tie stays unresolved and is left out of win counts.
- Reproducibility: 2 of 2 re-run games matched their first run exactly (ticks, winner, order count, every team result).

## Headline by generator

Position bias is the root-mean-square gap between each start's true win rate and the fair 1/4, corrected for the scatter raw win shares show even on a fair map and averaged over maps; the bracket is a 95% t interval over maps. Fair-map floor is the 95th percentile that headline reaches when every map is perfectly fair, with the same decided games per map: a headline below it cannot be told apart from a fair generator. Decisive only repeats the position bias counting just games won outright, so tick-cap adjudication cannot drive it. Biased maps counts maps whose wins by start reject a uniform split, raw p < 0.05 and after Benjamini-Hochberg across that generator's maps. Any bias p tests whether any of its maps is biased. Scorer rho is the within-map rank correlation between a colony's start-quality score and its win share, with a permutation p-value.

| Generator | Maps | Games | Cap | Position bias (pp) [95%] | Fair-map floor (pp) | Decisive only (pp, games) | Biased maps p<.05 / BH | Best start / fair (median) | Any bias p | Scorer rho (wins) |
| --- | ---: | ---: | ---: | --- | ---: | --- | --- | ---: | ---: | --- |
| Symmetric arena (15) | 6/6 | 48 | 12% | 0.0 [0.0, 6.1] | 11.6 | 0.0 [0.0, 6.6] (42) | 0 / 0 (chance 0.3) | 1.75 | 0.786 | n/a |
| Equilibrium (58) | 6/6 | 48 | 0% | 25.9 [0.0, 38.6] | 11.6 | 25.9 [0.0, 38.6] (48) | 3 / 3 (chance 0.3) | 2.50 | <0.001 | 0.41 (p 0.058) [0.32, 0.57] |
| Tug (59) | 6/6 | 48 | 0% | 12.2 [0.0, 18.3] | 11.6 | 12.2 [0.0, 18.3] (48) | 0 / 0 (chance 0.3) | 2.00 | 0.059 | 0.23 (p 0.350) [-0.22, 0.65] |

## Engine team-index bias

Wins by team index, pooled over rotations, so every index played every start equally often. A skew here is the engine's processing order (team 0 also carries the passive local player of `-test-games-nox` and its AI polls last), not the map.

| Scope | Decided games | Wins by team 0 / 1 / 2 / 3 | p (uniform) | Top team share [95%] | Bias (pp) |
| --- | ---: | --- | ---: | --- | ---: |
| Symmetric arena (baseline) | 48 | 15 / 8 / 11 / 14 | 0.477 | team 0: 31.2% [19.9%, 45.3%] | 0.0 |
| All generators | 144 | 33 / 35 / 36 / 40 | 0.884 | team 3: 27.8% [21.1%, 35.6%] | 0.0 |

## Symmetric arena (generator 15, id `symmetric-arena`, revision 1)

| Map seed (roll) | Games (cap) | Wins by start 0 / 1 / 2 / 3 | p | BH q | Best start share [95%] | Best / fair | Bias (pp) | Quality by start | Quality rho |
| --- | --- | --- | ---: | ---: | --- | ---: | ---: | --- | ---: |
| 2001 (546346630) | 8 (0) | 4 / 1 / 1 / 2 | 0.551 | 0.827 | start 0: 50.0% [21.5%, 78.5%] | 2.00 | 0.0 | 0.37 / 0.36 / 0.36 / 0.37 | 0.74 |
| 2002 (3034067026) | 8 (2) | 2 / 1 / 2 / 3 | 0.962 | 0.962 | start 3: 37.5% [13.7%, 69.4%] | 1.50 | 0.0 | 0.25 / 0.24 / 0.25 / 0.25 | 0.82 |
| 2003 (3140854180) | 8 (1) | 3 / 2 / 1 / 2 | 0.962 | 0.962 | start 0: 37.5% [13.7%, 69.4%] | 1.50 | 0.0 | 0.26 / 0.26 / 0.26 / 0.26 | 0.63 |
| 2004 (401415200) | 8 (3) | 3 / 2 / 0 / 3 | 0.398 | 0.827 | start 0: 37.5% [13.7%, 69.4%] | 1.50 | 0.0 | 0.17 / 0.17 / 0.17 / 0.16 | -0.74 |
| 2005 (2565527837) | 8 (0) | 4 / 1 / 2 / 1 | 0.551 | 0.827 | start 0: 50.0% [21.5%, 78.5%] | 2.00 | 0.0 | 0.29 / 0.29 / 0.29 / 0.29 | 0.32 |
| 2006 (926020328) | 8 (0) | 1 / 4 / 1 / 2 | 0.551 | 0.827 | start 1: 50.0% [21.5%, 78.5%] | 2.00 | 0.0 | 0.29 / 0.29 / 0.29 / 0.29 | -0.11 |

- Wins by generator colony index, pooled over maps: 17 / 11 / 7 / 13 (p 0.234); a skew means the generator favours a colony it places early or late.
- Wins by team index, pooled: 15 / 8 / 11 / 14 (p 0.477).
- Maps significant after Holm: 0; best-start dominance min / median / max: 1.50 / 1.75 / 2.00.
- Scorer check: left out. Symmetric arena colonies are identical by construction; the small score differences between them come from the build-site count, which is not symmetric under rotation.

## Equilibrium (generator 58, id `equilibrium`, revision 1)

| Map seed (roll) | Games (cap) | Wins by start 0 / 1 / 2 / 3 | p | BH q | Best start share [95%] | Best / fair | Bias (pp) | Quality by start | Quality rho |
| --- | --- | --- | ---: | ---: | --- | ---: | ---: | --- | ---: |
| 2001 (546346630) | 8 (0) | 0 / 1 / 7 / 0 | 0.002 | 0.005 | start 2: 87.5% [52.9%, 97.8%] | 3.50 | 35.4 | 0.31 / 0.31 / 0.59 / 0.52 | 0.63 |
| 2002 (23265159) | 8 (0) | 1 / 3 / 1 / 3 | 0.654 | 0.654 | start 1: 37.5% [13.7%, 69.4%] | 1.50 | 0.0 | 0.22 / 0.46 / 0.35 / 0.48 | 0.89 |
| 2003 (36133630) | 8 (0) | 1 / 3 / 4 / 0 | 0.198 | 0.296 | start 2: 50.0% [21.5%, 78.5%] | 2.00 | 13.4 | 0.10 / 0.23 / 0.53 / 0.36 | 0.40 |
| 2004 (380383894) | 8 (0) | 1 / 4 / 2 / 1 | 0.551 | 0.654 | start 1: 50.0% [21.5%, 78.5%] | 2.00 | 0.0 | 0.54 / 0.50 / 0.40 / 0.27 | 0.11 |
| 2005 (862494405) | 8 (0) | 0 / 0 / 2 / 6 | 0.007 | 0.013 | start 3: 75.0% [40.9%, 92.9%] | 3.00 | 28.3 | 0.20 / 0.40 / 0.21 / 0.40 | 0.63 |
| 2006 (3496247813) | 8 (0) | 0 / 0 / 0 / 8 | <0.001 | <0.001 | start 3: 100.0% [67.6%, 100.0%] | 4.00 | 43.3 | 0.27 / 0.45 / 0.31 / 0.46 | 0.77 |

- Wins by generator colony index, pooled over maps: 3 / 11 / 16 / 18 (p 0.004); a skew means the generator favours a colony it places early or late.
- Wins by team index, pooled: 8 / 14 / 15 / 11 (p 0.477).
- Maps significant after Holm: 3; best-start dominance min / median / max: 1.50 / 2.50 / 4.00.
- Scorer check: rho with win share 0.41 (p 0.058) [0.32, 0.57], with placement 0.35 (p 0.126) [0.17, 0.57]; the start the scorer rated best won 29 of 48 games on 6 maps (60.4%, fair 25.0%; one-sided permutation p 0.007). By factor (wins rho): wheat -, wood -, fertility -, depth -, room -, isolation -.

## Tug (generator 59, id `tug`, revision 2)

| Map seed (roll) | Games (cap) | Wins by start 0 / 1 / 2 / 3 | p | BH q | Best start share [95%] | Best / fair | Bias (pp) | Quality by start | Quality rho |
| --- | --- | --- | ---: | ---: | --- | ---: | ---: | --- | ---: |
| 2001 (2545613269) | 8 (0) | 3 / 1 / 0 / 4 | 0.198 | 0.237 | start 3: 50.0% [21.5%, 78.5%] | 2.00 | 13.4 | 0.09 / -0.00 / -0.07 / -0.01 | 0.40 |
| 2002 (3034067026) | 8 (0) | 1 / 3 / 0 / 4 | 0.198 | 0.237 | start 3: 50.0% [21.5%, 78.5%] | 2.00 | 13.4 | 0.09 / 0.12 / -0.13 / 0.21 | 1.00 |
| 2003 (36133630) | 8 (0) | 3 / 4 / 0 / 1 | 0.198 | 0.237 | start 1: 50.0% [21.5%, 78.5%] | 2.00 | 13.4 | 0.02 / 0.09 / 0.09 / -0.06 | -0.20 |
| 2004 (3707936981) | 8 (0) | 3 / 0 / 1 / 4 | 0.198 | 0.237 | start 3: 50.0% [21.5%, 78.5%] | 2.00 | 13.4 | -0.03 / 0.10 / 0.03 / -0.01 | -0.80 |
| 2005 (862494405) | 8 (0) | 1 / 2 / 3 / 2 | 0.962 | 0.962 | start 2: 37.5% [13.7%, 69.4%] | 1.50 | 0.0 | 0.05 / -0.04 / 0.08 / 0.05 | 0.63 |
| 2006 (2794315811) | 8 (0) | 5 / 1 / 0 / 2 | 0.095 | 0.237 | start 0: 62.5% [30.6%, 86.3%] | 2.50 | 18.9 | 0.10 / -0.21 / 0.04 / -0.02 | 0.40 |

- Wins by generator colony index, pooled over maps: 16 / 11 / 4 / 17 (p 0.018); a skew means the generator favours a colony it places early or late.
- Wins by team index, pooled: 10 / 13 / 10 / 15 (p 0.714).
- Maps significant after Holm: 0; best-start dominance min / median / max: 1.50 / 2.00 / 2.50.
- Scorer check: rho with win share 0.23 (p 0.350) [-0.22, 0.65], with placement 0.12 (p 0.626) [-0.29, 0.47]; the start the scorer rated best won 15 of 48 games on 6 maps (31.2%, fair 25.0%; one-sided permutation p 0.247). By factor (wins rho): wheat -, wood -, fertility -, depth -, room -, isolation -.

## Scorer check across generators

Within-map rank correlation between start-quality score and win share over 48 colonies on 12 maps, Symmetric arena left out: rho 0.38 (p 0.020) [0.16, 0.57]; with placement 0.30 (p 0.083) [0.08, 0.46]. Across the same maps, the start the scorer rated best won 44 of 96 games on 12 maps (45.8%, fair 25.0%; one-sided permutation p 0.004).
