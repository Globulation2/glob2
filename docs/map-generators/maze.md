# Maze

A maze in the pen-and-paper sense, built to be lived in: grass passages that colonies farm and
build out into, separated by thin stone-and-water walls, with every colony in its own
cul-de-sac.

- **Cells.** The map is tiled into cells on its own torus (`Tessellation`). With `cell-shape`
  Squares (the default), cells are about `cell-size` tiles across, with boundaries chosen so the
  cells tile the map exactly (neighbouring cells differ in pitch by at most one tile). With
  Hexagons, pointy-topped hexagons in offset rows sit about one and a half `cell-size` apart
  (`kHexPitchPercent`), stretched a little so an even number of rows wraps; a hexagon's slanted
  doorways are shorter than a square's, so they need the extra room. Either way the maze wraps
  across the seam like any other boundary, and at least two cells are needed in each direction.
- **Warp.** `warp` (0 by default) moves every corner of the tiling at random, up to that share of
  what keeps every cell whole (`warpCorners`), so squares and hexagons become irregular polygons
  that still tile. It runs after the maze is carved, so it knows which edges are walls: walls that
  share no corner (and the ponds below) never come closer than the narrowest passage allows, and
  no wall comes closer to a cell's centre than half that, so a warped maze keeps every guarantee
  of an unwarped one. At small cells there is less room to spare and warp is gentler.
- **Maze and homes.** Homes are chosen first, by farthest-point spreading between cell centres
  that only accepts a cell if every home still has a non-home neighbour and the non-home cells
  stay connected (`spreadPockets`). The pattern depends only on the tiling, so `validateRequest`
  checks exactly how many colonies fit; each map then places it at a random translation and mirror
  image of the lattice. A recursive backtracker carves a spanning tree of passages over the
  non-home cells, and each home is attached by exactly one passage, so every colony starts in a
  genuine dead end. `loopiness` knocks through extra walls between non-home cells only, so homes
  stay cul-de-sacs.
- **Terrain from one distance field.** Only walls are drawn. Every closed edge gets a stone spine,
  a sealed line (`traceSealedPath`) from its corner tile to the next, so walls meeting at a corner
  share its tile and none can be slipped between at any angle. The vertices are then designed from
  the steps to the nearest spine corner: one step out stays land, the next `channel-width` + 1 are
  water, and everything further out is grass, which `layBeaches` edges with sand. In tiles that
  is the spine, two sandy flank tiles, `channel-width` all-water tiles (default 2), a two-tile
  shore and then passage; STONE only places on a pure-grass tile, and the spine's own corners are
  grass. A corner where every edge is open gets a pond as wide as a wall's water instead, so a
  junction of passages still reads as one. Passages are simply the ground no wall comes near, so
  every cell is a grass chamber and every doorway as wide as its walls allow.
- **No walking along a wall.** A wall's sandy flanks are walkable land, so they are always kept at
  least one all-water tile from every passage's shore — a unit can't step across a tile it can't
  stand on. A doorway between two walls is therefore `shortest edge / 2 - 5 - channelWidth` tiles
  either side of its middle; `validateRequest` rejects settings that would leave a passage
  narrower than 9 tiles.
- **Roads.** A three-tile sand road runs down every open passage, from each cell's centre through
  the doorway's middle to the next centre, traced as a sealed line so its pure-sand tiles always
  share a side. Every cell is linked to the rest of the maze by ground that can never be closed:
  `Map::incResource` only seeds a resource on its own terrain and buildings need pure grass, so
  nothing grows over a road or is built on one. Roads only turn grass to sand, never water, and
  at a home the road stops against the swarm's footprint. Shore distances for the resource
  scatter ignore the road. With `sand-roads` off (on by default), passages are grass from shore
  to shore, open to farmland and buildings.
- **Resources.** Every home starts identical: fixed 1:1 wheat and wood on the tiles within three
  of the shore to the left and right of the home's door, growing forward from its back wall; a
  compact stone deposit at the back wall's centre; and a clear square around the swarm. Positions
  in a cell are measured along and across its exit in eighths of a tile, so any cell shape sorts
  its tiles the same way on every platform. Outside the homes, clumps of wheat, wood and stone
  (densities per 256 shore tiles) are scattered along every passage's shores, never more than
  three tiles in, so each passage keeps a clear lane down its middle however the maze turns.
  Fruit is treasure: every dead end that isn't a home gets one compact patch of nine fruit tiles
  (at 100% `fruit-amount`) near its far end, with fruit types dealt round-robin so every kind is
  somewhere in the maze. With
  `dead-end-treasure` off (on by default) the same fruit is scattered along the passages' shores
  instead. Algae is seeded along the channels.
- **Checked, not assumed.** `validateWorld` floods walkable tiles (water, buildings and every
  resource, including wall spines, block it) from colony 0's workers and fails the candidate if
  any colony isn't reached. It then rebuilds the maze from the request and checks the walls hold
  (`firstRegionLeak`): no ground the colonies can reach joins two cells except where the open
  edges round a shared corner connect them.

## Implementation source

[MazeGenerator.cpp](../../src/map/generator/generators/MazeGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
