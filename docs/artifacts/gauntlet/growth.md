# Final food-growth check

Frozen v9, default controls, 256×256, four colonies, seeds 1–3. The existing `growth-potential` executable was run on the final terrain dumps and reported swarm coordinates. [Raw output](growth-raw.txt) retains all measurements.

Yield is potential food regrowth expressed as equivalent full-fertility tiles, summed within the indicated walking distance. It is not harvested food, a supported population, or proof that an AI will plant and exploit every fertile tile. The tool walks through wheat and buildings, blocks wood/stone/fruit/water, and includes pure grass as potential farmland. Opening resource figures below come from the production map reports, whose walking model differs.

| Seed / family | Team | Growth ≤24 | Growth ≤48 | Wheat ≤12: tiles / stored | Nearest wood: steps | Wood stored ≤24 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 / bent | 0 | 90 | 281 | 15 / 44 | 9 | 73 |
| 1 / bent | 1 | 90 | 260 | 19 / 42 | 8 | 66 |
| 1 / bent | 2 | 90 | 260 | 27 / 66 | 7 | 74 |
| 1 / bent | 3 | 90 | 281 | 16 / 34 | 9 | 75 |
| 2 / jousting | 0 | 80 | 257 | 34 / 92 | 10 | 61 |
| 2 / jousting | 1 | 78 | 229 | 38 / 104 | 10 | 78 |
| 2 / jousting | 2 | 79 | 248 | 37 / 91 | 10 | 75 |
| 2 / jousting | 3 | 79 | 238 | 31 / 69 | 9 | 76 |
| 3 / paired gardens | 0 | 77 | 244 | 18 / 45 | 10 | 65 |
| 3 / paired gardens | 1 | 77 | 225 | 20 / 50 | 9 | 80 |
| 3 / paired gardens | 2 | 77 | 249 | 19 / 38 | 10 | 68 |
| 3 / paired gardens | 3 | 78 | 230 | 19 / 55 | 9 | 99 |

Within 24 steps, colonies on each map differ by at most two yield units. Seed 3 has the weakest local growth budget (77–78 within 24 steps; 225–249 within 48), while seed 1 has the strongest (90; 260–281). Every tested colony has 15–38 wheat deposits within 12 steps and reachable wood within 7–10 steps. These measurements establish available opening supply and growth potential, not successful long-term economy.

Production static fairness scores are **0.9703**, **0.9735**, and **0.9784** for seeds 1–3 respectively. Seed 1 is weakest by that fitted model; seed 3 is weakest by local growth potential. Neither ranking predicts the AI tournament outcome on its own.

The existing Forts reference directory contains its generated map and JSON but no terrain dump. No fresh reference generation was performed in this bounded check, so there is no final-version growth-potential comparison against Forts here.

## Reproduction

Build the standalone tool from `.agents/skills/glob2-map-design/scripts/growth_potential.c` as documented in that file, or use the retained `/tmp/gauntlet-evidence/growth-potential`. Final terrain/report pairs are in `/tmp/gauntlet-evidence/centroid-review-v9/w8-n4-l1-s{1,2,3}/`.

```sh
/tmp/gauntlet-evidence/growth-potential \
  /tmp/gauntlet-evidence/centroid-review-v9/w8-n4-l1-s1/terrain.txt \
  128 214 38 128 124 38 214 124
```

For seeds 2 and 3, substitute the swarm coordinates recorded in the raw output or the corresponding `result.json` (`map_report.map.colonies[].start`).
