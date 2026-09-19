# Gauntlet evidence

This directory contains compact, reviewable evidence for the optional Gauntlet
generator. Bulk maps, games, logs and parameter rows are retained under
`artifacts/gauntlet/` in the working checkout.

## Integration identity

The original frozen playtest and profiling binaries used provisional ID 58. Orchard
Commons merged first and owns that ID, so the integrated Gauntlet uses **ID 59**;
its string ID remains `gauntlet`. Historical raw reports and frozen-binary scripts
retain their original IDs. Current reproduction commands use 59 or the string ID.
The [integration check](integration/README.md) regenerated golden rows and confirmed
all Gauntlet outcomes/fingerprints against the provisional build; both supported
Linux compilers also produced the same observed maps.

## Final layouts

| Family | Finished tile preview | Request and full map report |
| --- | --- | --- |
| Bent courts, seed 1 | [Preview](bent.png) | [JSON](bent.json) |
| Jousting courts, seed 2 | [Preview](jousting.png) | [JSON](jousting.json) |
| Paired gardens, seed 3 | [Preview](paired-gardens.png) | [JSON](paired-gardens.json) |

All three are 256×256 with four colonies and default controls. The preview colors
show final tile types; red is buildings, gray stone, yellow wheat and pink fruit.

## Iterative review

The first independent review found a severe wood-access imbalance: clipped farm
rows put the timber far from some homes. Wood within 48 walking steps ranged from
11 to 351 stored units. It also found that tiny court gardens were decorative
rather than a useful expansion economy, and that the outer farm grid dominated
the image.

The second version placed nearby wood and grain before ambient planting, enlarged
the irrigated court gardens and opened the home lawns. The reviewer measured wood
within 48 steps at 70–86, up from the earlier 11–351 spread, and an improvement in
the static fairness proxy from 0.850 to 0.954. These are opening measurements, not
predicted tournament win rates. A second review approved the visual direction and
requested stronger circuit and growth checks.

Those checks caught a real court-bed containment defect. Planting now stays inside
the sand caps. Validation tests the finished terrain, all intended region
connections, and crop-safe circulation, including swimmers.

## Early calibration

Identical seed and AI order, 25,000 ticks on a 256×256 map, four colonies:

| AI | First version: worker births / wheat harvested | Revised economy: worker births / wheat harvested |
| --- | --- | --- |
| Nicowar | 37 / 455 | 71 / 787 |
| Cortex | 6 / 123 | 43 / 1,385 |
| Cabino | 33 / 248 | 52 / 807 |
| Maxima | 27 / 414 | 36 / 477 |

Cabino's wood harvest changed from zero to 161. These paired runs diagnosed the
opening; later rotation games and parameter studies cover the final implementation.

## Review findings carried into validation

Later review tightened the validator to check every fruit species, reserve complete
4×4 construction footprints after possible crop growth, and reject unintended
routes even when removable resources temporarily hide them. The test suite removes
only orange trees and separately blocks a home boundary to exercise those checks.
The circuit must survive mature crop spread; stone also seals swimming shortcuts.

A rotated mixed-AI tournament revealed an AI-specific opening stall: Cabino chooses
its construction center from discovered terrain, and widely separated starting
towers could put that center on stone. The fronts now have a bounded physical span,
and guards sit back from their covered entrance mouths. This also makes the two
fronts easier to supply. No AI or simulation behavior was changed.

The final independent frontage review covered 144 maps and 768 homes: seeds 1–6,
tower levels 0–3, 256 maps with 2/4/8 colonies, and 512 maps with 2/4/12 colonies.
No reconstructed construction center landed on a resource; the minimum clearance
was three tiles. [Summary](centroid-summary.json), [records](centroid-results.jsonl),
and [script](centroid-study.py) are retained. This model uses building visibility;
actual games also include worker discovery before the AI chooses its center.
An additional [80 extreme-control maps](centroid-extremes-summary.json) covered 448
homes, with no placement failure or resource collision and at least two tiles of
clearance. [Records](centroid-extremes-results.jsonl) and [script](centroid-extremes-study.py)
cover court 80/120, gate 5/9, tower 1/3 and thick partitions at seeds 1–2.

## Economy and defenses

[Final playtest results](games.md) cover four 45,000-tick rotations and two
25,000-tick layout-family games, with per-colony counters and a late-game preview.

The final three layouts retain [nearby renewable growth and opening supplies](growth.md):
24-step growth potential is 77–90; nearest wood is 7–10 steps away. Those values
measure reachable growing land, not actual harvest, and exclude enemy pressure.

At the same seed and AI order, the [24,576-tick defense comparison](mechanism-24576.csv)
recorded 184 tower shots with starting guards, versus 29 without starting guards
(Cabino built its own tower in that game). Guarded team 0 consumed
27 stone for ammunition, exceeding its eight granted stone, and delivered 36 stone.
This establishes continued ammunition supply, rather than only a burst from starting
bullets. There were 11 versus 17 warrior combat deaths across the games; this single
pair demonstrates a working mechanism, not a statistical balance claim.

## Parameter and translation checks

[Control measurements](controls.md) contain the one-at-a-time statistics and explain
finite-bed saturation. [Translation review](translations.md) records all 33 catalogs
and five passing translation tests.

## Profiling method

macOS `sample` located duplicate exterior-distance floods: both `layFarm` and the
Gauntlet computed the same field. An optional output now reuses the existing flood.
The shared helper's default behavior is unchanged. A regression compares terrain,
all farm fields, and returned distances for rectangular, irregular, full and empty
regions. Frozen binaries before and after the optimization were compared using
identical requests and output paths: 60 successful maps and their complete JSON
reports matched exactly; 12 unsupported requests refused identically.

Seven paired repetitions at 512×512 with twelve colonies measured median map-only
wall time of 2.706 → 2.630 seconds, and map plus JSON of 3.182 → 2.997 seconds.
[Raw timings, hashes and reproduction script](profile/README.md) are retained.

The final compact-frontage version also received a separate large-map timing pass:
three seeds at 512×512/twelve colonies, alternating Gauntlet and Forts. Median
map-only wall time was **0.916 seconds for Gauntlet and 2.088 seconds for Forts**.
[Commands and measurements](final-timing.json) and [script](final-timing.py) are
retained. This later run had less host contention and must not be used to infer
an optimization speedup against the earlier before/after timings.

Timings use alternating binary order on the same machine. Other development jobs
shared the host, so timings describe these samples rather than an isolated benchmark.

## Regression checks

All checks passed on the final implementation:

- [Focused Gauntlet contracts](tests-v9-focused.log), [shared toolkit regressions](tests-v9-toolkit.log),
  and [full defaults suite](tests-v9-full.log).
- [Golden comparison](golden-v9-check.log): 448 rows, zero failures. The
  [update proof](golden-v9-diff-proof.txt) shows exactly eight new Gauntlet rows;
  every existing row is unchanged.
- [Telemetry isolation](golden-v9-telemetry.log): 168 generated cases with no
  semantic differences between collection enabled and disabled.

## Limits

Validation and AI games were run on macOS arm64. Cross-platform generation and
per-tick checksums were not compared. No human play session has yet established
whether pacing or the two-front decisions feel fun. Stronger starting towers may
slow first contact; higher wheat requests eventually saturate finite protected beds.

## Reproducing the final runs

Build as described in [the generator guide](../../map-generators/GAUNTLET.md).
Use fresh output directories: structured jobs deliberately refuse to overwrite
completed results. The final rotated mixed-AI games used:

```sh
build/src/glob2 --generate-map --generator 59 --map-seed 1 \
  --param teams=4 --param width=8 --param height=8 --write-map true \
  --rotations 4 --report terrain --output-dir artifacts/gauntlet/reproduced/maps
for rotation in 0 1 2 3; do
  build/src/glob2 --run-game \
    --map-file "artifacts/gauntlet/reproduced/maps/map-r${rotation}.map" \
    --game-seed 1 --player nicowar --player cortex --player cabino --player maxima \
    --ticks 45000 --save final --telemetry team-timeline \
    --output-dir "artifacts/gauntlet/reproduced/game-${rotation}" \
    > "artifacts/gauntlet/reproduced/game-${rotation}.log" 2>&1
done
```

Seeds 2 and 3 use one rotation and 25,000 ticks. The defense comparison uses seed 1,
one rotation, `--param starting-towers=0`, and 25,000 ticks. Raw logs, final saves,
result JSON and initial maps are retained locally in `artifacts/gauntlet/`, with
`v9-seed-*`, `v9-game-*`, and `v9-no-towers*` prefixes. The earlier failed rotation
games remain there as evidence of the construction-origin issue.

The final held-out study generated and validated **all 2,000 supported requests**
with no failures. It varied every registered control, 1–8 starting workers, all
supported colony counts, square and rectangular maps, and seeds 200000–201999.
[Configuration counts](supported-summary.json) and the [script](supported-study.py)
are retained; full rows are in `artifacts/gauntlet/held-out-v9.jsonl`.

```sh
python3 docs/artifacts/gauntlet/supported-study.py build/src/glob2 \
  artifacts/gauntlet/reproduced-held-out.jsonl 2000
```
