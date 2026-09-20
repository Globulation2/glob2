# AI ratings from the final duel tournament

The AI selector displays the following combined Elo ratings from **3,000 completed games**. All eight AIs played randomly sampled 1v1 matchups on generated 128×128 symmetric-arena maps. Both platform bundles contain source revision `53b4c99f9eb5852a34e854726d96448c66500f24`, including the latest Maxima at tournament launch.

| AI | Displayed Elo | 95% bootstrap interval | Difficulty |
| --- | ---: | ---: | --- |
| maxima | 1938 | 1916–2118 | Hard |
| cabino | 1899 | 1795–1973 | Hard |
| nicowar | 1759 | 1677–1865 | Hard |
| cortex | 1656 | 1491–1730 | Medium |
| numbi | 1269 | 1110–1306 | Easy |
| warrush | 1222 | 1097–1285 | Easy |
| econo | 1184 | 1119–1321 | Easy |
| castor | 1073 | 998–1192 | Easy |

![AI ratings in the selector](win-probability/ai-strengths.png)

Ratings start at 1500, use K=32, and process accepted games in immutable manifest order, not completion order. The macOS and Linux games form one pool, weighted by their actual game counts. The displayed integer is rounded only after all games are processed. These are iterative Elo scores, not the fitted maximum-likelihood strengths used by the historical analysis.

The engine decided 2540 games; 460 reached the 90,000-tick cap. Capped games use the existing prestige adjudication policy. Probability-based early victory was disabled. The sample seed is 20260919, with five candidate map rolls per game. Only the final campaign is counted; cancelled preliminary runs and duplicate/expired attempts are excluded.

The 95% intervals use 1,000 bootstrap resamples of complete map/game blocks with seed 1. They describe sampling variability of this Elo procedure and are not guarantees about other maps or multiplayer formats. An Elo value can depend on game order, and the platform cohorts are pooled without asserting cross-platform checksum equivalence.

The selector sorts weakest first and keeps the inactive option last. Difficulty labels use fixed descriptive bands: below 1450 Easy, 1450–1699 Medium, and 1700+ Hard. AI implementations and saved IDs are unchanged.

## Reproducible evidence

The [retained outcomes](validation/ai-elo-20260919/outcomes.json.gz) contain every accepted logical game in manifest order, including IDs, platform build, seeds, matchup, placement, termination and tick count. The [summary](validation/ai-elo-20260919/summary.json) includes combined and per-build ratings; the [design](validation/ai-elo-20260919/design.json) records the tournament configuration.

Recalculate the ratings and intervals using only Python’s standard library:

```sh
python3 docs/validation/ai-elo-20260919/reproduce.py
```

The standalone reproduction was checked against the tournament analysis implementation to within 1e-8 Elo. The [previous mixed-format study](ai-strength-20260917.md) remains preserved as historical evidence.
