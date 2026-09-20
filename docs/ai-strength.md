# AI ratings from the completed broad duel tournament

The AI selector displays combined Elo from **10,000 successful 128×128 duels across 60 map generators**, involving all eight AIs. The tournament uses source `f2cfcfeb05b39cc17eab43a532ebb551172de8de` on both platforms.

| AI | Displayed Elo | 95% bootstrap interval | Difficulty |
| --- | ---: | ---: | --- |
| Maxima | 1847 | 1774–1977 | Hard |
| Cabino | 1809 | 1594–1811 | Hard |
| Nicowar | 1631 | 1551–1776 | Medium |
| Cortex | 1488 | 1480–1714 | Medium |
| Warrush | 1452 | 1263–1509 | Medium |
| Econo | 1338 | 1178–1402 | Easy |
| Castor | 1241 | 1165–1381 | Easy |
| Numbi | 1196 | 1097–1334 | Easy |

![Final AI ratings in the selector](win-probability/ai-strengths.png)

Each generator contributes 166 or 168 games, and each of the 28 AI pairings contributes 356 or 358. The 5,000 paired map/game-seed blocks each contain two games with swapped starting sides. The two builds contribute 2,224 macOS and 7,776 Linux games; all five requested hosts participated. Seed 20260920, five candidate rolls per map, 90,000-tick cap, and probability victory disabled.

The engine decided 8,740 games; 1,260 reached the tick cap and use the existing prestige adjudication policy. Ratings start at 1500, use K=32 in manifest order, and pool both platform builds. These are iterative Elo scores, not maximum-likelihood strength estimates. Only the final displayed integers are rounded.

Nine Hilbert River map seeds were rejected before play, affecting 18 games. They were replaced with new seeds of the same generator, AI matchup, build and settings, retaining both swapped-side games and the original schedule positions. The [replacement audit](validation/ai-elo-10000-20260920/replacements.json) records every rejected and replacement job. Failed requests, smoke tests, old campaigns and duplicate attempts are excluded from the 10,000 successful games.

## Coverage and uncertainty

The compatible pool was drawn from the 66 playable generators present at tournament launch. Emoji, Gauntlet, Comb, Encircled Kingdom, Faulted City and Bastion Keys require larger maps or more colonies and are explicitly excluded. Rice Terraces uses its existing `slant=0` setting; other controls retain their defaults. The editor-only Uniform tool is excluded. The Last Treeline was added to master after the tournament source was frozen and is not represented in these results.

The 95% intervals use 1,000 bootstrap resamples of whole paired blocks, seed 1. They reflect sampling and order variability of this Elo procedure, not guarantees for every map, multiplayer format or human opponent. Maxima and Cabino’s intervals overlap. Pooling builds does not establish cross-platform per-tick checksum equivalence.

The selector sorts weakest first and keeps inactive last. Difficulty bands are below 1450 Easy, 1450–1699 Medium, and 1700+ Hard. This changes labels and selection order; AI behavior and saved IDs are unchanged.

## Reproducible evidence

[Retained outcomes](validation/ai-elo-10000-20260920/outcomes.json.gz), [final summary](validation/ai-elo-10000-20260920/summary.json), [configuration](validation/ai-elo-10000-20260920/comparison.json), [coverage](validation/ai-elo-10000-20260920/coverage.json), and [compatibility/exclusions](validation/ai-elo-10000-20260920/compatibility.json) are committed. Recalculate the pooled scores and intervals with Python’s standard library:

```sh
python3 docs/validation/ai-elo-10000-20260920/reproduce.py
```

The standalone calculation is checked against the tournament implementation within 1e-8 Elo. The [Symmetric Arena-only study](ai-strength-symmetric-arena-20260919.md) and [earlier mixed-format study](ai-strength-20260917.md) remain preserved as historical evidence. Neither supplies the current UI scores.
