# Carousel

A ring of walled homes round a plaza, where every colony besieges one neighbour and is besieged by
the other. A colony's single door opens onto a narrow corridor that follows the ring to a small court
(the elbow) pressed against the next colony's home, and a narrow spoke runs from the court to the
plaza. The court and that home are parted by a thin wall of rock that no unit crosses but a tower
shoots over, so every colony's towers, in its own home against that wall, cover its neighbour's only
way out. The concept depends on the lanes being tight, so they default narrow and every wall is a
single line with no water beside it. Inside the ring the sea stays: the plaza is an island
in a lagoon with algae in it, and the spokes cross the lagoon between bands of cliff to keep the lagoon distinct from the outer farmland.

## Ground

Revision 5 draws every wall, coast and road from the terrain catalogue, where earlier revisions
stood the stone resource on grass and drew roads in sand. The design uses one terrain per rule, and a
last pass paints one of four climates (temperate, arid, highland, volcanic; `carousel-climate`), the same
for every colony, with looks that keep those rules (`shared/Climates.h`). Walls are drawn on
vertices and lean away from the open ground beside them (`wallVertices`), so no lane or door
loses a tile to a wall. Since revision 9 every wall, the siege's Ridges and the Obstacles cliffs
alike, is at least three vertices thick (`thickenRidges`, given the wall's rock), because a rock look
draws only on tiles with four rock corners and a thinner wall, above all a diagonal one, reads as
bare ground. A wall grows into open ground, the lagoon or the open sea, never a lane or the plaza; a
siege wall may also take a farm's edge (its grass, its cap ring and its crossings, never its water
rows), and a cliff its grass and cap ring but not its crossings. Revision 7 thickened only the
Ridges walls, away from every farm. Barren and fertile ground take one look each, no longer
speckled with a second.
There are no beaches: grass meets the water, and a coast that must be sealed is cliff. A tile
whose corners mix grass and water is a shoreline in the engine (`combineCornerRules`), so since
revision 8 no wheat or wood is planted or regrows on the water's edge itself; the crops start one
tile in, and `validateWorld` checks it. Following the
maintainer's review (2026-11-07), sand appears only where the subject is sand. Revision 9 paves the
lanes from wall to wall (revisions 5-8 ran rough ground down their middle so nothing could be built
in them) and draws the sea as one plain water.

| Ground | Rules | Where | Why |
| --- | --- | --- | --- |
| Court and corridor walls | Ridges | Every wall touching a lane or a court | The siege: a tower in the next home shoots over it into the court |
| Borders and quays | Obstacles | Every other wall, the lanes' bands along the lagoon (`laneBand`) | No tower shoots through a border; a spoke crosses the lagoon on a cliff-sided causeway |
| Sea cliffs | Obstacles | Every coast of the open sea and the lagoon (`cliffCoast`) | No beach to land on: nothing gets in from the sea |
| Lanes and courts | Paths | Every corridor, court and spoke, wall to wall | Quicker to walk, and buildable like any road |
| Promenade | Paths | The plaza from nine tiles beyond its pond to its shore | Where the spokes come in, paved and buildable round the orchard |
| Farms | Grass in the climate's farm look; crossings Paths, caps and plot rings Barren (`dressFarmSand`) | Every farm | Farm ground reads by climate; no sand stands in for a road or a ring |
| Base | Barren, paved round the swarm | Every home, but a grass verge four vertices wide at the foot of its walls, which the paved square also keeps off | Food grows on the farms, so the base is the climate's barren ground; the verge holds the quarry faces and the towers against the walls (a tile mixing grass and barren corners holds no building) |
| Lava | Lava | The plaza's pond, in the volcanic climate, drawn in concentric rings (ember field at the rim, lava, a molten core) | Volcanic maps show lava at their centre |
| Water | Water | The open sea, the lagoon and the farms' rows | One plain water, with no deep water |

Every home has two quarry faces of 12 stone each at the foot of its outer walls, away from the siege
line: the walls were its stone when they were built of it. Since revision 6 every starting tower also
has a face of 6 stone at the foot of the wall it stands against, within four tiles of it, so its
carriers restock in a few steps instead of a dozen or more; where that foot is barren or paved, a
grass niche is opened for it first. Both are unscaled.


- **Geometry** (`geometryFor`, `drawLanes`). Designed once in the wedge's frame and turned for every
  colony; round on a rectangular map. The homes' outer edge is `kRingShare` of the half side (a little
  more on smaller maps, and more on a crowded ring), and a home is `kHomeShare` of the half side but
  never wider than a fifth of the ring's arc per colony. The court sits on the homes' ring where its
  edge is `court-wall` tiles from the next home's rough outline, found by solving for the chord. The
  corridor (`corridor-width`, default 3) is an `arcPath` from the home's axis to the court; the spoke
  (`spoke-width`, default 3) leans back from the court to the middle of the wedge so it clears the next
  home. The court is a circle of `court-size` (default 30, about 3 tiles in radius).
- **Pieces** (`stampPieces`, `crowding`). Homes (`stampRoundHome`, no ponds) and courts over the lanes;
  the court wall is just the land within `court-wall` tiles (default 2) of both the court and the next
  home; the plaza with its pond. A ring too crowded for a wall between a lane and another home, or for
  two colonies' lanes to stay apart, is refused.
- **Farms** (`claimFarmFields`, `layFarmRows`). `growFarmFields` grows every colony one field out
  beyond the ring, shared out
  by equal yield for the colony's row angle, so a colony whose rows fall on the diagonal gets more
  ground, and kept only where every colony's field has room. Once the walls stand (below), `layFarm`
  lays rows along the colony's axis at `bestFarmRows` widths over the whole walled field, keeping
  three tiles of land round the water (six against open sea), with a sand cap closing the crop rows,
  a sand bridge clean across the whole farm, water and crop rows alike, every 16 tiles (`water-crossings` and `crop-crossings`, both on, switch
  each half) and, with `farm-plots` (on), a 10x4 building plot.
  `plantFarm` plants wheat along the water with one small woodlot. The farms are the homes' only
  water.
- **Filling in and walls** (`fillAndWall`). The sea outside the ring within `kFillReach` (24) tiles
  of a home or farm becomes that colony's ground (`fillToNearest`); the lagoon, every tile of sea
  inside the ring through the homes' centres, is never filled; lanes, courts and the plaza never
  grow, so they keep exactly their drawn width. Sea left keeps a `kSeaMargin` (3) strip of the
  nearest piece before its coast, which is sealed in cliffs (`cliffCoast`); a lane or court keeps
  `kLaneMargin` (5) instead, and all of that margin is cliff (`laneBand`), since a beach's mixed
  tiles and a sealed coast reach three tiles in where a coast runs diagonally, which left a diagonal
  spoke on the 3-tile margin with no grass down its middle. Every tile then has a side (the plaza; a
  colony's court and lanes; a colony's home and farms), and a single wall stands wherever
  two sides meet (`labelBorders` with doors), always on the home's or farm's side, so no wall
  narrows a lane. Two borders stay open: each home's door onto its own corridor, and each spoke's
  way into the plaza.
- **Roads** (`finishRoads`). The lanes are paved from wall to wall; with `sand-roads` (on), a road
  line runs down the corridor, through the court, down the spoke and across the plaza to its pond,
  and nothing is planted and no tower stands on it, so the way in can never be fully overgrown. Its
  ground is the lane's paving, not a rough strip. Roads keep `kRoadSeaGap` from water and never
  touch a wall tile.
- **Homes and prizes** (`furnish`). Scattered farmland on every home's fertile ground
  (`furnishGround`) and no starter kit of wheat and wood blocks: the farm feeds the home, and
  `secureStartingCrops` supplies missing opening crops;
  nothing is planted within six steps of a lane. One grove of one fruit per court, and on the plaza
  only fruit: an orchard of the three fruits between every two spokes' arrivals, with no wheat or
  wood to smother it. Algae seeds the lagoon's shallows.
- **Starting towers** (`planTowers`). `starting-towers` sets their level (0 none, when every site is
  an open pad; default 1, leaving upgrades to players) and `tower-count` how many
  (default 3), with three open pads. Every
  site stands in the colony's own home, directly against a wall and within six tiles of one, chosen
  for how much of the previous colony's elbow (its court and lanes within twelve tiles) it covers
  (`chooseTowerSites` counting other colonies' elbows only). `settleStartingTowers` drops any site
  that would close a colony's walk to the plaza and evens the counts. Since revision 10 every
  starting tower opens with a full magazine and a full stone reserve (`raiseTowers`), including
  towers within range of a neighbour's.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires its terrain vertex for vertex,
  every colony reachable, no land reachable from the sea (`seaEntry`), 95% of every farm's crop land
  and all of its plot a walk from its colony (`farmReachable`), every designed border standing as wall
  except at the doors, every home, farm, court and the plaza apart with the lanes shut (`pieceLeak`), a
  level-1 tower in every home within range of the previous colony's court (`towerReach`), and even walks
  to the plaza (`walkSpread`).

## Implementation source

[CarouselGenerator.cpp](../../src/map/generator/generators/CarouselGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
