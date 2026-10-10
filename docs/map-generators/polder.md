# Polder

Reclaimed land, all of it: the whole torus laid out in rows of crops with a ditch of water between every
two, sand dykes across the rows at intervals, small grass villages for the colonies and hamlets
between them. Food is effectively unlimited; the game is logistics, on the dykes and the ditches' beaches,
until someone can swim.

- **Rows.** Stripes that wrap the torus exactly (`stripePhase`), 10 of crops and 6 of water along an
  axis (a period of 16 divides every map side; the yield fit's 10 and 8 would give 18, which does not,
  for under 3% of yield) and 11 and 5 on the diagonal. `row-angle` is Random by default: an angle drawn per map and rounded to whole turn counts on a
  circle of `s / 16`, so the rows stay 16 apart and the angle is one of some fifty on a 256 map;
  Vertical, Horizontal and Diagonal are fixed. Dykes every
  `dyke-spacing` tiles (12-48, 24) along the rows, two corners of sand wide, laid by the phase along the
  rows (`alongStripes`) over the ditches and through the crop rows, as every
  farm-row map's bridges are, so a row still under crop is never a wall a worker walks the length of.
  `water-crossings` and `crop-crossings` (both on) switch each half (`FarmBridges`). With the ditches
  uncrossed the villages and hamlets are the only ways over a ditch that wraps the torus, and a seed
  whose colonies they cannot join fails validation.
- **Villages.** Grass discs of `village-size` (8-20, 14) on a lattice, shrunk so a whole row and ditch
  lie between two, each in a two-tile ring of sand that keeps crop growth outside the village; each holds a swarm, its quarry and a few buildings, and no wheat or wood
  blocks: the nearby rows supply food and construction material. Two and a half 10x4 farm
  plots per colony (`stampFarmPlot`) are spread through the rows, each the farthest a plot can stand
  from every village, hamlet and earlier plot, and at least 18 tiles from any village. `hamlets` (on) puts a
  grass disc of radius 5 half way between neighbouring villages, room for a forward inn and a tower, with
  a fruit grove.
- **Fields.** 55% of the fertile row ground under wheat and 8% under wood, in patches; every row tile is
  a few tiles from water, so it all regrows. Only crops could close a lane, so `openColonyRoutes` clears
  only crops.

## Implementation source

[PolderGenerator.cpp](../../src/map/generator/generators/PolderGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
