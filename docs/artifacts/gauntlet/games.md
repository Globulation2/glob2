# Final playtests

Final V9: four mixed-AI rotations of seed 1, each 45,000 ticks, plus seeds 2 and 3
at 25,000 ticks. Every game completed its requested tick limit. The map rotations
were verified by the generation runner. These are six automated games, not human
play sessions or a statistically powered balance tournament. All matches remained
unresolved at their time limits.

[Per-colony results](games.csv) retain births, harvest, population, deaths, buildings,
ammunition and eliminations. [The summarizer](summarize-games.py) reads completed
result files and final counter lines from the raw logs retained under
`artifacts/gauntlet/v9-game-*.log`.

## Four-rotation results, seed 1

| AI | Mean peak units | Worker births range | Wheat harvested range | Wood harvested range | Eliminations |
| --- | ---: | ---: | ---: | ---: | ---: |
| Nicowar | 112.2 | 50–152 | 716–2128 | 119–149 | 2/4 |
| Cortex | 375.2 | 128–183 | 3654–4708 | 134–183 | 0/4 |
| Cabino | 405.8 | 152–271 | 2293–3659 | 484–656 | 0/4 |
| Maxima | 91.0 | 35–113 | 679–1347 | 91–208 | 1/4 |

Cabino now builds and gathers wood in every position; the earlier zero-construction
failure is absent. Cortex and Cabino develop much larger colonies than the other
AIs. Late starvation still occurs, particularly in Cortex's large warrior armies;
these games do not establish that every AI handles the economy equally well.
Combat deaths and eliminations demonstrate contact rather than a sealed stalemate.

| Original start index | Mean peak population across the four different AIs |
| --- | ---: |
| 0 | 242.5 |
| 1 | 253.2 |
| 2 | 231.2 |
| 3 | 257.2 |

No start fails consistently across AIs. The relatively close pooled peaks are
useful positional evidence, but four observations per start cannot prove balance.

## Other layout families

At 25,000 ticks, all eight colony runs on seeds 2 and 3 had harvested wheat and wood,
constructed additional buildings, and survived. This establishes working openings
for jousting and paired-garden layouts; only the bent family received the longer
rotation tournament. The weakest opening growth budget belongs to seed 3, as
recorded in [the growth measurements](growth.md).

[Late-game preview](late-game.png) is seed 1, rotation 2 at 45,000 ticks. Its farms
have spread within their beds while the home lawns and arena circulation remain
clear. The terrain validator separately checks the conservative mature-growth
footprint and sealed swimming routes; a preview alone does not prove these contracts.

A matching towers-off game is discussed in [the evidence index](README.md#economy-and-defenses).
