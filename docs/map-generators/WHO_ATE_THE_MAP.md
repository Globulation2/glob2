# Who Ate the Map?

A novelty island with rounded biscuit bites missing from its coast, a patchy wooded
heartland, ponds and grass clearings. Positions are intentionally unequal. Bites can
leave peninsulas, narrow approaches and detached islands. Colonies on different
islands must build their own swimming pools; no extra buildings are granted.

## Controls and supported maps

`who-ate-the-map` (numeric ID 66) supports sides of 128, 256 and 512 tiles,
with aspect ratios up to 2:1. Maps with a 128-tile side support one to four
colonies; larger maps support one to eight.

`appetite` is 0 **A Little Nibble**, 1 **Hungry** (default), or 2 **Who Ate the Map?**.
The first two keep starting colonies on connected land. The last permits large
neighbouring bites to join and detach pieces; separation is seed-dependent.

The standard wheat, wood, stone, algae and fruit amounts scale ambient deposits.
Opening wheat, wood and a small quarry are guarantees independent of those settings.
Wood abundance controls finite inland reserves as well as renewable woods near
water. Dense resources are cleared where necessary to keep starts usable.

The outer shape, bites and ponds depend on dimensions, seed and Appetite, not on
resource amounts or colony count. Starts are selected from the finished landscape;
settlement never stamps home islands or edits the coastline. Invalid geometry is
reported, not repaired by filling bite marks or constructing bridges.

## Construction and play

A few low-frequency harmonics shape the island. Each bite combines a broad jaw
with round tooth cuts along its curved front. Teeth retain their proportions on
rectangular maps. Shallow, deep and slightly lopsided profiles, uneven placements
and varying tooth sizes avoid a repeated mechanical stamp. Narrow terminal slivers
and tiny detached specks are removed before beaches.

Ponds are scattered in the interior, keeping away from the outer coast. Correlated
woodland patches leave grass clearings; dry inland wood provides finite reserves.
Colonies occupy roomy fertile sites, separated by land travel where possible.
The site search requires connected local capacity for both farms and buildings.
Detached starting islands additionally need at least 1,200 pure-grass tiles, 400
inland service tiles and ten separate 4×4 service footprints with two-tile spacing
per colony. At least two service footprints per colony must remain clear after
settlement. Smaller fragments remain scenery. These budgets prevent narrow fertile
crescents from filling with crops before their colonies can build local pools.
Each colony gets nearby crops and construction room, including space for its own
pool. Deposit-only repairs open routes between colonies sharing an island and give
each colony an approach to its shore. They never bridge the sea.

No simulation, growth, save-format or network rules are changed. Generated maps use
the existing serializer and ordinary terrain/resource behavior. Wood can regrow near
water, so an opening route is not a promise of permanent unrestricted movement.

## Reproduce and inspect

```sh
scons release=1 server=0 -j6 build/src/glob2 map-generator-defaults-test map-generator-study
build/src/glob2 --generate-map who-ate-the-map --seed 7 \
  --width 128 --height 128 --teams 4 --set appetite=2 \
  --output artifacts/eaten.map --preview artifacts/eaten.png --json artifacts/eaten.json
build/src/MapGeneratorDefaultsTest artifacts/eaten-test-profile --eaten-only
```

Use the [map CLI](CLI.md) for previews and reports and the map-design skill's
`control_study.py`, `game_economy.py` and `tournament_starts.py` for parameter and
play studies. The [retained evidence archive](https://github.com/Globulation2/glob2/tree/evidence/who-ate-the-map)
contains previews, request manifests, per-case study results, game summaries, a
40,000-tick saved game, and profiling commands/results. Its
[EVIDENCE.md](https://github.com/Globulation2/glob2/blob/evidence/who-ate-the-map/EVIDENCE.md)
records the revision each result covers and the archive's scope. Historical
artifacts use provisional numeric ID 60; the integrated generator uses ID 66.
