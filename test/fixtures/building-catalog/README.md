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
