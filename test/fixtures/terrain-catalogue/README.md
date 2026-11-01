# Terrain catalogue fixtures

`custom-registry-137.game.gz` is a format-137 saved game written by an
origin/master build at revision `16970b936` (seven built-in terrains), before the
terrain catalogue raised `TERRAIN_COUNT` to 31. It embeds two runtime terrain
definitions, `fixture:bog` (water base, id 7) and `fixture:mud` (grass base, id 8),
painted at (4,4), (5,4) and (6,6) on a 32x32 grass map beside ice (10,10), trail
(12,10), sand (14,14) and water (16,14).

`TerrainPropertiesTest.cpp` loads it to verify that custom IDs and tiles from older
files are renumbered behind the current built-ins, that classic cells are untouched,
and that a resave reloads identically at the current format. The throwaway generator
case lived only in the working tree; it is not committed.

`custom-registry-186.game.gz` is a format-186 saved game written by an origin/master
build at revision `ddfa81a40` (`TERRAIN_COUNT` 29), before the grass and sand looks
raised it to 33. It embeds `fixture:bog` (water base, id 29) and `fixture:mud`
(grass base, not buildable, id 30), painted at (4,4), (5,4) and (6,6) beside chasm
(8,8), ice (10,10), trail (12,10), sand (14,14) and water (16,14).
`TerrainPropertiesTest.cpp` checks that built-in IDs load unchanged, custom IDs
and their vertices move up by four, and a resave reloads identically.
