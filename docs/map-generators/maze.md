# Maze

A maze in the pen-and-paper sense, built to be lived in: an overgrown jungle temple of cliff-walled
passages that colonies farm and build out into, with every colony in its own moated keep at the end
of a cul-de-sac. Revision 17 (Maze 2.0) rebuilt it on the terrain catalogue. Until then each wall
was a line of stone resource in a water channel with sand beaches, a single wall that blocked
movement, was every colony's quarry, watered the crops and held the algae. Each of those jobs now
has terrain of its own.

- **Cells.** The map is tiled into cells on its own torus (`Tessellation`). With `cell-shape`
  Squares (the default), cells are about `cell-size` tiles across (28 by default), with boundaries
  chosen so the cells tile the map exactly. With Hexagons, pointy-topped hexagons in offset rows sit
  about a quarter further apart (`kHexPitchPercent`). Either way the maze wraps across the seam like
  any other boundary, and at least two cells are needed in each direction.
- **Warp.** `warp` (0 by default) moves every corner of the tiling at random, up to that share of
  what keeps every cell whole (`warpCorners`), so squares and hexagons become irregular polygons that
  still tile. Walls that share no corner never come closer than the narrowest passage allows, and no
  wall comes closer to a cell's centre than a keep needs, so a warped maze keeps every guarantee of
  an unwarped one.
- **Maze and homes.** Homes are chosen first, by farthest-point spreading between cell centres
  (`spreadPockets`). The pattern depends only on the tiling, so `validateRequest` checks exactly how
  many colonies fit; each map places it at a random translation and mirror image of the lattice. A
  recursive backtracker carves a spanning tree over the non-home cells, and each home is attached by
  exactly one passage, so every colony starts in a genuine dead end.
- **Walls.** Every closed edge is a temple wall `wall-thickness` tiles thick (5 by default), drawn as
  cliffs (Obstacles: nothing walks, swims or shoots through) on vertices that lean away from the
  open ground (`wallVertices`), so a wall shows about half its thickness as pure cliff. Swimming
  gets nobody through the maze. Explorers still fly over it.
- **Rubble breaches.** `loopiness` knocks extra openings through walls between non-home cells, as
  in the original maze, but each is drawn as a collapsed wall: a scree gap (Rough: walkable, slow,
  unbuildable) between the broken wall's stubs, with a heap of mineable stone rubble on each side.
  A breach is a flanking route, a little slower than a doorway, and a source of stone. Homes stay
  cul-de-sacs.
- **Moated keeps.** Every home is a keep: inside its cliff walls a moat `channel-width` tiles wide
  (2 by default; the control kept the original channel width's id) runs along every side but the
  back, with sand banks. The keep's fields line the moat's inner bank and a paved court surrounds the
  swarm; every other open tile of the keep is spring meadow (Fertile: buildable crop land that
  waters the ground round it), since revision 18. One boardwalk bridge carries the road across the moat at the door. The moat leaves a verge at the back for the quarry, opening further
  forward on a warped or hexagonal cell until the verge has room.
- **Water where the food should grow.** Junction cells (three ways or more) hold a fountain in a
  paved plaza. `gardens` (15% of the non-home cells by default) are garden cells with a pool at
  their centre. Every water body has a sand bank, so its algae regrows. Corridor and dead-end cells
  have loam beds along their walls' feet (Fertile ground, which waters itself and the ground round
  it). Revision 18 dropped the paved temple courts (a share of the cells paved wall to wall);
  those cells are ordinary corridors and junctions now.
- **Fair gardens.** Gardens are dealt so every home's nearest garden is the same number of cells
  away, two cells by preference (a short walk into the maze rather than a forecourt). Telemetry
  records each home's distance (`maze.garden.steps.min`/`max`), and `validateWorld` refuses a map
  whose homes differ by more than one cell.
- **Roads.** With `sand-roads` on (labelled Paved roads), an overgrown road (Paths: twice as fast,
  buildable, nothing grows) runs unbroken down every passage from cell centre to cell centre,
  through every doorway. No wall takes a corner of the road in a doorway and no deposit is planted
  on it, but being buildable, a road can be built over: unlike the original maze's sand roads, it
  does not guarantee that a door stays open.
- **Resources.**
  - **Home kits.** Every home starts identical: fixed 1:1 wheat and wood on the moat's inner bank,
    to the left and right of the door, growing forward from the back. Every home plants the least
    any home has room for, at most 32 tiles each. The quarry is 12 stone at the foot of the back
    wall.
  - **Ambient deposits.** Clumps of wheat, wood and stone (40, 20 and 8 tiles per 256 growing tiles
    at 100%) are scattered over the loam beds and within three tiles of every water outside the
    homes, never on the road.
  - **Fruit** is treasure: every dead end gets a compact patch of nine fruit tiles near its far end,
    with fruit types dealt round-robin. With `dead-end-treasure` off the same fruit is scattered
    over the growing ground instead.
  - **Algae** is seeded over all water.
  - **Rubble** puts 6 stone tiles beside each side of every breach.

  The amount controls scale the ambient layers, the treasure, the rubble and the algae; the home
  kits are unscaled.
- **The look.** One fixed jungle-temple palette (not the shared climates): cliff walls, overgrown
  road for every road, plaza and keep court (one paving since revision 18), a boardwalk bridge, scree breaches, spring meadow over
  every keep, moss over much of the other loam, and lush grass in the gardens and near water,
  split by fractal noise. Looks are painted over canonical mechanics with `composeLooks`, so a look
  never changes a rule.
- **Checked, not assumed.** `validateWorld` rebuilds the design and checks:
  - every colony walks to colony 0;
  - the terrain matches the design vertex for vertex;
  - no ground or water a colony can reach, swimming included, joins two cells except through the
    passages and breaches the maze opened (`firstRegionLeak`);
  - every wall vertex is an Obstacle;
  - no breach gap holds a deposit;
  - the homes' fields are equal and every quarry holds 12 stone;
  - the garden distances agree.

## What 2.0 changed in play

- **Food.** Water fell from about 12% of the map to about 2%. Food within 24 steps of each swarm is
  about 56% of revision 16's (growth potential, seeds 1 to 6, four colonies on 256x256), and within
  48 steps about 71%. That is still about twice Hedgerow Country's. The economy is less food-rich
  than the original maze's, by design: gardens and fountains are the food worth contesting.
- **Stone** comes from home quarries and breach rubble, no longer from every wall.
- **Travel.** Paved roads double walking speed down the passages.
- **More cells.** The default cell shrank from 32 to 28 with corridors about as wide, so a 256 map
  has 81 square cells rather than 64.

## Implementation source

[MazeGenerator.cpp](../../src/map/generator/generators/MazeGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
