# Anthill revision 6: AI calibration and the doorstep-farm fix

Evidence for Globulation2/glob2#1261, commit `20a2b6852f` ("Open every Anthill queen chamber onto a
farm chamber").

## Setup

- Maps: 256x256, 4 colonies, map seeds 101, 202, 303, lobby pick (`map study anthill --candidates 5`).
  `maps/<gen>-<seed>/` holds the played map and the study's request and per-colony quality.
- Games: four copies of one AI (Nicowar, Cortex, Cabino, Maxima), game seed 1, 25,000 ticks,
  `--telemetry team-timeline`, release build (`scons release=1`, macOS arm64), runner `run_games.sh`.
  `games/<gen>-<seed>-<ai>/result.json` is each game's result (unit history, buildings,
  eliminations); `final-counters.txt.gz` holds the final `GLOB2_MEASURE` and `GLOB2_AI_FINAL` rows.
  Full logs and final saves (6.6 GB) are kept locally, not here.
- Fairness: 30 map seeds (1-30) per variant, `map study anthill` at the defaults with no candidate
  search; `fairness/per-seed-quality.json` per seed, `fairness/summary.txt` (from `fairstat.py`).

Variants (all revision 6 unless named):

| Name | What differs |
| --- | --- |
| r5 | master's revision 5 (stone walls) |
| r6 | the PR before this commit: every chamber dug `kFace` = 1.5 tiles wider |
| noface | `kFace` = 0 everywhere |
| noquarry | no quarry chambers |
| queenface (`qf` in games) | `kFace` on queen chambers only |
| halfface | `kFace` 0.75 on farm and plain chambers |
| farmface (`ff` in games) | `kFace` on queen and farm chambers, not plain |
| door | the committed fix: r6 with every queen's doorstep chamber a farm chamber |

Tunnel width (`--set tunnel-width=2` on r6) was also tested and changed nothing.

## Results

`comparison.txt` (3 seeds x 4 AIs x 4 colonies per row; units = final units per colony):

```
gen   units wheat wood bld starve dead | best/worst start mean per map
r5     25.4   229   38 8.0   0.9    1 | [1.38, 1.67, 1.83]
r6     27.3   254   51 8.8   1.5    0 | [1.93, 2.19, 1.07]
qf     23.7   217   36 7.6   2.0    2 | [1.29, 1.38, 1.25]
ff     30.3   278   55 9.1   1.6    0 | [1.33, 2.05, 1.25]
door   30.1   277   53 9.3   1.4    0 | [1.54, 1.12, 1.07]
```

Fairness over 30 seeds (lobby canonical-quality model):

```
r5        median 0.932 p10 0.853 min 0.822 | max/mean fertility 1.78
r6        median 0.891 p10 0.832 min 0.68  | max/mean fertility 1.95
queenface median 0.913 p10 0.865 min 0.747 | max/mean fertility 1.80
farmface  median 0.890 p10 0.821 min 0.689 | max/mean fertility 1.98
door      median 0.918 p10 0.843 min 0.758 | max/mean fertility 1.80
```

Diagnosis: widened farm chambers made the chamber outside a queen's door decide the start. On
r6 seeds 101 and 202 colony 0 drew a farm there and was the strongest colony with every AI
(`homes-r6.png`). With every doorstep a farm, the per-start spread on those seeds falls to 1.54 and
1.12 while the economy keeps the widened farms' gain. Seed 303's lobby pick already had a farm on
every doorstep, so its door map is byte-identical to r6's: a built-in control.

Shape sweep (`sweep-door.txt`, `sweep-r6.txt`): identical, only 64x64 with three or more colonies
refuses.

## Limits

- One game per map and AI; single games vary by up to 40%, so per-AI differences are indicative.
- 25,000 ticks with combat deaths near zero: this tests growth, not fighting. No rotation
  tournament yet.
- Cortex sometimes never builds its first inn and dies (r5 seed 303, qf seed 202, Forts seed 202);
  an existing Cortex limit, not specific to this generator.
- Catchment spread (walkable tiles near a start) stays at r6's level: it also depends on how many
  tunnels meet beyond the doorstep.
