# Canals

A lagoon city: the map cut into blocks by a grid of narrow canals, every canal just wide enough to stop
a unit and just narrow enough for a tower on one bank to shoot the other, and only a handful of
boardwalk bridges. Towers reach across from the first minute and armies cannot, so where the first
towers go is the opening; once swimming pools are built every canal is a road. One grand canal, deep and
too wide for any tower, rings the city with a few bridges that are its fronts.

Canals 2.0 (revision 10) draws the city with the terrain catalogue in one fixed lagoon look. Brick
paves the quays, streets and campi, and boardwalk makes the bridges. The homes are tended lawns, the
country's banks are pale-sand beaches, the parapets are ridge rock and the hedges are garden hedges.
The marshes are clay and mudflat, the gardens are loam, and the grand canal has a deep-water core.

## Blocks
- A tiling of `block-size` (16-40, 24), in squares or, with `block-shape`, hexagons. A hexagon's
  pitch is 150% of the size, so its blocks hold about as much as the squares.
- The tiling is warped by `warp` (0-100, 80). Every edge is an obstacle the warp keeps apart
  (`warpCorners`, by the widest canal plus eight tiles).
- Every edge is stroked `canal-width` corners of water (3-5, 3). A canal's water is a corner narrower
  than the stroke.
- The narrowest canal offered is 3, because a diagonal canal is sealed against a diagonal step only
  when its water is two tiles thick.
- A straight canal w corners wide puts the banks' buildable ground w + 4 apart (`Channels`), so a
  canal of 3 is reached by a level-2 tower and not a level-1.

## The grand canal
- `grand-canal` is on by default.
- One row of the lattice's edges becomes the grand canal. That row is a closed loop round the torus,
  so it parts nothing. It is left out when there are fewer than three rows.
- It is stroked `canal-width` + 4 corners (7 by default), never narrower than the narrowest channel
  a level-3 tower misses (6), so no tower covers it. Pads and piazzas keep a vertex off every shore,
  so no paved tile reaches the water's edge.
- Its water two steps from the banks is deep, which slows swimming.
- The bridge tree prices a crossing of it at four ordinary crossings, so the tree crosses it only where
  the way round is longer.
- A quarter of its edges carry a bridge, at least one; those include the tree's crossings, and extra
  bridges never add any.

## Homes and block kinds
- The colonies' blocks are the ones farthest apart on the block graph (`spreadPockets`), dealt to the
  colonies at random.
- Each home has a kit and no pond: the canal waters the block. Each also has a brick piazza
  round its swarm, wide enough that every tile beside the swarm is pure paving.
- Every other block is dealt one of fifteen kinds and a facing from a weighted draw (out of 100):

| Kind | Weight | What it holds |
| --- | --- | --- |
| Plain | 5 | The ambient fields only |
| Lake | 7 | A lake at the middle |
| Orchard | 8 | The three fruits round a small pond |
| Homestead | 9 | A paved 4x4 campo, with a wheat and a wood clump beside it |
| Hamlet | 6 | Two campi |
| Quarry | 6 | A ridge-rock outcrop with faces of stone at its foot |
| Woodlot | 7 | Wood over most of the block |
| Wheatfield | 7 | Wheat over most of the block |
| Marsh | 5 | A salt marsh: a mudflat core (rough: slow, unbuildable, nothing grows) in a rim of barren clay |
| Fort | 8 | A 13-tile square parapet (Ridges: towers shoot over it) round a campo, with one three-tile gate |
| Bastion | 7 | Four corner parapets, with an opening in every side |
| Funnel | 7 | Two hedges in a V (Obstacles: they stop arrows too) onto a three-tile gap, with a campo behind it |
| Chicane | 6 | Two staggered hedges with a corridor between them |
| Moat | 7 | A ring of water round an islet with a campo, crossed by one boardwalk causeway |
| Garden | 5 | An orto: a pond banked in fertile loam, under wheat |

Walls:
- Walls are drawn in the block's frame, only where the warp left pure grass clear of water.
- They are set on vertices (`wallVertices`), leaning away from the gate, gap, corridor and campo, then
  thickened to three vertices (`thickenRidges`) so each reads as one mass.
- A block whose walls would cut its land in two gets none: a chicane becomes plain, anything else a
  homestead (telemetry `canals.block.walls-omitted`).
- Forts and bastions get a face of stone outside the back of their parapet, since the walls used to be
  the stone the colonies mined.
- Every campo kind and built kind carries the ambient fields too, a tile clear of its walls.

The varied block kinds provide local objectives. Fairness is statistical.

## Banks
- The banks of the homes and of the city's blocks (campi and built kinds) are brick quays, one vertex
  wide, so they spoil exactly the tiles a beach did and the tower arithmetic is unchanged.
- The country's blocks keep pale-sand beaches.
- A garden's pond is banked in loam; its canal shore is a beach.

## Bridges
- A cheapest tree of block-to-block crossings joins every colony's block to the first, so every colony
  can be walked to.
- Then `extra-bridges` percent (0-100, 30) of the blocks' count are added at random (`openLoops`), so
  most blocks stay islands until someone swims.
- A bridge is a boardwalk (Paths) stroked across the canal and onto both banks, longer where the
  warp slants the canal across it. A boardwalk is buildable ground, so a colony can wall a bridge.

## Towers
- `tower-count` towers at `starting-towers` level (default 2 of level 1), and two pads per colony.
- They stand on the colony's own bank against the shore: the design's bank tiles, since a quay's
  inland tile is no shoreline (`chooseTowerSites` with `against`).
- They cover as much of the other blocks' land across the canal as they can.
- None may close the colony's walk to a bridge (`settleStartingTowers`).
- Since revision 11 every starting tower opens with a full magazine and a full stone reserve
  (`raiseTowers`), so towers facing each other across a canal trade fire from the first ticks.

## Fields
- Every block is fertile, because its canal is within the growth probe's reach of all of it.
- A modest share of every block goes under crops in patches, with the bridges' landings kept clear.
- Paved quays and campi hold back no field, as sand did.
- Algae is seeded in every canal.

## Validation
The validator checks:
- The map's terrain against the design, vertex for vertex.
- That every parapet and outcrop is Ridges and every hedge is Obstacles.
- That every fully decked bridge tile walks.
- That moats and lakes keep their water.
- That every colony walks to the first.

## Implementation source

[CanalsGenerator.cpp](../../src/map/generator/generators/CanalsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
