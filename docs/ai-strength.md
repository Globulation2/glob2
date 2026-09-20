# AI ratings with the updated Maxima

These are **19,948 fresh 128×128 duels across the same 60 compatible generators**, using source `d37c0c353d9c2c68b4543286aa9b14e23ae7e526`. This version includes the Maxima rally-placement and army-capacity fixes (#367 and #368). The previous 20,000-game cohort is preserved separately and contributes no observations to this fit.

| AI | Fitted Elo | 95% paired-bootstrap interval | Change vs previous version |
| --- | ---: | ---: | ---: |
| Maxima | 1873 | 1858–1889 | +16.0 |
| Cabino | 1680 | 1668–1693 | -9.7 |
| Nicowar | 1653 | 1641–1665 | -3.0 |
| Cortex | 1601 | 1588–1612 | +2.3 |
| Warrush | 1401 | 1388–1413 | +10.7 |
| Econo | 1310 | 1298–1322 | +1.6 |
| Castor | 1280 | 1268–1292 | +2.9 |
| Numbi | 1204 | 1189–1218 | -20.8 |

Changes above compare fitted point estimates; they are not confidence intervals for the differences. All ratings are centred on 1500 within each cohort.

![AI selector](win-probability/ai-strengths.png)

The original schedule targeted 20,000 games in 10,000 swapped-side pairs, with 332 or 334 games per generator. At the user’s request, this final analysis uses the 9,974 complete successful pairs available when the run stopped. Of 19,950 successful games, two successful counterparts to engine crashes are excluded to preserve side balance. The other 50 scheduled games comprise 48 invalid-map results and two repeated memory crashes. No failures count as losses and no replacement games were run. The retained generator counts are in summary.json; planned coverage.json describes the original schedule, not achieved quotas. Missing outcomes may introduce selection bias that the bootstrap intervals do not capture. The fresh sample seed is 20260922. No map seeds or job IDs overlap the reference cohort. All six requested hosts participated initially, including therig.local (68 slots). Localhost was then withdrawn at the user’s request; the remaining five Linux hosts supplied 60 slots. Unfinished macOS pairs were rescheduled together with the same seeds and settings on the existing Linux build; the host-reassignment audit preserves the originals. The original 240-game preflight passed on both platform binaries and all six hosts.

Maps use five candidate rolls, a 90,000-tick cap, prestige adjudication and probability victory disabled. 17,591 games were engine-decided; 2,355 reached the cap. Pre-play map rejections and the two incomplete crash pairs are excluded, with every omitted job recorded in exclusions.json. Raw attempts remain preserved.

Every game has equal weight in an unregularized Bradley–Terry fit, with no K factor or input-order weighting. Draws score 0.5; 400 rating points represent tenfold odds. The 95% intervals refit 1,000 resamples of whole paired blocks within generators, seed 1, preserving the retained generator counts. This removes order weighting, not model assumptions or map-selection effects.

The pool matches the reference tournament: Emoji, Gauntlet, Comb, Encircled Kingdom, Faulted City and Bastion Keys remain excluded for format incompatibility; Uniform is editor-only. Last Treeline is deliberately excluded to retain the same map pool. Rice Terraces uses `slant=0`; other controls retain defaults. The other seven AIs and existing generators are unchanged between the reference and new source revisions.

The selector sorts weakest first and inactive last. Difficulty bands remain below 1450 Easy, 1450–1699 Medium and 1700+ Hard. This PR changes labels/ordering; the Maxima gameplay fixes are already on master. Saved AI IDs are unchanged.

## Evidence and reproduction

[Summary](validation/ai-elo-maxima-d37c0c353-20000-20260920/summary.json), [all outcomes](validation/ai-elo-maxima-d37c0c353-20000-20260920/outcomes.json.gz), [coverage](validation/ai-elo-maxima-d37c0c353-20000-20260920/coverage.json), [exclusion audit](validation/ai-elo-maxima-d37c0c353-20000-20260920/exclusions.json), [source/configuration](validation/ai-elo-maxima-d37c0c353-20000-20260920/source.json), [preflight](validation/ai-elo-maxima-d37c0c353-20000-20260920/preflight-summary.json) and [validation](validation/ai-elo-maxima-d37c0c353-20000-20260920/validation.md) are retained.

```sh
python3 docs/validation/ai-elo-maxima-d37c0c353-20000-20260920/reproduce.py
python3 docs/validation/ai-elo-maxima-d37c0c353-20000-20260920/test_rating_model.py
python3 docs/validation/ai-elo-maxima-d37c0c353-20000-20260920/independent_check.py
```

The [previous-version 20,000-game results](validation/ai-elo-20000-20260920/summary.json), [original 10,000-game evidence](validation/ai-elo-10000-20260920/summary.json), and historical studies remain preserved. Do not pool outcomes across Maxima versions. Platform pooling does not establish per-tick checksum equivalence.
