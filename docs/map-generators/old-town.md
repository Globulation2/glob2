# Old town

A city of stone blocks and narrow grass streets with farmland all round it, open on every side.
Streets are buildable and buildings block walking, so every building a player puts up closes a street:
the players build the city's fortifications themselves, and a tower on a street fires over the block
into the next.

- **City.** A disc of `city-size` percent of the half side (40-90, 70), tiled into blocks of
  `block-size` (10-20, 14) warped by `warp` (0-100, 50); the band `street-width` (3-7, 4) along every
  cell border is street, everything else in a city cell is a stone block. There is no wall: the first
  play found a walled city unfair to whoever started far from a gate.
- **Plazas.** Blocks left open, each filled by a fountain pond to a tile short of its streets (radius
  4.0 at the default block and street, two and a half times the first build's), with a grove of one
  fruit in turn beside every plaza's pool. A home plaza is two adjoining blocks (a single block
  of 14 less its streets, with a fountain in it, has no room for a 4x4 swarm; measured: every settlement
  failed), the fountain in one and the swarm in the other, the pairs as far apart as the city allows;
  `plazas` (0-4, 2) more per colony farthest from those, given up first when the city is small; and the
  cathedral square at the cell holding the centre, with an orchard of the three fruits round its fountain.
- **Tendrils.** From the fields' sand cap, one every 16 tiles round it (some thirty-five on a 256 map), a
  wavy road of sand two tiles wide and 10-13 tiles long runs in towards the city (`tendrils`, on): across the margin and into the outer streets, notching
  the outer blocks where it meets them (the second and third plays asked for "little tendril sand
  roads extending inwards", short and frequent).
- **Fields.** Outside the city, farm rows along the map's axis (`layFarm` at `bestFarmRows`, bridged
  clean across every 16, water and crop rows alike, under `water-crossings` and `crop-crossings`, both on) with `farm-plots` (0-6, 3) 10x4 building plots per colony spread through them
  (`stampFarmPlot`, each the farthest a plot can stand from the city and the plots before it), half
  their fertile ground under wheat and a twentieth under wood, no outcrops; the plazas inside get a
  light share so they stay open. Every kit has no quarry: the blocks are stone.
- **Checked, not assumed.** Every fountain present, every colony walkable from the first through the
  streets.

## Implementation source

[OldTownGenerator.cpp](../../src/map/generator/generators/OldTownGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
