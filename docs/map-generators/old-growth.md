# Old growth

A dry continent under unbroken forest: every colony starts in a big clearing with a pond, a ring of
pools, wheat on every shore and a ring of sand round it, and beyond the sand stands wood in every
direction. There is no other water but a few lakes far from
every home, so almost none of the forest ever grows back: what a colony cuts stays cut, the map opens as
the game goes on, and contact happens only where someone has cut through.

- **Homes.** Radius `home-size` (12-24, 20) with `home-pools` (0-8, 5) pools of radius 2.5 on a ring
  at 60% of it, so the whole clearing is within the growth probe's reach of water; two wheat patches
  and a quarry by the central pond and 12 wheat on every pool's shore, and no wood inside (first play:
  "clearing the wood hurts workers"; the forest edge is the woodlot, within the crop guarantee's 32
  steps). A two-tile ring of sand round the clearing keeps the forest from spreading in, since crops
  spread only onto grass.
- **Forest.** Wood on `forest-density` percent (60-100, 90) of the forest ground, the gaps drawn from a
  noise field so they are small openings; below the 8-connected site percolation threshold (about 41%
  open) the openings never join into a way through, so at any density offered the forest is a wall
  that has to be cut (`plantCover`).
- **Lakes.** `lakes` per 128x128 of map (0-4, 1) of `lake-size` tiles (40-160, 90), each at the tile
  farthest from every clearing and every lake so far (`distanceSquaredTo`) and grown by distance with a
  little noise; ringed with wheat and an orchard of the three fruits, the only ground where wood
  regrows and the only fruit not hidden.
- **Hidden groves.** One per colony (`hidden-groves`, on): a pocket cut in the forest at 65% of the
  cutting cost half way to the nearest rival, on its own side (`equalCostSites` over
  `StepCosts::chopping`), holding a fruit grove, a wheat patch and a stone clump. Costs are measured on
  the planted forest, so the groves lie at the same worker-hours from every home.
- **Trails.** `trails` (off) cuts a trail from every colony to the first before the game starts
  (`openColonyRoutes`). Without it the colonies start entirely apart, and the validator checks the map by
  cutting cost (`contactMatrix`) rather than by walking.
- **Checked, not assumed.** Every pond present; the forest dry beyond the reach of any water (no growth
  chance on the finished map's own field where nothing waters it); every colony reachable from the
  first, by walking with trails, by cutting without.

## Implementation source

[OldGrowthGenerator.cpp](../../src/map/generator/generators/OldGrowthGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
