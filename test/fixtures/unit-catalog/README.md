# Unit catalog fixtures

`combinations.json` defines the locked capability combinations used by the full-engine
`UnitCustomization` suite. The suite exercises actual assignments, packet accounting,
combat, services and save continuation. `ablation-checksums.txt` is the current
per-tick simulation checksum trace; it is regenerated deliberately when the
simulation identity changes. The checksum does not cover every cached unit field.
The suite's explicit state, inventory, reservation and continuation assertions
supplement that trace.

Phase continuation cases save during active water-only pickup, harvest and delivery;
meal, healing and training entry, interior work and exit; mixed melee and magic with
cargo and fractional regeneration; starvation; and pending or completed conversion.
Every resumed tick compares the complete checksum vector, serialized unit caches
and private RNG, ordered service membership, reservations and exact cargo fractions
with the uninterrupted simulation.

`legacy152-stock.replay.gz` is a retained 128-tick replay produced by master
`69ef3e1b2bf453aa280a69be72f83167b1511d9d`, before unit catalogs. It starts from
SmallForTwo with seed 19 and Cortex/Maxima, and includes the original recorded orders
and simulation checksums. `EngineSession` loads this replay through the real replay
reader and runs every recorded tick using the version-152 representation adapter.
Keep this historical replay unchanged: it verifies accepted replay execution across
the unit-catalog boundary independently of current golden regeneration.

`legacy152-team3-ranged-stats.bin.gz` retains the fourth team's exact format-152
`TeamStats` bytes from master `69ef3e1b2bf453aa280a69be72f83167b1511d9d`,
FourSquares, seed 713, checkpoint tick 28,672. A tracking binary reader observed
the section boundaries during the old engine's normal load; no current writer
reconstructed these bytes. The slice spans offsets 1,794,809 through 2,140,457
(exclusive) of the decompressed save, and contains 345,648 bytes. Its SHA-256 is
`004510982f2bff134e13db08ddb810b4b2ce1bd7b1bca1143f14e825dee86e0c`;
the source compressed checkpoint SHA-256 is
`3fa5b68bee0dc310a540691d964b8854dc7f27bf838b7495ae52da6be6aaa0cf`.

The saved explorer has positive air damage at every level. One of its 32 explorers
also learned ground magic, so air and ground level histograms overlap. The
`TeamStatsSave` regression verifies that migration counts those physical explorers
once across all 128 historical rows, then preserves complete current statistics
and simulation state through a full sampling interval and repeated resaving.
Keep this historical byte slice unchanged.
