# Canals

A lagoon city: the map cut into blocks by a grid of narrow canals, every canal just wide enough to stop
a unit and just narrow enough for a tower on one bank to shoot the other, and only a handful of sand
bridges. Towers reach across from the first minute and armies cannot, so where the first towers go is the
opening; once swimming pools are built every canal is a road.

- **Blocks.** A tiling of `block-size` (16-40, 24) - squares, or with `block-shape` hexagons
  (a hexagon's pitch is 150% of the size, so
  its blocks hold about as much as the squares) - warped by `warp` (0-100, 80), every edge an
  obstacle the warp keeps apart (`warpCorners`), every edge stroked `canal-width` corners of water
  (3-5, 3). A canal's water is a corner narrower than the stroke and a diagonal canal is sealed against a
  diagonal step only when its water is two tiles thick, which is why the narrowest offered is 3; a
  straight canal w corners wide puts the banks' grass w + 4 apart (`Channels`), so 3 is reached by a
  level-2 tower and not a level-1.
- **Homes and block kinds.** The colonies' blocks are the ones farthest apart on the block graph
  (`spreadPockets`), dealt to the colonies at random, each with a kit and no pond: the canal waters the block. Every other block is dealt one of fourteen kinds and a
  facing from a weighted draw (6/9/8/10/6/5/7/7/4/9/8/7/6/8 in 100): plain fields; a lake; an orchard
  round a small pond; a homestead (a 4x4 pad of grass in a ring of sand, `stampFarmPlot`, with a wheat
  and a wood clump beside it); a hamlet of two pads; a quarry; a woodlot; a wheatfield; a dune of bare
  sand; and five fortified kinds built of stone or water: a fort (a 13-tile square of wall round a pad with one three-tile gate), a bastion (four
  corner walls, an opening in every side), a funnel (two walls in a V onto a three-tile gap with the pad
  behind it), a chicane (two staggered walls with a corridor between) and a moat (a ring of water round
  an islet with a pad and one sand causeway). Walls are drawn in the block's frame, only where the warp
  left pure grass clear of water, and a block whose walls would cut its land or a bridge off gets none
  (`blockWalls`). Every pad kind and built kind carries the ambient fields too, a tile clear of its
  walls. The varied block kinds provide local objectives. Fairness is statistical.
- **Bridges.** A tree of shortest block-to-block paths from the first colony's block to every other
  colony's, so every colony can be walked to, then `extra-bridges` percent (0-100, 30) of the blocks'
  count more at random (`openLoops`), so most blocks stay islands until someone swims. A bridge is a line of sand
  corners across the canal (a tile with a sand corner is no longer pure water) reaching onto both banks.
- **Towers.** `tower-count` towers at `starting-towers` level (default 2 of level 1) and two pads per colony, on its own bank against the beach (`chooseTowerSites` with `against`), covering the most of
  other blocks' land across the canal; none may close the colony's walk to a bridge
  (`settleStartingTowers`).
- **Fields.** Every block is fertile (its canal is within the growth probe's reach of all of it), so a
  modest share of every block goes under crops in patches, the bridges' landings kept clear; algae in
  every canal, sand being everywhere.

## Implementation source

[CanalsGenerator.cpp](../../src/map/generator/generators/CanalsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
