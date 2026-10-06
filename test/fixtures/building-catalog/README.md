# Building catalog compatibility fixtures

`partial-repair135.game.gz` is a binary format 135 save produced by the unchanged
engine at commit `86df5ea49ef7fdf7914a57bf72bb527043876373`, using its engine test
support and release objects. It contains one level-zero inn repairing from half
health. One wood delivery has occurred: building GID 0 is site type 2, has 166 HP,
and its legacy inventory contains two wood (one healthy-material credit and one
delivered material). One further wood delivery is required.

The catalog loader must preserve the remaining cost and health without importing
legacy repair credits as spendable inventory. The regression also checks that a
new-format save of this imported job continues identically.

To regenerate with the original engine, create a `HeadlessGame` with the default
race, add a finished inn at (8,8), set its HP to half `getEffectiveMaxHp()`, call
`launchConstruction(1,1)` and `tryToBuildingSiteRoom()`, deliver one wood through
`addResourceIntoBuilding(WOOD)`, and save through `BinaryOutputStream`. Compress
with a zero gzip timestamp. Do not regenerate using the new repair accounting.

Uncompressed SHA-256:
`7042a7400248e4e5dcb78e617785cb6a21116300a331c71f4540cb008af3cbe7`.
Compressed SHA-256:
`532f293dd638565b7b9dcf82db0538786b6b180d7c26b239c519b7b915e55f32`.

## Retained compositions

`composition/seed-{713,714,715}.manifest.json` load fixed, identity-free catalogs.
The first two combine feeding, healing and training; independently priced unit
recipes and projectile combat; shared storage and direct withdrawal; and all three
attraction roles on rectangular overlays. Seed 715 removes feeding and production.
The files retain randomized footprints, display tiers, recipe durations and costs,
and definition ordering. They use installed artwork.

`composition/generate.py` documents the seeded construction. Run it deliberately
when designing a new fixture, then review and retain the resulting JSON. Tests
load the committed definitions rather than regenerating from changing stock data.
The engine harness checks every continuation tick, inventories and reservations,
plus exact production resource conservation including cancellation. Retained
traces cover the new custom rules; they are distinct from stock parity evidence.

`terrain136.game.gz` was produced by unchanged master `71d7eee1b` with
`--run-game --map-file maps/SmallForTwo.map.gz --game-seed 716 --player numbi
--player castor --ticks 257 --save final --replay false`. It exercises the format
136 embedded terrain catalog alongside frozen building definitions and active AI
state. The regression imports it, resaves to the current format, and compares
subsequent AI orders and simulation components. Compressed SHA-256:
`2c9945b6b909b8ad014ec488c1eeb2091049f23b331eb5fbd7dd17e2249b1d6a`.

## Authoring example

`authoring/manifest.json` and `authoring/field-kitchen.json` are the complete
experimental feeding/healing example in
[the building authoring guide](../../../docs/features/building-catalogs.md#complete-field-kitchen-example).
The `BuildingCatalog` suite loads these exact files and checks admission, storage,
service costs, staffing and enabled/disabled availability. Keep the guide's JSON
example synchronized with the definition. The small catalog has no starting
colony; the guide shows how to add its definition and experiment to a stock copy.
