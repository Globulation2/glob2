# Carousel

A ring of walled homes round a plaza, where every colony besieges one neighbour and is besieged by
the other. A colony's single door opens onto a narrow corridor that follows the ring to a small court
(the elbow) pressed against the next colony's home, and a narrow spoke runs from the court to the
plaza. The court and that home are parted by a thin wall of stone that no unit crosses but a tower
shoots over, so every colony's towers, in its own home against that wall, cover its neighbour's only
way out. The concept depends on the lanes being tight, so they default narrow and every wall is a
single line of stone with no water beside it. Inside the ring the sea stays: the plaza is an island
in a lagoon with algae in it, and the spokes cross the lagoon between bands of stone to keep the lagoon distinct from the outer farmland.

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
  nearest piece before its coast, which is sealed (`sealCoasts`); a lane or court keeps
  `kLaneMargin` (5) instead, and all of that margin is stone (`laneBand`), since a beach's mixed
  tiles and a sealed coast reach three tiles in where a coast runs diagonally, which left a diagonal
  spoke on the 3-tile margin with no grass down its middle. Every tile then has a side (the plaza; a
  colony's court and lanes; a colony's home and farms), and a single line of stone stands wherever
  two sides meet (`labelBorders` with doors), always on the home's or farm's side, so no wall
  narrows a lane. Two borders stay open: each home's door onto its own corridor, and each spoke's
  way into the plaza.
- **Roads** (`finishRoads`). With `sand-roads` (on), a sand road runs down the corridor, through the
  court, down the spoke and across the plaza to its pond, so the way in can never be fully overgrown.
  Roads keep `kRoadSeaGap` from water and never touch a wall tile.
- **Homes and prizes** (`furnish`). Scattered farmland on every home's fertile ground
  (`furnishGround`) and no starter kit of wheat and wood blocks: the farm feeds the home, and
  `secureStartingCrops` supplies missing opening crops;
  nothing is planted within six steps of a lane. One grove of one fruit per court, and on the plaza
  only fruit: an orchard of the three fruits between every two spokes' arrivals, with no wheat or
  wood to smother it. Algae seeds the lagoon's shallows.
- **Starting towers** (`planTowers`). `starting-towers` sets their level (0 none, when every site is
  an open pad; default 1, leaving upgrades to players) and `tower-count` how many
  (default 3), with three open pads. Every
  site stands in the colony's own home, directly against stone and within six tiles of a wall, chosen
  for how much of the previous colony's elbow (its court and lanes within twelve tiles) it covers
  (`chooseTowerSites` counting other colonies' elbows only). `settleStartingTowers` drops any site
  that would close a colony's walk to the plaza and evens the counts.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every designed stone,
  every colony reachable, no land reachable from the sea (`seaEntry`), 95% of every farm's crop land
  and all of its plot a walk from its colony (`farmReachable`), every designed border standing as stone
  except at the doors, every home, farm, court and the plaza apart with the lanes shut (`pieceLeak`), a
  level-1 tower in every home within range of the previous colony's court (`towerReach`), and even walks
  to the plaza (`walkSpread`).

## Implementation source

[CarouselGenerator.cpp](../../src/map/generator/generators/CarouselGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
