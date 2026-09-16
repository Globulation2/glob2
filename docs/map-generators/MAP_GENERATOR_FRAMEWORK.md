# Map generator framework

A generator is a pure function, `bool generate(Game &, GenerationContext &)`, registered
alongside its metadata in a `GeneratorDefinition` (id, stable legacy numeric id, display name,
revision, its `GeneratorControl`s, and optional `validateRequest`/`validateWorld` callbacks).
`GeneratorRegistry::builtins()` holds the fixed list; `GenerationService` drives the actual
lifecycle (validate the request, sample candidate seeds, run `generate`, score the result,
validate the finished world). There is no generator superclass or pipeline dispatcher: a
generator is a function that calls the shared stages below in whatever order its map needs, and
a designed generator's body is a short sequence of them. What a generator keeps to itself is its
design — the layout as a pure function of the request — and the few routines that only make sense
for its map.

## The shared toolkit

One header per concern under `src/map/generator/shared/`. Everything down to Roads operates on
plain dimensions, masks and a `Map`, independent of `Game`; Settlements, ScoredSettlements, BalancedStarts, Pipeline
and Terrain take a `Game` because placing buildings and units needs its mutation APIs.

| Module | What it gives a generator |
|---|---|
| `Grid` | `Torus` wrap and offset arithmetic; `floodFrom`/`stepsFrom`, the one eight-connected breadth-first flood (with a step limit and the visit order for callers that need it); `walkableTiles` and `groundUnitTiles` passability masks; `unitTilesByTeam` and `firstColonyCutOff`; `Axes`, the map's axes turned so that u runs along its longer side and v across it, the frame of a belt that wraps the map the long way (Ring world, Braided river) |
| `Topology` | `connectedRegions` component labelling with explicit wrap and neighbour policy; `labelComponents` (assign component owners and report the first mixed-label component, keeping unlabelled connecting ground); `regionAdjacency` and `graphDistances` over sparse labels; `DisjointSets`, union-find for Kruskal-style networks (Stone highlands' passes, `carveNearTree`) |
| `Geometry` | `kPi`; `ShapeTransform` (invertible stretch and rotation); `Stretch`, which places a layout designed in a circle on a map's shorter side onto a rectangular map as an ellipse touching all four sides (`toFill`, `apply`, `undo`, `heading`), exactly the identity on a square map; `RadialShape`, a seeded rough outline with a per-angle `radiusAt()`; `stampShape` and `stampRoughDisc` into a label grid; `AxisFrame`, a frame along a heading from an origin (`at(along, across)` and `project`), in which a colony's trail, gates or towers are designed once; `Teardrop`, a blunt-headed tapering outline (two half-ellipses sharing the widest cross-section) with `halfWidthAt`, `contains`, `reach` under a stretch and `fitting` a packed radius |
| `Drawing` | Drawing on the torus: `strokePath`, a thick path of points each with its own half width, rasterized through the wrap; `tracePath`, a path traced one tile thick with no gaps (a sand road); `bezierPath`, a quadratic curve as such a path; `polarPoint`; `forEachTileInShape` and `fillShape`, a `RadialShape` filled at any centre and turned to any heading within its bounding box, optionally stretched into an oval; `stretchPath`, a designed path placed on the map by a `Stretch` with its half widths kept in tiles; `bentPath`, a tapered path leaving a point along a heading and bowing sideways; `wanderingPath` and `carveCorridor`, a path (and its stroke) that wanders from one point to another the short way round the torus, leaving and arriving exactly where asked, its width swelling and narrowing (a tunnel, a lane, a trail that doesn't look ruled); `pathClearance` and `pathBounds`, the water between two stroked paths (optionally ignoring a branch's root) and a cheap bounding circle; `growBranches`, a tree grown level by level by forking in two at every tip to a `ForkStyle`, every branch offered to a caller's `accept` before it is kept and a refused one retried once at half length; `arcPath`, a circular arc as a stroke; `zigzagPath`, a switchback trail in an `AxisFrame` with each leg's straight run returned apart from its turns; `ringWithGates`, every tile of a ring visited with the gate it lies in (a wall with ramps, a moat with bridges); exact fixed-point geometry for designs that must rasterize the same everywhere: `SubtilePoint` (16 units a tile), `sealedSegmentTiles` and `traceSealedPath` (a line no unit can cross even diagonally, at any slant), `forEachTileInPolygon` and `fillPolygon` (tile centres inside a polygon, shared edges split exactly, through the wrap); `traceSealedLap`, a sealed line right round the torus along one axis (a wall of bluffs at the back of a belt); `forEachTileInTeardrop` and `fillTeardrop`, a `Teardrop` the same way with each tile's place along and across its axis; `traceRay`, such a line traced along a heading until a stop (a lane from a plot to the shore); `downhillPath`, a radially monotone walk with correlated angular drift, bounded heading and tapered width (lava/root/drainage flows) |
| `Wedge` | `WedgeFrame`: the map as one equal wedge per colony round the centre, so a feature designed once in a wedge's frame is stamped into every wedge alike; given a `Stretch` it measures every cell in the round design frame, so the same design fills a rectangular map; `Blob`, a stretched, turned rough disc in that frame; `WedgeField`, periodic noise sampled in the wedge frame so a layer planted by it comes out the same in every wedge |
| `Sketch` | `TerrainSketch`, the undermap designed in memory; `layBeaches`, the order-independent beach pass; `raiseIslands`; `sprinkleSand`, decorative sand patches on inland grass following a caller's noise, kept a strip of grass away from every beach; `writeUndermap`; `pureTiles`, the tiles whose four corners all hold one terrain (the sketch as the game will draw it; also available directly from a finished map), and `tileCorners`, the reverse; `growWater`, one body of water grown to an exact tile count by a caller's key (Stone highlands' ponds, Amphitheatre's bays); `keepRoadInland` and `roadTiles`, a sand road kept off every beach and the tiles its vertices spoil |
| `LatticeNoise` | `PeriodicNoise`, value noise that tiles the torus exactly and samples anywhere, with `periodicNoise`/`fractalNoise` (integer fields per tile, octaves) and `torusNoise` (four octaves in [-1, 1]) sampled from it, plus `percentile` and `noisyShare` (the given share of a region where a noise field is highest, as a mask: cover in patches rather than speckle) |
| `RecursiveGeometry` | Integer recursive halves/thirds, retained region hierarchy and stop reasons; uniformly spaced rectangular Hilbert paths |
| `HierarchicalCrossings` | Required connectivity, marginal travel-benefit shortcuts, seeded ties and explicit budget shortfalls |
| `HeightMap`, `Noise` | Perlin noise faded across the wrap, and the stamped height fields the height-field generators shape |
| `Planting` | The deposits a generator places by hand: `clearGround`, `growPatch`, `seedNear`, `plantPatchNear` (bounded seed search plus a counted compact patch), `plantKit` (a home's wheat, wood and stone from three seeds, or no stone for a home already walled in it), `KitFrame` (a home's origin and facing, so one kit design lands the same way round every home), `swarmSurroundings`, `clearAroundSwarms`, `seedAlgae` (any water or a shallows band; optionally only its best-growing share, shared out equally between the colonies' wedges), `algaeGrowthChance` (each water tile's chance of passing the engine's algae growth test, which needs water within 15 tiles and solid sand within 30 at a reflected offset), `stockIslands`, `plantFields` (the preferred tiles dealt into wheat and wood patches by an unrelated split key), `scatterClumps` (clumps dropped on random eligible tiles of a region), `plantRound` and `plantOrchard` (a clump, or groves of the three fruits, at the same point round a circle at every colony's angle), `plantCover` (one deposit over every allowed tile of a region: a forest, a wheat plain, undergrowth); The deposits a generator places by hand: `clearGround`, `growPatch`, `growPatchesNear` (spend a resource budget across disconnected eligible pockets, returning tile and patch counts), `capResourceStock` (cap existing deposits without refilling or RNG draws; caller still controls growth), `seedNear`, `plantKit` (a home's wheat, wood and stone from three seeds, or no stone for a home already walled in it), `KitFrame` (a home's origin and facing, so one kit design lands the same way round every home), `swarmSurroundings`, `clearAroundSwarms`, `seedAlgae` (any water or a shallows band; optionally only its best-growing share, shared out equally between the colonies' wedges), `algaeGrowthChance` (each water tile's chance of passing the engine's algae growth test, which needs water within 15 tiles and solid sand within 30 at a reflected offset), `stockIslands`, `plantFields` (the preferred tiles dealt into wheat and wood patches by an unrelated split key), `scatterClumps` (clumps dropped on random eligible tiles of a region), `plantRound` and `plantOrchard` (a clump, or groves of the three fruits, at the same point round a circle at every colony's angle), `plantCover` (one deposit over every allowed tile of a region: a forest, a wheat plain, undergrowth); `clearDeposits`, the deposits on ground a design promised open (a ford's landings) cleared, a designed wall kept; The deposits a generator places by hand: `clearGround`, `growPatch`, `seedNear`, `plantKit` (a home's wheat, wood and stone from three seeds, or no stone for a home already walled in it), `KitFrame` (a home's origin and facing, so one kit design lands the same way round every home), `swarmSurroundings`, `clearAroundSwarms`, `seedAlgae` (any water or a shallows band; optionally only its best-growing share, shared out equally between the colonies' wedges), `algaeGrowthChance` (each water tile's chance of passing the engine's algae growth test, which needs water within 15 tiles and solid sand within 30 at a reflected offset), `stockIslands`, `plantFields` (the preferred tiles dealt into wheat and wood patches by an unrelated split key), `scatterClumps` (clumps dropped on random eligible tiles of a region), `plantRound` and `plantOrchard` (a clump, or groves of the three fruits, at the same point round a circle at every colony's angle), `plantCover` (one deposit over every allowed tile of a region: a forest, a wheat plain, undergrowth), `plantCoverShare` (cover over the top share of chosen tiles by a noise level, in patches; Canals' woodlots, Plantations' crop bands); The deposits a generator places by hand: `clearGround`, `growPatch`, `seedNear`, `seedForPatchCapacity` (on-demand local eligible-frontage ranking for a retry), `plantKit` (a home's wheat, wood and stone from three seeds, or no stone for a home already walled in it), `KitFrame` (a home's origin and facing, so one kit design lands the same way round every home), `swarmSurroundings`, `clearAroundSwarms`, `seedAlgae` (any water or a shallows band; optionally only its best-growing share, shared out equally between the colonies' wedges), `algaeGrowthChance` (each water tile's chance of passing the engine's algae growth test, which needs water within 15 tiles and solid sand within 30 at a reflected offset), `stockIslands`, `plantFields` (the preferred tiles dealt into wheat and wood patches by an unrelated split key), `scatterClumps` (clumps dropped on random eligible tiles of a region), `plantRound` and `plantOrchard` (a clump, or groves of the three fruits, at the same point round a circle at every colony's angle), `plantCover` (one deposit over every allowed tile of a region: a forest, a wheat plain, undergrowth) |
| `Resources` | The ambient layer and its fairness guards: `scaledCount`/`scaledShare`, `placeResourceClump` (optional per-tile placement mask), `setScaledResource`, `scatterResources`, `guaranteeStartingResources` (optional constrained crop rescue), `openCrampedStarts` |
| `Roads` | `cheapestRoute` and `openRoad`: the walk that crosses the fewest deposits, with only those cleared; `cheapestWalk`, the same search by any step cost (Symmetric arena's causeway routes); `openColonyRoutes`, the backstop that opens the cheapest way from colony 0's doorstep to any colony it cannot walk to under a `StepCosts` model, clearing deposits and fording water with sand (Everglades), optionally a lane of any radius wide (`clearRoute`) and never through a protected mask (Continents' passes through scree); `connectColonies`, the cheapest walk from colony 0 to every colony it cannot reach opened through deposits alone, never water or a designed wall (Watershed, Braided river); `reserveSandRoute`, a cardinal or eight-neighbour sand approach of explicit tile width with optional positive routing costs, reserved before furnishing without changing water corners or protected terrain |
| `Settlements` | `placeSettlement`: whole-footprint home mask, nearest legal anchor, exact worker count, per-colony diagnostics; `placeTower`, a completed defence tower at the allowed footprint nearest a designed point, with optional full-width approach coverage and initial stone reserves; `placeStartingBuilding` uses the same footprint search and selectively supplies a completed building (a starter inn can receive wheat without giving away fruit); `placeBuilding`, a completed building of any type and level at the allowed footprint nearest a designed point (a granted swimming pool or inn; Plantations); `countBuildings`, a validator's count of a colony's completed buildings of a type |
| `Walls` | Walls a design builds and the checks that prove they hold: `seaVertices` and `seaMargin` (the land a swimmer can stand on), `sealCoasts` (stone on every grass tile touching that margin, so a coast is sealed; City states, Carousel, Switchbacks), `islandSeaMargin` and `sealedIslandStone` (the margin and the stone of walled land in the sea with ponds and sand roads, once for Carousel and Switchbacks), `checkGatePartition` (seal explicit tile plugs, detect diagonal or wrapped boundary leaks, and prove each connected plug touches exactly its two labelled regions); `labelBorders` (a wall between labelled regions no unit can cross even diagonally, one to three tiles thick, or with doors a design leaves open; Stone highlands' ridges, Amphitheatre's borders, Carousel's walls), `designedStone` (a designed band's stone and any gaps the beach pass left), `reachesWithShut`, `pieceLeak` and `seaEntry` (with the doors shut, does any part reach another, or the sea reach inside), `towerReach` (how close a tower on some ground comes to shooting at a target: towers scan square rings with no line of sight), `walkSpread` (every colony's walk to its own target, and how uneven they are) and `firstRegionLeak` (reachable ground where two labelled regions meet that a design says must not; Maze's walls); `colonyLeak`, the first two colonies that can walk to each other with a set of tiles shut (a map's causeways or gates), crops and buildings counted as the ground they will leave |
| `Tessellation` | Polygon tilings of the torus with no terrain attached: `squareTessellation` and `hexTessellation` (cells, the corners they share and edges as identities, exact across the wrap even two cells wide), `edgeEnds`, `centreAcross`, `outline`, `transform` (the lattice's translations and mirrors), `labelTiles` (every tile's cell), `cellCrossing` (a path through a particular shared edge, outside reserved centre discs, preserving distinct toroidal parallel crossings), `shortestEdgeSteps` and `centreClearance`; `warpLimit` and `warpCorners`, corners moved at random into irregular polygons that still tile, never closing the gap between walls that share no corner below a minimum (Maze); `rasterizeBoundaries` (sealed selected edges with toroidal thickness) and `relaxWarpOutside` (bounded deterministic contraction away from an arbitrary protected tile mask) |
| `GraphMaze` | A maze carved through any `CellGraph` (a tessellation's with `cellGraph(tiling)`, or scattered sites' with `cellGraph(torus, sites, siteNeighbours)`): `pocketsFit` and `spreadPockets` (cul-de-sac cells spread as far apart as still leaves a maze), `carveSpanningTree` (recursive backtracker), `carveNearTree` (Kruskal over jittered distances: the network a traveller would build, each cell to the next one over), `openPocketDoors`, `openLoops`, `deadEnds` (Maze, Drumlin field); `closedEdges` (candidate crossings excluding protected endpoints) and `edgeDetours` (existing route lengths for shortcut scoring, caching floods by endpoint); `farthestCells` (farthest-point spreading over eligible cells with no maze constraint) and `claimNeighbourCells`, neighbouring cells dealt to seed cells round by round with every seed ending on the same count (Plantations' granted islands) |
| `HeightMap`, `Noise` | Perlin noise faded across the wrap, and the stamped height fields the height-field generators shape |
| `ClearingLandscape` | Sand-contained round homes on a dealt lattice, optional home pools and distant lakes; shared by Old Growth and Locust without changing Old Growth's seed streams or layout arithmetic for passing layouts. Locust may opt into a vacancy lattice only after the ordinary room check fails |
| `ScoredSettlements` | `chooseScoredSettlements`: compare a bounded list of complete settlement proposals in fresh worlds, score actual workers/resources/room with `StartQuality`, enforce caller viability checks, and restore all trial RNG effects; the returned winning sites are materialized by the same builder |
| `Territories` | Ground shared out where wedges cannot be fair (a square map's corners, the wrap, odd colony counts): `growTerritories`, equal-area regions grown together from seed tiles, the smallest always taking the next cheapest tile, connected and deterministic, or shared by value when each claimant's ground has a worth per tile; `balancedTerritories`, a power diagram from one site per claimant with the weights tuned until the areas are equal, so every border is a straight line (Amphitheatre); `smoothLabels`, a majority filter over any radius that trims spurs or, at a radius of 3 or 4, straightens borders into curves; `separateTerritories`, a gap opened between neighbouring territories; `fillToNearest`, the reverse: unclaimed ground near labelled regions given to the nearest, so water between pieces becomes land; `strandedGround`, land a flood cannot reach; `growFarLake`, a lake of an exact size at the roomy far end of a region; `growLakeBeside`, one on either flank of a site, kept wholly to its side of the line from the way in; `siteAtDepth`, the roomiest site a given number of steps from a region's way in |
| `Farmland` | `stampContainedPlot`, arbitrary grass-corner sets with sealed sand margins; `plantContainedPlot`, fertility-ranked deposits with explicit count and renewable constraints; `containedPlotsMismatch`, final eight-neighbour grass containment validation (see [Savannah](SAVANNAH.md)). Farms laid like real ones, in long rows of crops with water between: `bestFarmRows`, the crop and water widths that yield most for rows at an angle (10 and 8 tiles along an axis, 12 and 9 on the diagonal, a rough line through the exact regrowth sums `tools/farm_row_fit.py` computes); `farmYield`, the yield per tile at that angle from the same fit (0.149 along an axis, 0.113 on the diagonal); `layFarm`, rows over a region with a rim of land kept round them and, optionally, a 10x4 building plot of grass ringed with sand at its most inland point and sand bridges clean across the farm every so many tiles along the rows, water and crop rows alike (2026-09-14; before, only the water rows), each half switched by `FarmBridges` and offered to players by every farm-row map as `water-crossings` and `crop-crossings` (`waterCrossingsControl`, `cropCrossingsControl`, both on), so workers cross the whole field on one road (a plot's grass always wins over a bridge), and a ring of sand closing the crop rows so wheat and wood never spread out of the farm; `plantFarm`, wheat along the water on every crop row and a small woodlot along one; `clearFarmPlots`; `farmReachable`, the share of a farm's crop land a colony can walk to; `growFarmFields`, farm fields grown into open water straight out of their own homes by equal yield for their row angles, held off all other land and each other, opened so no strip too narrow for its beaches survives (and only ground whose eroded core joins the home's core stays, since two cores that do not touch can overlap once grown back through a waist the coast walls then close), and joined to their homes by a broad neck. Walls round a farm are the map's business, not the farm's; `layContourFarm`, complete circular bands around central clearings, with inner/outer sand caps and radial crossings (`ContourFarmStyle`) |
| `Towers` | Tower sites chosen for the ground they cover over the walls, other colonies' (offence) or their own (defence), each weighted: `startingTowerRequest` (a request from a map's level and count controls), `chooseTowerSites` (best first, a colony at a time in turn, optionally every site directly against a wall, no stocked tower in range of another colony's tower or swarm, plus open 2x2 pads for players to build more), `settleStartingTowers` (the sequence every arena map runs: `dropBlockingSites` so no site closes a colony's walk to a goal, `evenTowerPlan` so every colony has as many sites as the fewest got, then `raiseTowers`), `towerFootprints` and `roomyGround` (no tower on a strip it could close); a tower in range of another colony's tower starts empty |
| `Homes` | Homes built alike: `stampRoundHome`, `homeSwarmSite` and `plantHomeKit` (a round home facing out from the middle, its swarm towards its door, optional ponds kept clear of its edge, and a wheat and wood kit near the swarm with no stone, since these homes are walled in stone; `homeHasRoom` is the size floor; Carousel, Switchbacks; the axis is any heading, out from the middle on a ring), `regionHome` (a home in ground of any shape: the swarm the same walk in from the door as every other colony's, on the roomiest such tile, facing away from the door), and `furnishGround` (farmland in patches on a ground's fertile tiles, outcrops and groves; City states, Carousel, Switchbacks, Amphitheatre); Homes built alike: `TeardropHome` with `stampTeardropHome`, `teardropHomeSwarm` and `teardropHomeKit` (a home on a long hill: town head, sand collar, farm tail; Drumlin field) |
| `Bases` | Premade bases (The Glacis, Allotments, Caravanserai): a colony that starts with a whole base standing, or staked out as sites, and far more colonists than the lobby's "Starting workers" control allows. `BasePlan` (pieces as building type, level, frame offset, finished or level-0 site, stock share; depots such as a quarry or wood stacks), `standardBasePlan` (one layout at three tiers, Hamlet/Town/City, in two kinds, Finished/Sites, with optional stocked towers and a quarry; tiers drop pieces without moving the rest), `baseTier` and `baseGarrison` (a `colonists` value to a tier, and to workers plus, with the garrison on, three eighths as many level-1 warriors and an eighth as many explorers), `BaseSite` and `baseTile`/`baseFootprint` (a site with a quarter-turn facing, Canals' block frame, so rectangles stay rectangles), `basePlanFits` (every footprint on pure grass, disjoint, a one-tile walking ring on open ground round each, depots clear), `baseFootprints`/`baseSurroundings` (masks later layers keep clear of), `raiseBase`/`raiseBases` (checkRoomForBuilding, addBuilding, stock before the lists, `garrison` ring by ring round the swarm, one `Team::createLists` per colony, start position and boot tile from the swarm), `plantBaseDepots` and `validateBase` (every planned building of the right type, level and finished-ness on its footprint, and the colony's worker count). A landscape using it sets `GeneratorDefinition::startingWorkers` |
| `Compounds` | Square walled compounds round a base site: `stampCompound` (interior labelled, a one-tile stone wall at a Chebyshev radius, gates of odd width cut on the facing side, then the back and the flanks), `compoundsApart` and `wallStanding` (a validator's proof that every designed wall tile holds stone and every gate is open) |
| `Lots` | Lanes and lots: `layLanes` (one-tile lanes about a pitch apart that wrap the torus exactly, spread by whole tiles like Maze's cells), `LaneGrid` (each tile's block by column and row, each block's span between its lanes), `laneTiles`, `stampLotPad` (a sand-ringed grass pad of the first size that fits, centred in its block, held back for a ditch's beach; Canals' homestead pad as a primitive) |
| `Routes` | Routes between sites on the torus, the short way round: `midpointAcross`, `siteDistance`, `nearestPairs` (each site's nearest others as pairs, each once), `waypointsAlong` (stepping stones along the straight way between two points, with gaps kept at both ends), `headingAcross` and `quarterTurn` (a heading as a BaseSite facing) |
| `Orbits` | Fairness by symmetry groups rather than wedges: `Symmetry` (signed permutation matrices on doubled centred coordinates plus a whole-tile translation, mapping tiles and corners exactly), `pointSymmetry` (the half turn, quarter turns, mirrors and all eight square symmetries; Symmetric arena), `translationSymmetry` (the roomiest lattice of 2, 4, 8... colonies on the torus, square, staggered or sheared, with no centre and served as well on a rectangular map), `jitterSites` (bounded integer perturbations preserving caller-specified minimum spacing), `latticeSites` (colonies on that lattice, or evenly staggered rows when the count has no exact group), `stampOrbits`, `orbitSum`, `orbitNoise` and `topShare` (features and fields every symmetry leaves unchanged), `equaliseDeposits` (the gameplay RNG's amounts made equal across each orbit) and `orbitMismatch` (the finished world checked exactly symmetric, colonies permuted one to one); Fairness by symmetry groups rather than wedges: `Symmetry` (signed permutation matrices on doubled centred coordinates plus a whole-tile translation, mapping tiles and corners exactly), `pointSymmetry` (the half turn, quarter turns, mirrors and all eight square symmetries; Symmetric arena), `translationSymmetry` (the roomiest lattice of 2, 4, 8... colonies on the torus, square, staggered or sheared, with no centre and served as well on a rectangular map), `latticeSites` (colonies on that lattice, or evenly staggered rows when the count has no exact group), opt-in `roomyLatticeSites` (leave surplus composite-grid sites empty when that strictly improves spacing), `stampOrbits`, `orbitSum`, `orbitNoise` and `topShare` (features and fields every symmetry leaves unchanged), `equaliseDeposits` (the gameplay RNG's amounts made equal across each orbit) and `orbitMismatch` (the finished world checked exactly symmetric, colonies permuted one to one) |
| `Morphology` | Mask arithmetic on the torus in integers: `dilate`, `erode`, `openMask`, `closeMask` (squares), `distanceSquaredTo` (exact Euclidean) and `dilateRound`, `clearance` (steps to the nearest tile outside a mask), `dropSmallRegions` (specks, or pockets on the inverted mask), `widestWalkClearance` and `narrowestPassage` (the width of a walk's narrowest point, a validator's check that a tunnel or street still takes a column of units), `slivers` (Stone highlands' pockets, Farmland's field opening, `roomyGround`, `separateTerritories`) and `bridgeDiagonals` (a line of tiles joined wherever it touched only at a corner, so a river or moat core holds against eight-connected movement); Mask arithmetic on the torus in integers: `dilate`, `erode`, `openMask`, `closeMask` (squares), `distanceSquaredTo` (exact Euclidean) and `dilateRound`, `clearance` (steps to the nearest tile outside a mask), `dropSmallRegions` (specks, or pockets on the inverted mask), `widestWalkClearance` and `narrowestPassage` (the width of a walk's narrowest point, a validator's check that a tunnel or street still takes a column of units), `roomiestTile` (the tile of a region with the most clearance, nearest a point on a tie) and `slivers` (Stone highlands' pockets, Farmland's field opening, `roomyGround`, `separateTerritories`) |
| `Growth` | Where wheat and wood grow back, known on the sketch: `cropGrowthField` (the exact `Fertility::Field` of a `TerrainSketch`, not gated on deposits), `wateredShare`, `wetTiles` (a region promised dry checks 0), `dryZone` (where water would water a region: `kCropProbeReach` on each axis), `drainWithin`, `waterSteps` (every tile's steps to the nearest pure water) and `digPond` (a pond of an exact size grown beside a site that has no water, kept clear of its swarm and on its own ground; Continents' oases); finished-map `cropSeedsIn` checks protected masks after resource repairs, and `cropSpreadEnvelope` conservatively floods wheat/wood through toroidal eight-connected grass to test long-term containment without simulating growth speed |
| `Contact` | Distances by what a step costs: `StepCosts` (open, clearable, eternal, water, building; `walking`, `chopping`, `swimming`), `stepCost`, `costsFrom`, `contactMatrix` (every colony's cost to every other), `costsToTarget`, `costSpread`, `unevenCosts` (a validator's message) and `equalCostSites` (one site per colony at the same cost from home, on its own side: groves an equal chop into a forest) |
| `Points` | Irregular sites and cells: `spreadPoints` (dart throwing with a minimum spacing), `nearestSiteLabels` (warped Voronoi cells in sixteenths of a tile), `relaxPoints` (Lloyd relaxation on the torus) and `siteNeighbours` (which cells touch); Stone highlands' basins; Stone highlands' basins. Under a `Grain` (a whole-step direction and a stretch along it, with an integer `distance2`): `spreadPoints` with fixed sites kept clear, `nearestSiteDistances`, `nearestSiteLabels` (cells elongated with the grain) and `packLandforms` (a radius per site so every pair keeps a gap, fixed radii kept, small sites dropped); Stone highlands' basins. On uneven ground: `farthestSites` (sites spread by walking distance over a passability mask, the best of several first sites kept, so a bay between two sites counts as the walk round it) and `recentreSites` (each site walked to the candidate nearest the middle of its own territory); `nearestTwoSites`, an exact unwarped owner/runner-up query with squared toroidal distances, stable index ties and missing-site sentinels; `spreadRankedSites`, weighted maximin selection with a hard toroidal spacing floor and explicit partial failure |
| `Patterns` | Structure with a grain: `turingPattern` (labyrinths and spots grown from noise by activation and inhibition, about a wavelength across, optionally stretched), `stripePhase`, `stripeSpacing`, `stripeHeading`, `stripeNormal` and `stripeDistance` (parallel stripes that wrap both seams exactly at any whole-number slant, optionally warped), `upwindSteps` (the shadow a mask casts downwind) and `traceStreamline` (a curve following a field of headings); `runsAndGaps`, a broken line of runs and gaps (a moraine's hummocks, cover with doors in it) |
| `Channels` | Water sized for its purpose: the beach arithmetic once (`kChannelSpoiledTiles`, `bankToBank`: a channel w corners wide spoils w + 3 tiles and puts banks w + 4 apart), `widestChannelTowersCross` and `narrowestChannelTowersMiss` by tower level, `beachTiles` (the rim towers stand against to cover a canal, on a sketch or a map), `bridgeAcross` (a sand bridge across water), `straitsBetweenCells` (a strait of an exact corner width along every border between labelled cells, so cells become islands; Plantations) and `crossingsPerLabel` (every colony's number of ways across); channels drawn as centre lines with a radius per point: `SandFord` with `stampFord`, `fordAlong`, `fordFault` and `fordLandingWalkable` (a ford laid across a channel and checked on the rasterized water: it interrupts open water, is dry across and lands on walkable ground), `channelCoreFault` (a channel keeps a 4-connected core of water except at its fords, so nothing steps over it) and `channelCrossings` (the stretches two labelled regions face each other across, the edges of a crossing graph; Watershed's fords, Braided river's riffles) |
| `Room` | Building room as a design decision: `buildableTiles`, `buildAnchors` (every 4x4 footprint's top-left, across the wrap), `buildSites` (footprints in a region) and `growUntilSites` (a chamber grown until it holds exactly what it promised) |
| `Biomes` | Kinds of land as data: `BiomeKit` (ponds, stone ring, farmland, wood share, outcrops, groves, cover of wood or stone, a dry reserve of finite crops where nothing regrows, orchard island) with `fertilePlain`, `stoneFortress`, `orchardIsland` and `forest`, and the kinds of real land `farmland`, `woodland`, `savanna`, `barrens` and `highland` (stone scree at 40%, below the percolation threshold, so a range is slow to cross but not a wall); `biomeWorth` (an estimate from the start scorer's weights to share ground by, to be tuned with the fairness tournament); `scaledBiome` (a kit's ambient layers scaled to the map's resource amounts); `sketchBiome` (its ponds, island and ring) and `furnishBiome` (its deposits) |
| `WorldAtlas` | Real geography compiled in (see [the world atlas](WORLD_ATLAS.md)): `LandClass` (ocean, lake, plain, forest, steppe, desert, mountain, tundra, ice) and a river flag per cell, `kAtlasRegions` (the six continents, run-length encoded by `tools/world_atlas.py` from Natural Earth and the Köppen-Geiger climate map), `atlasRegion` and `decodeAtlas` |
| `Raster` | A picture laid onto the torus: `fitRaster` (a source raster fitted inside the map less a margin, centred, its aspect kept, turned a quarter turn when that fits a rectangle larger, all in integer fractions), `resampleRaster` with `resampleMajority` (each tile's most common class, sea on a tie) and `resampleAny` (a flag any covered cell carries: a river survives shrinking) |
| `Landmass` | Rasterized coasts made playable: `cleanLandmass` (slivers under three tiles wide, specks of sea and islets nothing fits on, by `CoastCleaning` thresholds), `largestRegion` (the mainland) and `inheritLabels` (filled ground takes the classes round it) |
| `BalancedStarts` | `chooseBalancedStarts`: boot tiles whose walks to wheat and wood are as nearly equal as the finished map allows |
| `Pipeline` | The stages round the others: `dealStarts` (the design's start sites dealt to the colonies at random, so a team number never gets the same ground map after map), `designFailure` (the registry's request check for a designed generator: the design's own failure), `settleColonies` and `settleRoundColonies` (round homes on their own grass, `homeGrassMask`), `homePondMissing` (a validator's check that every home kept its pond), `secureStartingCrops` (clear round the swarms, guarantee the crops, clear again), `reopenCrampedStarts` (at non-default amounts) and `openStartsBuriedByResources` (at any amount), `designMismatch`, `walkFromFirstColony`, `cropsBesideReach` (which crops a flood from a colony stands beside) and `coloniesApart` (the island map's opposite promise: no colony can walk to another) for validators, `startingAccessFailure` (read-only worker access to caller-selected supplies and nearby 4×4 building origins) and `startingFloorFailure` (the wheat, wood and building-room floor every legacy-core landscape validates), `ResourceAmounts` |
| `Terrain` | The height-field pipeline as stages: `heightFieldTiling`, `classifyHeightField`, `paintHeightFieldTerrain`, `paintHeightFieldResources`, `chooseHeightFieldStarts`, `plantHeightFieldGroves`, composed by `generateHeightField` |
| `StartQuality` | `scoreStarts`, the finished map's colony measurements; `FairnessModel.h` scores them and the service ranks candidates by the resulting fairness |
| `GenerationContext` | Named `std::mt19937` streams, `bounded` draws and `shuffle` |
| `legacy/Regions`, `legacy/Distances`, `legacy/StartingPositions` | The older area-grid toolkit: point dispersion (`splitUpPoints`, `splitUpArea`, `divideUpArea`), the legacy distance encoding, `divideUpPlayerLands` and the boot-tile placers `placeStarts`/`placeArchipelagoStarts`. Concrete islands, Isles, Contested commons and the height-field generators still build on it; nothing new should |

Controls and validation live in `core/`: `GeneratorControls` declares discrete, stepped,
power-of-two and named-choice option domains once for the UI and the catalog; `GeneratorDefinition::validateRequest`
is a pure check run before generation, and `validateWorld` runs after the structural checks
against the finished terrain. How the lobby ranks a generator's candidate seeds is not a
generator's business any more: every seed is scored by the same fitted fairness model
(`FairnessModel.h`), and a map that is cramped or crowded on purpose states that through its
own `validateWorld` rather than by retuning the ranking.

## The designed generator

Twenty-two generators (Maze, Fjord continent, Watershed, Stone highlands, Symmetric arena, Ring world,
City states, Tidal flats, Everglades, Spider web, Coral, Carousel, Amphitheatre, Switchbacks, and the
landscape generators Fingerprint, Rain shadow, Old growth, Canals, Polder, Old town, Anthill and Braided Delta) follow one
landscape generators Fingerprint, Rain shadow, Old growth, Canals, Polder, Old town, Anthill and
[Breachable highlands](BREACHABLE_HIGHLANDS.md)) follow one
landscape generators Fingerprint, Rain shadow, Old growth, Canals, Polder, Old town, Anthill and Drumlin
field) follow one
landscape generators Fingerprint, Rain shadow, Old growth, Locust, Canals, Polder, Old town and Anthill) follow one
landscape generators Fingerprint, Rain shadow, Old growth, Canals, Polder, Old town, Anthill and Plantations) follow one
shape, and the newest of them are little more than a sequence of shared stages:
landscape generators Fingerprint, Rain shadow, Old growth, Canals, Polder, Old town, Anthill and Braided
river) follow one shape, and the newest of them are little more than a sequence of shared stages:

1. `design(request, context)` computes the whole layout from the request and the context's
   named streams without touching the map, and returns it with a `failure` string when the
   request leaves no room.
2. `generate` stamps the layout into a `TerrainSketch`, calls `layBeaches` and `writeUndermap`,
   then `settleColonies` with a home mask and an anchor per colony. The design has already dealt its
   start sites to the colonies at random (`dealStarts`), so which team gets which home is a draw:
   without it, farthest-point spreading and lattices hand team 0 the same ground on every map.
3. The kits go down with `plantKit` (three seeds, each grown from the nearest eligible
   tile), then the ambient layers
   (`scatterResources`, or the generator's own ranking fed to `plantFields` and `scatterClumps`,
   `seedAlgae`, `stockIslands`), then `secureStartingCrops`: `clearAroundSwarms`,
   `guaranteeStartingResources` and `clearAroundSwarms` again.
4. `openRoad` (or the generator's own cheapest-walk variant) keeps every walk the map promises
   open, clearing only the deposits in the way; `reopenCrampedStarts` runs at non-default amounts
   (`openStartsBuriedByResources` for a landscape that wants that relief at the defaults too).
5. `validateWorld` calls `design` again on a fresh context, checks it with `designMismatch`,
   walks every colony from colony 0 with `walkFromFirstColony`, and then checks whatever the
   design promised: ponds present, walls standing, fords open, symmetry exact.

A generator with a different order calls the same stages in its own order; a generator with a
different need (Ring world's belt-wide road, Symmetric arena's orbit stamping) writes that one
piece itself and says why in its header comment.

## The generator catalog

`GeneratorRegistry::builtins()` orders these for the product-facing catalog; the numeric legacy
id is a separate, stable compatibility identifier that never changes once assigned and is never
reused after a generator is retired. The order was shuffled once, on 2026-09-14, so the list
carries no bias towards the landscapes that happened to be written first; the lobby opens on the
first playable entry and the editor on the first entry (both Fingerprint), and a saved lobby
restores whatever landscape it had.

| id | legacy id | Display name | Family |
|---|---|---|---|
| `fingerprint` | 26 | Fingerprint | Its own — see below |
| `old-town` | 31 | Old town | Its own — see below |
| `symmetric-arena` | 15 | Symmetric arena | Its own — see below |
| `swamp` | 1 | Swamp | Height-field noise; water interleaved with land |
| `tidal-flats` | 18 | Tidal flats | Its own — see below |
| `river` | 2 | River | Height-field noise; a winding river through connected land |
| `isles` | 6 | Isles | Point dispersion; islands linked by land bridges |
| `ring-world` | 16 | Ring world | Its own — see below |
| `amphitheatre` | 23 | Amphitheatre | Its own — see below |
| `shattered-coast` | 7 | Old random | Iterative water/sand/grass balancer, own resource search |
| `crater-lakes` | 4 | Crater lakes | Height-field noise; round lakes in otherwise connected land |
| `fjord-continent` | 12 | Fjord continent | Its own — see below |
| `spider-web` | 20 | Spider web | Its own — see below |
| `concrete-islands` | 5 | Concrete islands | Point dispersion; islands linked by channels |
| `watershed` | 13 | Watershed | Its own — see below |
| `maze` | 11 | Maze | Its own — see below |
| `islands` | 3 | Islands | Height-field noise; organic islands with no inter-island passage |
| `stone-highlands` | 14 | Stone highlands | Its own — see below |
| `switchbacks` | 24 | Switchbacks | Its own — see below |
| `city-states` | 17 | City states | Its own — see below |
| `canals` | 29 | Canals | Its own — see below |
| `braided-river` | 42 | Braided river | Its own — see below |
| `hills` | 46 | Hills | [Contour farms and summit towns](HILLS.md) (named Rice terraces until 2026-09-16) |
| `rice-terraces` | 52 | Rice terraces | [Terraced hillsides spiralling round the torus](RICE_TERRACES.md) |
| `plantations` | 48 | Plantations | Its own — see below |
| `sierpinski-gardens` | 49 | Sierpiński Gardens | [Recursive lakes, home districts and orchard causeways](FRACTAL_MAPS.md) |
| `hilbert-river` | 50 | Hilbert River | [Folded river, contained bank farms and hierarchical shortcuts](FRACTAL_MAPS.md) |
| `lava-shield` | 51 | Lava shield | [Volcanic island: crater rim, lava tongues, scored coastal towns](LAVA_SHIELD.md) |
| `rugged-archipelago` | 8 | Old islands | Island growth + beach passes, own resource search |
| `contested-commons` | 9 | Contested commons | Point dispersion (`shared/legacy/Regions`) |
| `rain-shadow` | 27 | Rain shadow | Its own — see below |
| `everglades` | 19 | Everglades | Its own — see below |
| `polder` | 30 | Polder | Its own — see below |
| `carousel` | 22 | Carousel | Its own — see below |
| `old-growth` | 28 | Old growth | Its own — see below |
| `locust` | 47 | Locust | Dry, finite wheat fields and shoreline wood — see [design and verification](LOCUST.md) (named Vultures until 2026-09-16) |
| `anthill` | 32 | Anthill | Its own — see below |
| `glacis` | 39 | The Glacis | Its own — see below; a premade base (`shared/Bases`) |
| `savannah` | 45 | Savannah | [Open plains, contained home crops and neutral watering holes](SAVANNAH.md) |
| `coral` | 21 | Coral | Its own — see below |
| `emoji` | 34 | Emoji | [Design and play contract](emoji/DESIGN.md) |
| `forts` | 35 | Forts | [Walled forts in river country](FORTS.md) |
| `braided-delta` | 36 | Braided Delta | [Design and heuristics](BRAIDED_DELTA.md) |
| `breachable-highlands` | 37 | Breachable highlands | [Stone valleys with clearable wooded saddles](BREACHABLE_HIGHLANDS.md) |
| `hedgerow-country` | 38 | Hedgerow Country | [Warped fields, gateways and cuttable hedges](HEDGEROW_COUNTRY.md) |
| `allotments` | 40 | Allotments | Its own — see below; a premade base of construction sites |
| `caravanserai` | 41 | Caravanserai | Its own — see below; a premade base |
| `drumlin-field` | 43 | Drumlin field | Its own — see below |
| `continents` | 44 | Continents | Its own — see below |
| `uniform` | 0 | uniform terrain | Editor-only; one terrain type, unstructured |

Retired ids, never reused: 25 (Marches, a lattice of homelands with wooded border bands, dropped
2026-09-13 after play: "not working conceptually") and 33 (Patchwork, a different biome kit per colony,
dropped the same day: "the concept is failing"). `shared/Biomes` stays in the toolkit for any map that
wants a kind of land as data, and Continents builds its kinds of real land on it. The scaffold
(`tools/new_map_generator.py`) skips the retired ids when it picks the next one.

Swamp, River, Islands and Crater Lakes ("the height-field generators") shape their terrain and
paint their resource bands from the same Perlin noise field via `generateHeightField`'s stages
(`shared/Terrain`), and use `chooseBalancedStarts` (below) for colony placement. Contested
Commons, Concrete Islands and Isles build on the older Voronoi-style point-dispersion machinery in
`shared/legacy/Regions` and `shared/legacy/Distances`; Contested Commons additionally paints solid,
sharply-bordered resource zones
as a deliberate design choice rather than a noise scatter. Shattered Coast and Rugged
Archipelago predate the shared resource/placement machinery and still place resources relative
to each boot tile with their own compass-direction search, rather than through
`scatterResources`/`chooseBalancedStarts`.

None of these ten designs an economy — the field or the region graph decides where land is, and
the start search takes the best of what it left — so none of them can promise the shape of a
colony's ground. Since 2026-09-16 each one instead promises the floor underneath it: the relief in
`openStartsBuriedByResources` opens a colony that deposits walled in, and a `validateWorld` of
`startingFloorFailure` (`shared/Pipeline`) rerolls the seed when a colony still cannot reach wheat
or wood, or still has fewer than 16 4x4 building origins within 24 steps. Concrete Islands and
Isles pass `kReachableAnywhere` as the wood range in both, so an archipelago's long walk to the
nearest stand stays a question about those landscapes rather than a world the service throws away.

## Resource amounts and switches

Every playable generator has amount controls for the resources it places (wheat, wood and stone
everywhere; algae and fruit where it places them) and one to three on/off switches for its own
sub-behaviours. An amount is a percentage of the generator's default, 100, from 0 to 300 in steps
of 25 unless noted. It scales the numbers that already decided that amount, and at 100 every map is
exactly what it was. Fairness placements (starter kits, 1:1 guaranteed wheat and wood, the
reachability backstop) stay unscaled wherever a generator has them, so an amount of 0 empties the
ambient layer but still leaves every colony a start.

Audited 2026-09-14 for consistency: every landscape carries `wheat-amount`, `wood-amount`,
`stone-amount`, `algae-amount` and `fruit-amount` as percentages, with these exceptions, each
because the landscape has no such layer or names it differently: Anthill has no `stone-amount`
(its only stone is its walls); Old growth has no `wood-amount` (its `forest-density` is the wood
control); Isles, Old random and Old islands have no `fruit-amount` (they place no fruit); the
height-field generators (Swamp, River, Islands, Crater lakes) share `heightFieldResourceControls`
and keep their `fruit` control as a count of groves, 0 to 64. Maze's and Stone highlands' amounts
were counts of their own until that audit and are percentages since.

| Generator | What the amounts scale | Switches (default) |
|---|---|---|
| Swamp, River, Islands, Crater lakes | Each resource's band of the height field: algae the lowest sixth of the water, stone a third of the farmland share just above the beach, wheat and wood the rest as two separate bands, none past the top of the grass. Fruit still counts groves | Stone on hilltops (off): stone on the highest grass instead of by the shore. River also: Winding river (on) |
| Concrete islands | Each colony's wheat and wood fields (how far in from the coast they reach) and its six stone deposits; the channels' algae (how deep it grows); the neutral islands' wheat half and fruit count | Sandy beaches (on) |
| Isles | Each colony's fields and stone deposits, and its algae patch (5×5 at 100) | Land bridges (on); Sandy beaches (on) |
| Old random | The area of each colony's wheat, wood, stone and algae squares | Colony meadows (on): the cleared grass square round each colony |
| Old islands | The area of each island's deposits | Extra starting deposit (on): the fourth deposit, of whichever of wheat or wood came out smaller |
| Contested commons | How many of the commons' zones are wheat, wood or fruit, its quarry's size and the moat's algae; home islands' fields are unscaled | Moat bridges (on); Jagged coastlines (on) |
| Maze | The shore scatter's wheat, wood and stone (48, 24 and 16 tiles per 256 shore tiles at 100), every dead end's treasure (9 fruit tiles) and the channels' algae (24 per 400 water tiles); the homes' kits are unscaled | Sand roads (on); Treasure in dead ends (on): off, the same fruit is scattered along the passages' shores |
| Fjord continent | The ambient scatter, the core's stone and fruit clumps, the lake's and open sea's algae, and the banks' extra clumps; starter kits and bank guarantees are unscaled | Lake connects to fjords (off); Sandy lake shore (on); Fjord bank deposits (on) |
| Watershed | Separate wheat and wood farmland budgets, stone outcrops, confluence fruit groves, and algae at the mouths and in the shallows; starter kits are unscaled | River delta (on); Meandering rivers (on) |
| Stone highlands | The ponds' farmland (wheat and wood) and algae, how much of the ridgeline is two tiles thick (stone, 0 to 200), and the valleys' fruit groves (four per 128×128 at 100); kits are unscaled | Fruit in home valleys (off) |
| Symmetric arena | The farmland's wheat and wood, stone outcrops and algae, on top of Resource richness; fruit (0 to 100) keeps that share of the orchard's groves nearest the centre, never fewer than three | Moat (on); Stone in the orchard (on) |
| Ring world | The ambient scatter's wheat, wood, stone and fruit, and the shallows' algae; starter patches and island prizes are unscaled | Winding belt (on); Colonies on both coasts (on) |
| City states | Every home's ambient fields, outcrops and grove, everything on the commons (farmland, outcrops, groves, the orchard), the sea's algae and the islets' prizes; every home's kit and the walls' stone are unscaled | Stone walls (on): off, the causeways are plain roads and the homes' coasts are open |
| Tidal flats | Every island's ambient fields and outcrops and every island's prize; each home's kit is unscaled | Central island (on): off, the middle of the map is flats and there is no orchard |
| Everglades | The swamp's standing wood and wheat, its outcrops and groves, and the pools' algae; every home's kit is unscaled | None |
| Spider web | The threads' standing wheat and wood, the share of knots carrying stone, whether the dew drops and the hub carry fruit and stone, and the shallows' algae; every pad's kit is unscaled | Spiral (on): off, the capture threads are closed rings; Sand roads (on): off, the threads are grass from shore to shore |
| Coral | The branches' standing wheat and wood, the share of forks carrying stone, the tips' fruit groves and the shallows' algae; every pad's kit is unscaled | Sand roads (on): off, the branches are grass from shore to shore |
| Plantations | Every neutral plantation's crop cover (the wheat and wood shares of its band), the orchard and rock islets' counts, and the sea's algae; every home island's cover, every colony's granted pools and inns and the rock islet beside every colony are unscaled | Outpost inns (on): an inn on each granted island; Causeways (off): sand causeways join a colony's own islands, so no unit need swim to work them |
| Carousel | Every home's ambient fields, the farms' wheat and woodlots, the courts' and the plaza orchard's fruit, and the lagoon's algae; the walls' stone and the starting towers are unscaled (a home has no kit since 2026-09-14) | Sand roads (on): off, the corridors and spokes are grass from wall to wall |
| Amphitheatre | Every territory's ambient fields and grove, the arena's groves and terrace outcrops, and the bays' algae; every home's kit, the walls' stone and the starting towers are unscaled | None |
| Switchbacks | Every home's ambient fields and grove, the farms' wheat and woodlots, the plateau's orchard, and the algae; the mountains' stone and the starting towers are unscaled (a home has no kit since 2026-09-14) | Sand roads (on): off, the trails are grass from wall to wall |
| The Glacis | The wadi banks' wheat and wood, the plain's outcrops, the groves beside the fords and the wadis' algae; every compound's well-side kit, quarry, walls, stock and starting towers are unscaled | Garrison (on): off, a compound starts with its colonists only |
| Allotments | The field lots' wheat and wood, the woodlots, the quarry and grove lots and the ditches' algae; every city's sites, wood stacks, stock and home fields are unscaled | Garrison (on) |
| Caravanserai | The outposts' quarry, orchard and algae; every capital's fields, stock and towers, and the oases' and outposts' wheat, are unscaled | Garrison (on) |
| Continents | Every kind of land's kit (`scaledBiome`): its farmland's wheat and wood shares, its dry reserve, its outcrops, its groves, and its cover (wood over forest, stone over the ranges); the shallows' algae; the islets' prizes are drawn unscaled and every colony's kit is unscaled. `oases` (0-200) scales the pond dug for a colony that starts with no water in reach | Mountain ranges (on): off, a range is savanna; Great rivers (on); Islets (on) |
| Braided river | Every bar's farmland and the terraces' thin bank-strip farmland, the dry terraces' woodlots and outcrops, the deep bars' fruit groves and the channels' algae; every home's kit, every promised bar's wheat and wood, the bluffs and the moraine are unscaled | Moraine hummocks (on): off, the terrace edges are open bank. Dry patches (12): the share of the terraces' inland grass turned to sand patches, 0 for the plain sheet of grass |

`scaledCount` and `scaledShare` (`shared/Resources.h`) apply a percentage to a count or a share and
return it unchanged at 100. `setScaledResource` scales one `Map::setResource` square to a share of
its tiles, nearest the centre first, placed in `setResource`'s own order. Where an amount has to
add random draws (Contested commons' moat algae, Fjord's bank clumps), they come from streams of
their own, so the rest of the layout doesn't reshuffle.

A high amount can also bury a colony: resources block ground units and buildings alike, so a
widened farmland band or ambient scatter can wall a swarm into a pocket with nowhere to put a
building. `guaranteeStartingResources` does not cover that case — a colony buried in wheat has
wheat at its feet, so it counts as served — so `openCrampedStarts` (`shared/Resources.h`) clears
the resource tiles nearest such a colony, one ring at a time outwards, until it can walk to 16
tiles where a 4x4 building fits within 24 steps, and the caller then re-runs the guarantee in case
the clearing took the nearest crop too. A colony that already has the room is untouched.
`reopenCrampedStarts` runs that pair at any non-default amount and never at the defaults;
`openStartsBuriedByResources` runs it whatever the amounts are, which is what the landscapes on
the legacy core call, since nothing in them budgets a colony's room and the defaults bury one too
(a 512-tile fjord continent with four colonies sealed two of them onto nine tiles and four until
2026-09-16). Concrete islands and Isles need it differently:
their colonies' fields can cover every building site the start search looks at, so at a non-default
amount that search widens its window out from the default field rather than failing.

`placeSettlement` guards the same class of problem for the colonies it places: it measures the
workers' room (free tiles inside the home mask touching the swarm) before the swarm goes down, and
if the nearest-tie site the random pick landed on is short of room it takes the nearest site that
has it instead of failing the map. The pick itself is unchanged wherever the room was already
there, so this only rescues maps that used to fail outright.

## Resource placement

`shared/Resources.cpp` gives generators a small set of composable primitives rather than one
fixed resource pass:

- `placeResourceClump` / `placeResourceClumpInArea` grow a resource outward from a point to a
  requested tile count — the primitive every guaranteed placement (starter kits, bank deposits)
  is built from. Their optional row-major placement mask clips **every** clump tile, including
  wrapped edges; filtering only the centre can still spill a radius-two crop patch into a town.
  Null retains every existing caller's placement and random-draw order.
- `computeComponents` flood-fills the map into connected components of land (or of water). A
  generator whose landmasses are separate (an island generator) needs this so that a
  map-wide "take the best tiles first" pass can't spend an entire resource band on whichever one
  or two islands score highest, starving the rest — every subsequent per-component pass below
  runs independently within each component, sized to that component's own share of the eligible
  candidate pool. On a single connected map this is one component covering everything.
- `scatterFarmland` answers two independent questions separately: *where* farmland goes is a
  `Fertility::Field` threshold (see below) over each land component's own candidate pool, widened
  a little beyond the raw requested tile count but capped at two-thirds of that component's pool
  so every landmass keeps real slack to route around whatever gets placed; *which* of corn or
  wood a given spot becomes is a second, independent noise field, sorted and sliced into a corn
  share and a wood share so the two alternate along the region instead of forming two concentric
  rings sorted by fertility.
- `scatterBand` is the same per-component noise-threshold technique for stone and algae, which
  have no growth rule to prefer and so use a plain noise field rather than fertility. Stone is
  shared out between landmasses and algae between water bodies (a lake apart from the sea), since
  algae only places on water.
- `scatterResources(game, context, densities)` is the shared ambient layer — ordinary, unclaimed
  deposits filling the interior between a generator's deliberate placements — used by Fjord and
  Ring world.
  It runs after every colony's swarm, workers and guaranteed clumps already exist:
  `isResourceAllowed` refuses any tile already holding a building or unit, so scattering earlier
  can claim a tile a swarm footprint needed; scattering last only ever loses a clump's own tile
  to whatever was already there, the same low-stakes trade every generator's own resource
  layering already makes.
- `guaranteeStartingResources(game, context, wheatRange, woodRange, clearRadius=0,
  protectedWalls=nullptr, allowedTopup=nullptr)` is a
  reachability backstop, not a placement pass: it compares a resource-respecting flood from each
  boot tile against the same flood with resources ignored, and where the two disagree — a
  colony's own tile is on well-connected land, just walled off from wheat or wood by a resource
  tile a ground unit can't stand on — it clears exactly the wall tiles responsible before topping
  up whichever resource is still out of range. A colony genuinely isolated on its own small spot
  (nothing reachable even through terrain alone) is left untouched, since there's nothing on the
  other side of a wall that isn't there. `protectedWalls` is an optional width×height mask of
  resource tiles that belong to the map's design, such as Stone highlands' ridgelines or City
  states' walls; they are
  treated like terrain, never cleared and never looked past. It wraps each boot tile onto the map
  first, since the height-field generators' fallback site search can hand over one past the edge.
  `allowedTopup` has a separate role: it confines *new* emergency wheat or wood to a chosen farm
  belt. If that belt is full of the opposite crop, the opt-in backstop trades at most a radius-two
  patch of accessible surplus crop for the missing one. It scores the current walking flood, so
  the new patch has an immediately reachable gathering edge. It does not trade protected walls,
  and maps that omit `allowedTopup` keep their former rescue behavior.
  RuggedArchipelago, ShatteredCoast, Fjord, Watershed, Braided river, Stone highlands, Ring world, City states,
  Tidal flats, Everglades, Spider web and Coral call this, as do the height-field generators, and Concrete islands and Isles at any wheat or wood amount
  other than 100.
  Maze doesn't need it: its deposits are placed only along passage shores, leaving a clear lane
  down every passage, and its `validateWorld` confirms every colony can still walk to every
  other.

Every guaranteed placement (starter kits, bank guarantees, per-team clumps) keeps wheat and wood
at a 1:1 ratio, since those exist for reachability fairness between colonies. Ambient/bonus
layers (`scatterResources`, Fjord's bank-scatter roll, Maze's scattered deposits) instead use 2:1 corn:wood, which is purely
a feel decision independent of the fairness guarantee.

### Fertility

`Fertility::Field` (`src/map/FertilityField.{h,cpp}`) is a closed-form evaluation of
`Map::growResources`'s own regrowth rule — a wheat or wood tile expands when a randomly-offset
mirror point is water and its own mirror isn't sand, which integrates to a triangular kernel
over every nearby water tile with a per-sand-tile correction. `FertilityCalculator` (the map
editor's fertility tool, and old-save-format migration) is a thin wrapper over the same field, so
gameplay fertility and generation-time fertility scoring are the same computation. The field
takes plain terrain/sand masks, not a live `Game`, so a generator or the scoring pass below can
evaluate a candidate's growth potential without needing a finished map.

## Colony placement and fairness

Two further shared pieces score and choose where colonies actually start, beyond a generator's
own terrain and resources:

- `shared/BalancedStarts::chooseBalancedStarts` (used by the four height-field generators)
  scores every legal site by its *worse* primary resource — a colony beside wood but far from
  wheat is not a good start — using the finished, as-built state: the swarm's clearing already
  cleared, its footprint impassable, the flood starting from where workers actually stand. It
  takes the narrowest score window that still holds enough mutually distant sites, falling back
  to the older any-legal-site search on maps where no set of sites can reach both resources at
  all.
- `shared/StartQuality::scoreStarts` measures the *finished* map's colonies: the walk to every
  resource, the stock standing within 12, 24 and 48 walking steps and how much of it no rival
  reaches sooner, buildable room, territory held outright and contested, ground fertility, and
  the distance to the nearest and farthest rival. What those measurements are worth is not
  decided here: `FairnessModel.h` turns them into a start's fitness with coefficients fitted to
  thousands of real games, a softmax over the map's colonies turns fitness into a chance of
  winning, and the map's score is the fairness of those chances — 1 when every colony is as
  likely to win as any other. See [FAIRNESS_MODEL.md](FAIRNESS_MODEL.md). `scoreStarts` also
  stamps the same `Fertility::Field` it computes for
  scoring into `Map::Tile::fertility`/`Map::fertilityMaximum`, which is otherwise only ever
  populated by the editor's fertility tool or old-save migration — without this, every generated
  map's in-game fertility overlay would show nothing at all.

`GenerationService` rolls `kSampledCandidates` (5) seeds from a root seed and keeps the
best-scoring one that generates successfully; `GenerationService::bestSeed` is the same search
used by the map editor's regeneration action. Generation is deterministic and the score is a
pure function of the finished map (asserted in `map-generator-defaults-test`), which is what
makes "roll several, keep the best" and "regenerate the winning seed later" both sound.

The lobby's Landscape field opens `LandscapePickerScreen`, a full-window modal that shows every
playable landscape as a freshly generated map at the draft's current size and colony count, with
a Regenerate all button. `LandscapePreviewer` rolls those previews on background threads (one
roll per landscape, up to three seeds before a tile reports no preview) and hands back the seed
each shown map came from; the lobby then rolls that one seed instead of sampling five, so the map
a player picked by sight is the map the preview shows and the match starts on. The lobby's own
five candidate rolls run on the same workers: the best-scoring seed is rolled once more on the
UI thread for the snapshot, so a large map with many colonies no longer freezes the lobby while
its candidates roll. Any later edit to
the draft drops the remembered seed and returns to candidate sampling. The picker takes only a
list of localized names and requests, so the editor or a multiplayer lobby can run it too.
Background rolls are safe because `syncRand()`'s state is per thread: a worker seeds its own
stream inside `GenerationService::generate` and never touches the menu's live colony on the UI
thread.

### Premade bases and the worker count

The structural check after generation (`validateGeneratedWorld`) requires every colony to hold a
swarm and exactly as many WORKER units as the lobby's shared "Starting workers" control (1 to 8).
A landscape built on `shared/Bases` starts thirty-odd colonists, so it owns that number through a
control of its own (`colonists`, 16 to 48) and says so with `GeneratorDefinition::startingWorkers`,
a hook the check compares against instead; the lobby's "Starting workers" value is ignored by The
Glacis, Allotments and Caravanserai, and warriors and explorers are never counted. Ground that is
cramped on purpose for every colony (Allotments' lots, Caravanserai's capital disc) needs no
allowance in the seed ranking: the fitted fairness model compares a map's colonies with each other,
so room scarce for all of them does not mark the roll down.

## Fjord continent

The richest generator, and the one most of this framework's resource work was proven against:

- **Shape.** A `ShapeTransform`-warped `RadialShape` coastline; an untouched core disc
  (`coreR`, currently 0.23x the continent radius) that every fjord stops short of, so it always
  stays connected land regardless of coastline roughness. A fjord is carved between every pair of
  angularly-neighboring teams as a smooth S-curve centerline from just outside the coast in to
  `coreR` (or, in lake-connected mode, well inside the lake — see below). On a rectangular map
  the continent is stretched along the longer side by `Stretch` on top of its own transform, so it
  fills the map as an oval; square maps are unchanged.
- **Central lake.** `lake-size` (0–90%, of `coreR`; 0 disables it) carves a lake at the exact
  center, ringed by a sandy no-man's-land wider than `Map::controlSand()`'s own coastal fringe
  (`sandy-lake-shore`, on by default; off, the lake has an ordinary beach).
  `lake-connected` (a switch, default off) decides whether the fjords actually cut through into the lake —
  every peninsula then water-isolated from its neighbors, boats required — or stop short behind a
  solid land ring, keeping mutual land connectivity; the latter is verified with an explicit
  flood-fill after construction rather than assumed, and only runs in that mode, since a
  lake-connected map is *supposed* to fail a same-landmass check by design. A fjord's carved tip
  is widened specifically in connected mode, since `controlSand()` erases any water tile with a
  grass neighbor in its own 3x3 neighborhood and a narrow tip is entirely coastal by that rule.
- **Core resources.** The ring around the lake carries several stone clumps and a grove of every
  fruit type — a genuinely rich destination, not a single token deposit. The lake itself gets a
  center-anchored algae clump plus bonus clumps drawn from well inside its shoreline, so a
  deposit never reads as merely stuck to one edge.
- **Outlier islands.** `resource-islands` (0–20) places small, unconnected islands out in the
  open sea, each themed to one resource. Each candidate is checked directly against the
  coastline's `radiusAt()` at its own position rather than against one global worst-case bound,
  so an island can land close to a narrow stretch of coast even while the coastline bulges out
  far away in some other direction.
- **Fjord banks.** Every fjord guarantees one corn and one wood clump per side, placed last so
  nothing else can overwrite the guarantee, plus six lighter best-effort clumps per side mixing
  corn/wood/stone along the same bank (`bank-deposits`, on by default). Each amount places its
  rolled clumps that many hundredths of a time, drawing any chance from a stream of its own.
- **Ambient layer and backstop.** `scatterResources` fills the continent interior at the end
  (corn:wood 2:1, fruit, stone; algae left to the dedicated shoreline/lake passes above), and
  `guaranteeStartingResources` runs last as the reachability backstop described above.

## Maze

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
  share its tile and none can be slipped between at any angle. The undermap is then designed from
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

## Watershed

A branching river system draining into a sea along one side of the map: fertile floodplains,
dry sandy uplands, and sand fords across the channels.

- **Layout.** The coast, springs, flow tree and fords are planned in a canonical frame from named
  streams, then oriented: the sea can face any side of a square map but faces a short end of a
  rectangular one, so the rivers run the long way. Each map has one river network.
- **Rivers.** Springs are sampled by land area (`river-density`) and join the network downstream
  of them nearest-first; a path that runs into another river becomes its tributary. Width grows
  with the square root of the springs upstream (`river-width`), and the trunk splits into one to
  three distributaries on a delta lobe (`river-delta`, on by default; off, one mouth and no lobe,
  with the other distributaries still planned so the rest of the network is unchanged). Every
  path meanders (`meanders`, on by default; off, rivers run without their meanders).
- **Channels that keep their water.** Channels are stamped wide enough to keep a four-connected
  water core under an all-at-once version of `controlSand`'s grass-to-sand rule;
  `Map::controlSand()` is then required to change nothing.
- **Fords and uplands.** `fords` sets the spacing of sand strips laid across straight stretches
  clear of every other channel. Ground farther from water than `dryness` allows turns to sand,
  never within 11 tiles of water. Springs rise inland, so a ford is the shortest crossing between
  banks, not the only route.
- **Resources and colonies.** Colonies take fertile floodplain sites spread across banks, each
  with an identical wheat, wood and stone kit. Farmland ribbons follow the rivers at 2:1 wheat to
  wood, with stone at the upland edge, fruit at confluences and algae at the mouths.
  `guaranteeStartingResources` runs with 16-step ranges. `validateRequest` allows one colony per
  1,024 map tiles.
- **Checked, not assumed.** `validateWorld` plans the layout again from the request and checks
  that every ford interrupts a real channel, is open across its width and has walkable land at
  both ends; that every channel keeps its water core outside fords; and that every colony can walk
  to colony 0 and stand beside wheat and wood (water, buildings and every resource block).

## Stone highlands

Open grass valleys divided by thin stone ridgelines, joined by passes, with a pond in every basin.
Stone is never used up and players can't clear it, so the passes are the only ground routes for
the whole game.

- **Ridges.** Basin sites spaced about `valley-size` apart are scattered on the torus, and every
  tile takes its nearest site after a periodic warp. A tile becomes ridge when any of its eight
  neighbours has a lower label, so no unit can slip through diagonally; noise thickens some
  stretches to two tiles (45% of the ridge at the default stone amount, which scales that
  share), and enclosed pockets under 40 tiles are filled.
- **Passes.** A random spanning tree of `pass-width` openings, cut mid-ridge, joins every valley;
  `loopiness` opens that share of the remaining shared ridges.
- **Homes and ponds.** Colonies take the roomiest valleys by farthest-point spreading, one per
  valley while they last. Ponds (`pond-size`, a percentage of each valley) then grow in basin
  interiors away from ridges, passes and homes, with two-tile beaches so algae can regrow; valleys
  under 120 tiles get none.
- **Resources.** Identical 1:1 wheat and wood kits beside every home, 2:1 farmland ringing the
  ponds, algae in the water, and fruit groves (four per 128×128 at 100% `fruit-amount`) only in
  valleys no colony starts in (unless `home-valley-fruit`, off by default, lets them grow in home
  valleys too).
  `guaranteeStartingResources` runs with the ridges as protected walls, and any resource left in
  or beside a pass is removed. `validateRequest` rejects maps smaller than four valleys or with
  fewer than 1,024 tiles per colony.
- **Checked, not assumed.** The layout is a pure function of the request, so `validateWorld`
  rebuilds it and checks that every ridge tile holds stone, every pass is open and leads to both
  its valleys, and every colony can walk to colony 0.

## Symmetric arena

An orchard island behind a moat at the exact centre, crossed by sand causeways, giving 2, 4 or 8
colonies identical starting ground.

- **Symmetry.** A half turn for 2 colonies; a quarter turn for 4 on square maps and both mirrors
  for 4 on rectangular ones; all eight square symmetries for 8, on square maps only
  (`pointSymmetry`, `shared/Orbits`).
  `validateRequest` rejects other colony counts and layouts that leave too little room.
- **Built, not copied.** Every decision is either a function of a tile's whole orbit (integer
  noise summed over the orbit, the integer squared radius) or is made once for colony 0 (home,
  pond, starting kit, shortest route to its causeways, swarm and workers) and stamped onto every
  image, with the swarm's top-left anchor recomputed per image. Terrain is written with
  `controlSand`'s rule applied to every corner at once, because the engine's row-order pass is
  order dependent, and resource amounts drawn from the engine RNG are equalised across each orbit.
- **Centre.** `centre-size` sets the island's radius and `moat-width` the water around it;
  `causeways` and `causeway-width` choose one gate straight towards each colony or two flanking it.
  With `moat` off (on by default) the island joins the land around it, with no causeways; the
  moat's width still spaces the homes. The orchard deals two-by-two fruit groves out an orbit at
  a time, turns some orbits to stone (`orchard-stone`, on by default) and always keeps all three
  fruits; the fruit amount keeps that share of the fruit orbits nearest the centre, never fewer
  than three. It is the map's only fruit.
- **Outside the ring.** Symmetric lakes (`lakes`), shore farmland, stone outcrops and algae scaled
  by `richness` and each one's own amount. Each home gets a pond and a fixed kit, and each
  colony's shortest route to its causeways is kept as clear land. `scatterResources` is not used.
- **Checked, not assumed.** `validateWorld` requires corner, terrain, deposit, building and unit
  invariance under every symmetry, with a consistent colony permutation that reaches every colony,
  and equal walking distances from every colony to wheat, wood, each fruit and the orchard. Only
  the starting state is symmetric: in-game growth uses the synced RNG. `scoreStarts` counts
  building sites by top-left anchor, so its fairness reads just under 1 on this map.

## Ring world

One continental belt wraps the map along its longer axis (horizontally on square and wide maps),
and the seas either side meet across the other wrap: there are no corners, and every colony has
exactly two land neighbours.

- **Belt.** The centre line and width are whole-number harmonics of the map length and the coast
  is lattice noise whose cells tile the torus, so the seam can't be seen. A dry spine beside the
  centre line and one shared coast budget keep the belt a single landmass and the ocean a band
  whatever `belt-width` (a percentage of the map's breadth) and `coast-roughness` ask for. With
  `winding-belt` off (on by default) the centre line runs straight round the map. Terrain
  is stamped with an order-independent beach pass rather than `Map::controlSand()`.
- **Lakes and islands.** `lake-density` adds inland lakes that keep land between them and the
  ocean and stay off the spine; `resource-islands` (per 128×128) adds themed islands out at sea.
- **Colonies and resources.** Evenly spaced, jittered slots alternate coasts (`both-coasts`, on
  by default; off, every colony takes the same coast) and sit on the
  spine-connected belt at one fixed distance from water, with identical starter patches; then
  `scatterResources`, a shallows algae pass and `guaranteeStartingResources`. A final road pass
  clears only the deposits on the cheapest walk from each colony to the next, closing the loop.
  `validateRequest` needs at least 24 tiles of belt length per colony.
- **Checked, not assumed.** `validateWorld` floods from colony 0 while counting seam crossings,
  with water, buildings and every resource blocking, and requires every colony reached, a walkable
  loop around the map, and a body of water that wraps beside the belt.

## City states

A large shared commons in the middle of the map, ringed by a strait, and round it one big home for
every colony: a wedge of the outer land with its own lake, fields and quarry, cut off from its
neighbours by water channels and from the commons by the strait. The only way off a home on foot is
its causeway, a road across the strait lined with stone, landing on the commons at the home's own
angle; a wall of stone round every home's coast keeps anything landing from the sea on the beach.
The commons is where the game is fought, richer towards its centre, where an orchard of all
three fruits stands round the central lake; once swimming pools let armies cross water anywhere, the
causeways stop being the only way in.

City states stays round on a rectangular map, in a circle on the shorter side: stretching it gives
the homes different shapes (deep and narrow along the long axis, wide and shallow across it), and
fairness falls from about 0.97 to 0.65–0.8, because a home's fields and fertility depend on its
shape.

- **Geometry.** A pure function of the request (`geometryFor`). The commons' radius is
  `commons-size` percent of half the shorter side, the strait `strait-width` percent of the shorter
  side, and the homes reach out to the map's half side less a rim of sea, so opposite homes never
  meet across the wrap. Both coasts are radial shapes (`coast-roughness`); the strait follows the
  commons' coast. Every home is the same wedge turned round the centre, so the layout is fair for
  any colony count. `validateRequest` needs at least 20 tiles of home between the strait and the
  sea, and at each home's inner coast at least the causeway plus ten tiles of arc beyond its
  channel.
- **Causeways and walls.** One causeway per home at the wedge's middle: a `causeway-width` road
  across the strait with shoulders either side. With `stone-walls` (on), stone stands on every
  solid-grass shoulder tile and, round every home, on every solid-grass tile that touches the
  sea's margin (the land whose corners the beach reaches, and any beach joined to it), so every
  step off a beach lands on stone and the road is the only way in. Grass may never touch water, so the beach pass always
  leaves a sand lane outside the stone that a unit can land on and walk along but never leave; the
  causeway as a barrier, and as the ground kept clear of deposits with both its approaches,
  includes its lanes. Home lakes keep seven tiles from the sea so the two beaches never meet.
- **Sand roads.** With `sand-roads` (on), a line of sand two undermap vertices thick, which nothing
  can grow over or be built on, runs from the heart of the commons along every home's axis, over
  its causeway, to a main street seven tiles inside the gate that follows the strait out to both
  flanks. Side streets turn inland from it past both sides of the swarm and at its ends, stopping a
  few tiles behind the swarm and at most half the home's depth in, short of the lake and the kit's
  fields. A ring road at half the commons' radius, or at the fords when the heart is a delta, joins
  every home's road. A vertex only turns to sand on land, off ridges and causeway shoulders, at
  least five tiles from any water and three from any sand patch inside a home, and two from water on
  the commons, and clear of the swarm's square. Sand there could open a gap in a wall, since stone
  stands only on grass. Where a road would break a rule it simply stops. The sea's margin never
  spreads along a road, and valley lakes keep clear of roads.
- **Every roll differs.** Both coasts are random harmonic profiles periodic in the wedge
  (`coast-roughness`): bays and headlands with a finer ripple on the commons' coast, which the
  strait follows, and bays into the flanks of every home's inner and outer coasts, all held flat
  around each causeway; every channel bows sideways by the same random amount. One home layout
  and one heart layout are drawn per map. Homes: Lakeland (one lake), Riverside (a creek from the
  lake towards one flank with a sand ford), Highland (two stone ridges out to the sea with a pass
  each) and Marsh (four ponds); Barrens, both flanks of the home under sand, was dropped
  2026-09-14 ("that one is just a giant desert"). Hearts: a
  lake with the orchard on its shore, a stone crag with a gap towards every landing, an island in
  the lake reached by a ford from every landing, a delta of rivers from the lake to the strait
  between the landings with a ford each, and a belt of forest round the orchard. `sand` adds
  patches of sand (per 64×128 of land) over homes and commons, clear of lakes, landings and
  coasts. Every home feature is designed in the wedge's frame, arc across it and radius out, and
  stamped into every home alike, so any roll stays fair by rotation.
- **Homes.** Each has one lake at six tenths of its depth (held clear of both coasts), the swarm
  between its causeway and the lake, an identical unscaled kit of 40 wheat and 30 wood beside the lake and a stone deposit beyond
  it, then its own scaled ambient farmland on fertile ground, outcrops and a grove.
- **Commons.** A central lake of `kHeartShare` of its radius, `valleys` extra lakes (per 128×128 of
  commons) likelier towards the centre, farmland on fertile ground weighted by depth inward per
  `frontier-richness`, outcrops and groves by the same weight, and the orchard of the three fruits
  on the central lake's shore. Algae seeds every shallows. `guaranteeStartingResources` runs with the
  walls' stone protected, causeways and approaches are cleared, and a cheapest-walk pass keeps a
  way open from every swarm to its causeway and from every landing to the heart.
- **The archipelago** (2026-09-14: "WAY more islands out filling the no mans land", each with "the
  10x4 little sand building plots", "in the area outside the circle ... where the torus wraps", and
  bigger, since the plot "is taking up too much of the space"). The design circle sits on the
  shorter side, so the sea outside it - a square's four corners, which the torus joins into one
  ocean round the point across the map from the centre, and a rectangle's bands - held nothing.
  Round islets of radius 11 now dot it: one on the wrap point, then `islands` (0-4, 2) rings of
  eight, sixteen and so on at equal angles round it, at least two radii plus the moat apart, so the
  set has the map's own four-fold symmetry with mirrors; on a rectangle the two points across the
  wrap on each axis get the same rings. An islet whose disc and three tiles of water round it do not
  lie wholly in the sea - too near a home's outer coast, or another islet - is left out with its
  mirror images, so the symmetry holds. Every islet carries the farms' 10x4 building plot
  (`stampFarmPlot`, a clearing of grass in a two-vertex ring of sand) at its middle, kept clear of
  everything, and a small prize on its grass beyond the ring, stone, fruit and wheat in turn. An
  islet is only ever reached by swimming: a forward post, not a stepping stone. The archipelago
  cannot be turned round the centre for every colony like the rest of the design, so with three,
  five or six colonies it lies nearer some homes than others, which the lobby's best-of-five rolls
  cover, as on Canals. A 128 map's corners hold only the islet on the wrap point; a 256 map holds
  thirteen at the defaults.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every causeway road
  and ford walkable and every designed stone tile present (shoulders, walls, ridges and crag), every colony and the heart reachable on foot from
  colony 0, every islet's plot buildable and no islet reachable on foot, no colony reachable from any beach with the roads shut, no home able to reach the
  commons or another home with the causeways shut, and the colonies' walks to their landings within
  twelve steps of each other. `validateRequest` needs at least 20 tiles of home depth.

## Tidal flats

Grass islands standing on a wide expanse of walkable sand, with tide pools and lagoons over the
flats and the odd grassy sandbar. Sand carries units freely but holds no building and no deposit,
and nothing regrows beside it, so there are open-field battles from the first minute and no forward
bases: an army on the flats fights far from any inn or tower while a defender holds a rim of towers
on its island's edge, and expansion means taking another island whole.

- **Layout.** Every home island sits on a ring at 58% of the half side, one per colony evenly
  spaced from a random start, with a radius of `home-island-size` percent of the half side or as
  much as the ring, the wrap and the central island leave room for. Everything else -
  `extra-islands` neutral islands, `sandbars` and `lagoons` per colony and `tide-pools` per
  128×128 of flats - is placed in one wedge's frame, keeping clear of the home island, the wrap, the
  central island and each other, and stamped into every wedge alike, so the layout is fair for any
  colony count. `coast-roughness` shapes every island. `validateRequest` refuses a map whose home
  islands would shrink below nine tiles of radius.
- **Islands.** Each home has a pond at its middle, an unscaled kit of 40 wheat and 30 wood on the
  pond's two sides and a quarry towards the map's centre, then scaled ambient farmland on its
  fertile ground and an outcrop. Every neutral island is an oasis: a pond, and every other tile
  of it under unscaled wheat, so taking one means clearing it first, with one prize inside, a
  fruit grove or a stone deposit in turn. Since 2026-09-14 ("more of those pond + wheat or pond +
  wood oases, especially on smaller maps with more players", and "a few more of the random green
  patches"), `extra-islands` defaults to six per wedge (was three) and `sandbars` to eight (was
  three); an oasis is 30 to 45 percent of a home island's radius, never under 3.5 tiles, rounder
  than a home island, and keeps three tiles of sand from other features and four from a home island
  (every feature used to keep eight); sandbars, the green patches a forward post stands on, are 3.5
  to 5 tiles and keep the same gaps; and the scatter tries 240 times per feature (was 80). A 128 map
  with six colonies is the limit: its home islands nearly touch, and only an oasis or two of the
  smallest size fits between them and the central island. With `central-island` (on) an island at
  the centre carries a pond, the orchard of all three fruits and a quarry. Algae seeds every pool
  and lagoon, which is exactly where it regrows. Nothing is kept clear because the flats hold
  nothing.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every home's pond
  present and every colony and the central island reachable on foot from colony 0, with water,
  buildings and every resource blocking.
- **Rectangular maps.** The layout is designed in a circle on the map's shorter side and placed on
  the map through a stretched `WedgeFrame`, so on a rectangular map it fills the map as an ellipse,
  its islands, pools and lagoons stretched with it. Square maps are unchanged.

## Everglades

A dense wetland, the same everywhere and across the wrap. Pools and long sloughs lie on a jittered
lattice that tiles the torus, bigger where the land is wet and smaller where it is dry, and the
grass between them starts thick with wood and wheat. Every tile is a few steps from water, so wood
and wheat grow back at the engine's top rate; resources block movement, so the swamp grows shut
unless workers keep cutting it. Nothing grows on sand, and every pool's beach is sand, so the banks
between pools are the lanes that stay open, winding and narrow. Every colony starts in a clearing
with a pond and a kit, ringed by a sand levee with gaps the swamp creeps through; what lies beyond
is held only while it is kept cut. Nothing is symmetric on purpose: the landscape is one random
field over the whole map and the clearings are nudged off their ring, so it reads as country, not
as an arena. Fairness is statistical; the lobby keeps the best-scoring of several seeds.

- **Layout.** One pool per cell of a `pool-spacing` lattice that divides the map's width and
  height exactly, so the field wraps without a seam; each pool is jittered within its cell, its
  radius `pool-size` scaled by a smooth wetness field (0.6 at the driest ground to 1.5 at the
  wettest) and by a random factor, and `sloughs` percent of them are grown 1.4 times and
  stretched 2.2 times along a random line. The engine wraps every pool in a two-tile beach, so
  at the defaults (spacing 15, size 4) a 256 map is about 30% water, 34% beach and 36% grass;
  that sits at the threshold where the beaches stop linking into one network, so some lanes
  between neighbours stay open for ever and others close as the grass grows over. Closer or
  bigger pools link every beach into a permanent maze; sparser or smaller ones close the map. Clearings of `clearing-size` radius (rough outlines)
  sit near a ring at 58% of the half side, evenly spaced from a random start and each nudged by
  up to 12% of a wedge and 8% of the radius, with a pond at the middle, a `kLeveeWidth` band of
  sand round the outline of which `levee` percent stands (36 sectors, chosen at random), and no
  pool water for two tiles beyond it so the gaps open onto grass. The generator bends rather than
  refuses: a clearing that would not fit between its neighbours or inside the wrap at its fullest
  reach shrinks until it does (down to a radius of 6; the pond shrinks with it and a clearing
  under 9 has none, the swarm taking the middle), then the grass beyond the levee and the fit
  margin go, then the ring widens to 76% of the half side; pools whose
  expected coverage (mean pool area over cell area, overlaps discounted) would pass 60% of the map
  are scaled down to that, and `validateRequest` refuses only when even the smallest clearings
  cannot fit.
- **Resources.** Each home has an unscaled kit of 40 wheat and 30 wood beside its pond and a
  quarry on the side towards the map's centre; the swarm stands just past the pond's beach on
  the far side with the kit kept four tiles clear of it, so there is room to build. Off the clearings the
  swamp starts with wood on 55% of the grass and wheat on 15%, in patches, scaled by the amounts,
  with stone outcrops (which never grow) and fruit groves. Algae seeds the pools.
- **Routes opened, not hoped for.** Last of all, `openRoutes` walks from colony 0 and, for any
  colony it cannot reach, opens the cheapest way through: deposits on it are cleared and water on
  it becomes a sand ford (one sand corner per tile), so a boxed-in start is repaired rather than
  failed.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every clearing's pond
  present, no deposit on any levee tile, and every colony reachable on foot from colony 0, with
  water, buildings and every resource blocking.

## Spider web

An orb web of land spun over open water. Spokes run from a hub at the centre of the map out to a
frame thread, capture threads cross between the spokes sagging towards the hub, and every colony
starts on a pad where its spoke meets the frame. Threads are wide enough to build on, but every
ground route follows them: the knots where threads cross are the places to hold, the hub's
orchard is the prize, and the water between is crossed only once colonies can swim.

- **Web.** `spokes` per colony (raised on webs with few colonies so the web never reads as a
  star, lowered on crowded ones so halfway out neighbouring spokes keep 2.5 thread widths apart),
  each bowing sideways by a random amount that is greatest at mid-length. The frame joins the
  spoke ends. Capture threads are drawn with `bezierPath` and `strokePath`, `ring-spacing` tiles
  apart (squeezed on a small map to keep a whole turn between the hub and the frame) and
  `thread-width` wide (the width on a 256-tile map; a smaller map thins its threads with the square root of its size, to no less than 75%, so a 128-tile web still reads as threads), spokes and frame two tiles wider. Each sags towards the hub by `sag` and a
  random share of it; `torn-strands` percent of the distinct strands of a wedge tear, keeping a
  stub hanging from each spoke. With `spiral` (on) the capture threads are a spiral with one arm
  per colony that climbs one spacing per wedge; off, they are closed rings.
- **Fair by rotation.** A spiral arm climbing one spacing per wedge maps onto the next arm under a
  turn of one wedge, so every capture thread and its images share a key. Every choice about a
  thread — torn, sag, bow, stone at its knot — is a stateless roll of that key from the seed, and
  dew drops are chosen in one wedge and turned round the centre into every other, so every colony
  gets the same web.
- **Pads and hub.** Pads sit as far out as the wrap allows, `home-size` percent (60–200, 130 by
  default) of a standard pad of 13 tiles of radius, never more than 16% of the half side times that
  percentage so a small map's pads leave room for the web, and shrink down to 7 until neighbouring
  pads keep six tiles of sea between them; only then is the request refused. The hub is `hub-size` percent of the half side, with a pond once it is nine tiles across.
- **Resources.** Each pad has an unscaled kit of 40 wheat and 30 wood on the hub side of the swarm
  and a quarry beyond it. About 28% of knots carry a small stone deposit, `dew-drops` islets per
  colony carry a fruit grove or stone each, and the hub carries the orchard of all three fruits
  and a quarry. The threads' own wheat and wood are ranked and split by `PeriodicNoise` sampled in
  the wedge frame, so every colony's stretch of web is farmed alike: 20% of their grass under wheat
  and 13% under wood, so any stretch of thread passes a field or a copse. A few dry sand patches
  (7% of the threads' inland grass, three or more tiles from water) decorate the threads, sampled in
  the wedge frame too. Algae is counted over the shallows up to ten tiles out, one clump per 30
  tiles, and seeded on each colony's best-growing 30% of that water, the same number of clumps in
  every wedge.
- **Routes opened, not hoped for.** Beaches keep the threads walkable, but a small hub can be
  stocked shut, so after `secureStartingCrops` an `openRoad` from colony 0 onto the hub and to
  every other colony clears any deposits in the way.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every spoke's centre
  line to be land, every colony reachable on foot from colony 0 and the hub reachable too, with
- **Sand roads.** With `sand-roads` (on), a line of sand one tile thick (`tracePath`) runs down the
  middle of every spoke and whole thread, off the pads and dew drops; torn stubs, which are dead
  ends, get none. Every tile touching the sand loses the pure grass a deposit or a building needs,
  so nothing can grow or be built across a thread and close it. Threads thin on small maps to no
  less than 75% of the control, so grass remains either side of the road.
- **Rectangular maps.** The web is designed in a circle on the map's shorter side and placed on the
  map by `Stretch`, so on a rectangular map it fills the map as an ellipse; widths stay in tiles.
  Square maps are unchanged.

## Coral

Every colony is a sea fan of land. A single trunk leaves the colony's pad on the rim and forks, and
forks again, as it grows in towards the middle, so each colony's coral is a triangle opening
inwards: narrow and solid at home, wide and fingery at the far end. The water between sibling
branches is a triangle opening inwards too, the far ends of neighbouring fans reach into each
other's gaps, and all the fans meet in a tangle in the middle. Land gets thinner and richer the
further it is from home.

- **Grown, not drawn.** The trunk leaves the pad heading for the map centre, turned off it by `lean`
  degrees (every colony alike, so the map turns like a pinwheel); everything else is `growBranches`,
  which splits every tip in two, each child turned `fork-angle` degrees (with jitter) from its
  parent (the angle on a 256-tile map; smaller maps fork wider and bigger maps narrower with the
  square root of the long side, between 0.7 and 1.45 times, since a small fan needs wide forks to
  fill its wedge and a big fan's many levels curl into rings at a wide angle), a level at a time so both sides of every fork compete equally. `branching` sets the
  levels on a 256-tile map, one more per doubling of the map and one fewer per halving; the trunk is
  sized so the lengths of every level reach just past the centre, and where that trunk would be
  longer than 30 tiles each level keeps more of its parent's length (up to all of it) instead, so
  the fan fills out rather than hanging off a long bare trunk. On a small map levels are given up
  until the trunk clears its pad, so on 128-tile maps branching above 4 changes nothing.
  `branch-width` is the trunk's width; branches taper to about eight tiles.
- **Checked against every colony.** The fan is grown once, for colony 0, and turned round the map
  centre for every other colony, so it is fair however random the growth. Every branch and bud must
  keep `strait-width` tiles of water from all land already grown, from that land's copies in every
  other wedge and from its own copies; a refused branch is retried at half length, then dropped
  with its subtree. This refusal is what makes neighbouring tips interleave, and it keeps the land
  from ever reading as spokes and rings.
- **Bridges.** `land-bridges` per pair of neighbours join the narrowest straits between a fan and
  its neighbour's copy, 30 tiles apart, preferring straits away from the rim (where a bridge would
  join two trunks just below their pads); the search widens until every boundary has a bridge.
  With none, colonies meet only by swimming.
- **Pads.** Pads sit as far out as the wrap allows, `home-size` percent (60–200, 130 by default) of
  a standard pad of 12 tiles of radius (less on a small map, at most 15% of the half side), and
  shrink down to 7 until neighbouring pads keep six tiles of sea. A bigger pad is more room for the
  first economy; it also brings the trunk's root in, so at large sizes the fan has less room. Maps
  under 128 tiles across are refused, as are layouts whose trunk cannot clear its pad.
- **Resources.** Each pad has an unscaled kit of 40 wheat and 30 wood in front of the swarm and a
  quarry behind it. Remoteness is the walk along the coral from the nearest pad: 40% of forks
  carry stone growing from 3 to 9 tiles with it, and every tip a fruit grove from 3 to 12 tiles. The
  branches' wheat and wood are ranked by `PeriodicNoise` sampled in the wedge frame, leaning
  towards home: 30% of their grass under wheat and 21% under wood, since the branches are all the
  land there is. A few dry sand patches (8% of the branches' inland grass, three or more tiles from
  water, never on a pad, bud or bridge) decorate the wider branches. Algae is counted over the
  shallows up to ten tiles out, one clump per 35 tiles, and seeded on each colony's best-growing 30%
  of that water, the same number of clumps in every wedge.
- **Routes opened and checked.** After `secureStartingCrops`, an `openRoad` from every colony to
  its trunk's first fork, and with bridges from colony 0 to every colony, clears any deposits in
  the way. `validateWorld` rebuilds the design and requires every trunk's centre line to be land,
  every colony able to walk to its first fork and, with bridges, every colony reachable on foot
  from colony 0.
  middle of every branch that forks on and every bridge, off the pads and buds; dead-end tips get
  none, since they carry no traffic and their narrow land would be left without fields. Every tile
  touching the sand loses the pure grass a deposit or a building needs, so nothing can grow or be
  built across the road. Branches never taper below about eight tiles (`branch-width` 7–17, 11 by
  default, is the trunk's), so grass remains either side of the road.
- **Rectangular maps.** The fan is designed in a circle on the map's shorter side and placed on the

## Carousel

A ring of walled homes round a plaza, where every colony besieges one neighbour and is besieged by
the other. A colony's single door opens onto a narrow corridor that follows the ring to a small court
(the elbow) pressed against the next colony's home, and a narrow spoke runs from the court to the
plaza. The court and that home are parted by a thin wall of stone that no unit crosses but a tower
shoots over, so every colony's towers, in its own home against that wall, cover its neighbour's only
way out. The concept depends on the lanes being tight, so they default narrow and every wall is a
single line of stone with no water beside it. Inside the ring the sea stays: the plaza is an island
in a lagoon with algae in it, and the spokes cross the lagoon between bands of stone (2026-09-14:
farms reaching in towards the plaza made it "hard to know where you are on the map").

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
  beyond the ring (the inward field towards the plaza went with the lagoon, 2026-09-14), shared out
  by equal yield for the colony's row angle, so a colony whose rows fall on the diagonal gets more
  ground, and kept only where every colony's field has room. Once the walls stand (below), `layFarm`
  lays rows along the colony's axis at `bestFarmRows` widths over the whole walled field, keeping
  three tiles of land round the water (six against open sea), with a sand cap closing the crop rows,
  a sand bridge clean across the whole farm, water and crop rows alike, every 16 tiles (2026-09-14:
  "so they go clean across the whole farm"; `water-crossings` and `crop-crossings`, both on, switch
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
  (`furnishGround`) and no starter kit of wheat and wood blocks (2026-09-14: "there's already too
  much food on this map"; the farm feeds the home, and `secureStartingCrops` is the backstop);
  nothing is planted within six steps of a lane. One grove of one fruit per court, and on the plaza
  only fruit: an orchard of the three fruits between every two spokes' arrivals, with no wheat or
  wood to smother it. Algae seeds the lagoon's shallows.
- **Starting towers** (`planTowers`). `starting-towers` sets their level (0 none, when every site is
  an open pad; default 1 since 2026-09-14, so players upgrade their own) and `tower-count` how many
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

## Amphitheatre

A sunken arena in the middle of the map, ringed by walled terraces, and all round it a walled
territory for every colony with two inland seas beside its home. The outermost wall's ramps face the
colonies, the next wall's stand between neighbours, and so on down to the pit, so every step inward
is a meeting at a known place.

- **Arena.** `rings` walls `terrace-width` apart round a pit of `pit-size`, drawn with
  `ringWithGates`; ramps `ramp-width` wide alternate between each colony's axis and the middle of
  its wedge.
- **Territories.** The ground outside the outer wall is shared out by `balancedTerritories` from every
  colony's frontage (its wedge's arc just outside the wall): every tile goes to the colony whose frontage
  is nearest by steps less a weight per colony, and the weights are tuned until the areas are equal, so
  every border is a smooth curve of equal weighted distance between two frontages. (Until 2026-09-13
  the territories were grown a tile at a time with `territory-roughness` noise and smoothed by a
  majority filter, which left every border ragged; the control is gone.)
- **Homes.** Every swarm stands the same number of steps from its ramp (`siteAtDepth`), four tenths
  of the way to the shallowest territory's far end; the wheat and wood kit (no stone clump: the
  territory is walled in stone) faces the nearest sea; scattered fields by `furnishGround`.
- **Inland seas.** `bay-size` percent of the smallest territory, split into two seas, one either side
  of the home (`growLakeBeside`), each wholly on its side of the line from the ramp through the home
  and ten tiles clear of it, seven from any wall. Where any territory lacks the room, every territory
  gets one sea of the full size at its far end instead (`growFarLake`). Every colony's seas are the
  same size; land a sea closes off becomes stone (`strandedGround`). There is no sea shared between
  territories.
- **Prizes.** Orchards of the three fruits in the pit and on the innermost terrace, and an outcrop on
  every terrace, the same at every colony's angle.
- **Starting towers.** `tower-count` towers (default 3, at `starting-towers` level, default 1 since
  2026-09-14) and three open pads per colony in its own territory, each directly against the
  arena's outer wall within 18 steps of the colony's ramp mouth and clear of the ramp itself,
  covering the most of the arena (terraces, pit and ramps) over the stone (`chooseTowerSites`,
  `settleStartingTowers`). Until 2026-09-14 they stood along the border walls covering the
  neighbours' territories, which "make no sense": the starting towers hold the door, and a colony
  that wants towers on its borders builds them.
- **Checked, not assumed.** Every designed stone present, every ramp walkable, territories within
  `kAreaTolerance` percent, every colony's seas the same size, no territory reaching another or the
  arena with the outer ramps shut, and even walks to the ramps and to the pit.

## Switchbacks

A plateau in the middle of the sea and a ring of homes on the rim, each joined to the plateau by a
mountain of stone with one zigzag trail through it. Walking the trail takes several times the
straight-line distance, but the walls between legs are thin, so the trail is a fortification: every
colony's towers stand on its own trail against the inner side of each wall and shoot across it at the
next leg up. Every home reaches a walled wheat farm on either flank.

- **Geometry.** Designed in every colony's `AxisFrame` and turned round the centre; round on a
  rectangular map. Homes stand on the rim; the mountain fills the ground between the plateau and the
  home with as many legs as fit (`layLegs`): every wall, between legs and at both ends, exactly
  `leg-wall` thick (default 2), and the trails widened to use up the rest, so the stone stays thin.
  On a crowded ring the plateau grows until the mountains fit round it; a smaller map narrows the
  trail and walls with the square root of its size.
- **Trail.** `zigzagPath` gives the trail and each leg's straight run; everything else in the
  mountain is stone. A sand road runs down the trail's middle.
- **Farms.** Every home reaches out across its axis to a field on either flank, and the fields share all
  the open sea between the mountains and round the rim by equal yield for their row angles
  (`growFarmFields` with no gap), joined to the home with no coast between. Then every tile of sea left
  is filled to its nearest field (`fillToNearest`, the whole map as its reach), so the farms always
  grow to fill the available space, and a single line of stone stands on every border between one
  colony's ground and another's (`labelBorders`): nothing but stone parts two colonies, as on Carousel
  (third play, 2026-09-13; before this the fields kept three tiles of water from each other and the
  mountains, which on a 256x128 map took over half the sea). Rows run across the axis at `bestFarmRows`
  widths (`layFarm`, rim 3), with a sand cap, a sand bridge clean across the whole farm every 16
  tiles (2026-09-14; before, only the water rows; `water-crossings` and `crop-crossings`, both on, switch
  each half) and a 10x4 building plot under `farm-plots` (on);
  wheat with one woodlot (`plantFarm`).
- **Walls.** The mountains' rock, the border lines between colonies, and a sealed coast on whatever sea a
  design leaves (none at the defaults). Sea within a level-3 tower's range of the plateau becomes rock
  before the fill, so the mountains' inner ends join round the plateau and nothing outside the trails
  comes near it.
- **Homes and plateau.** Round homes (`stampRoundHome`) whose farms are their water on every size of
  map (128 maps had two home ponds instead until 2026-09-13), with no starter kit of wheat and wood
  blocks since 2026-09-14 (the farms on either flank feed the home; `secureStartingCrops` is the
  backstop). The plateau has a pond and only fruit, an orchard of the three fruits between every
  two summits, so nothing overgrows it.
  2026-09-14) and four open pads per colony, all on its trail
  beside the walkway down its middle, each directly against stone on the inner side of a wall - the side
  towards the middle of the map - so it shoots across the stone at the next leg up, where attackers
  coming down from the plateau pass (`chooseTowerSites` counting the colony's own trail). A trail too
  narrow for a tower beside its walkway gets fewer, the same for every colony, and no site may close a
  trail (`settleStartingTowers`).
- **Checked, not assumed.** Every designed stone present, every farm walkable from its colony
  (`farmReachable`), no land reachable from the sea, homes and their farms and the plateau apart with the
  trails shut and with only the middle legs shut (so no leg can be skipped), a level-1 tower at home
  reaching the first leg and one on the plateau reaching the last, and even walks to the plateau.

## Fingerprint

An organic labyrinth: a Turing pattern grown over the whole torus (`turingPattern`, `Patterns`)
draws long curving bands that fork, merge and dead-end the way a fingerprint's ridges do, and the bands
become the barriers.

- **Pattern.** `wavelength` (12-48, 30; the first play found 20 read like Everglades) is the
  crest-to-crest width, so about half of it is corridor;
  `grain` (0-100, 0) stretches the blur along one axis so the bands run rather than wander. `pattern`
  chooses the barrier's share of the map: Labyrinth 42% (bands and corridors read alike once a water
  band's beach has taken a tile of each side), Islands 60%, Channels 28%.
- **Barrier.** Water, so every corridor is beside water and all the land is farmland (a share of the
  fertile ground under crops in patches); or Stone, permanent walls with pools in the corridors, the
  only fertile ground. The pools lie at the bottoms of the field's troughs: every local minimum with
  nothing lower within two wavelengths (`windowMinimum`), grown to 40 tiles along its trough; a
  percentile of the field instead gave a hundred tiny pools whose beaches cut the stone into blobs.
- **Homes.** Clearings on a lattice (`latticeSites`), each a rough disc of `home-size` (10-24, 18)
  with a pond (`stampRoundHomes`) and a margin of three tiles of pattern cleared beyond it, shrunk so
  a band's width of pattern always runs between neighbours. Fairness is statistical, like Everglades'.
  The ambient layer is 22% of the fertile ground under wheat and 12% under wood, an outcrop per 600
  tiles and a grove per 1000 (the first play found half that "very empty").
- **Routes.** The pattern owes nobody a way through: `openColonyRoutes` opens the cheapest way from the
  first colony to any it cannot walk to, a sand ford across water or a gap cut in the stone.

## Rain shadow

A map with a grain: long stone ridges run across the torus, parallel and a valley apart, and the wind
blows across them. The windward foot of every ridge catches the rain (a chain of pools, and the
farmland), the lee side is a band of dry sand where nothing grows or builds, and passes cut every ridge
at intervals staggered from ridge to ridge, so moving along a valley is easy and crossing is slow.

- **Ridges.** `ridges` (2-8, 4) crossings of the map, tilted by `slant` (0-3, 1) so following one across
  the width climbs that many ridges: stripes that wrap the torus exactly (`stripePhase`), so with a slant
  every ridge is one spiral, which is what staggers its own passes from one crossing to the next
  (straight ridges stagger by parity instead). On a small map ridges are taken away until a valley
  holds a home with its margins, the pools and the sand (four suit a 256 map, a 128 map gets two).
  `ridge-thickness` (2-6, 3): three tiles is sealed against diagonal steps and within a level-1 tower's
  reach across (`towerReach`).
- **Passes.** Every `pass-spacing` tiles (24-96, 48) along the ridge, `pass-width` (3-9, 5) wide,
  laid out by the phase along the ridges (`alongStripes`), so they repeat seamlessly.
- **Wind.** Streams run along the windward foot, 20 tiles long every 24 and 7 deep (the first play
  wanted the ponds "deeper and more connected"; the second, 2026-09-14, "a couple tiles larger"
  still), four tiles out from the stone
  (three took the ridge's windward row with them: a pool's beach spoils the tiles round it and stone
  stands only on pure grass); the lee sand starts two tiles behind the ridge for the same reason and
  runs `lee-width` (2-12, 6) tiles, measured downwind from the stone itself (`upwindSteps`) so it stops
  where a pass lets the rain through.
- **Pass roads, lakes and rivers.** A line of sand runs through every pass and 18 tiles into the
  valley on either side (12 until 2026-09-14), so a pass never grows shut, and on the windward side
  it ends in a lane of sand along the middle of the foot out to the streams either side of the pass,
  so the road runs into their beaches instead of stopping short of them in the grass; no stream lies
  within 4 tiles of a pass along the ridge. `inland-lakes` (on) throws lake sites 56 apart, keeps those within 18% of a valley's middle
  phase, grows a rough lake of radius 5 at each and joins it to the nearest stream by a wandering river
  one tile wide (`wanderingPath`); `sand-patches` (0-20, 6) percent of the valleys' grass is sprinkled
  into sand patches (`sprinkleSand`). All from the first play: "the valleys feel too empty".
- **Homes.** On a lattice, each slid along the wind to the middle of its valley, so every home has the
  same ridge behind it and the same foot before it. Crops go on the fertile ground, which the pools make
  the windward side of every valley: a third of it under wheat and a fifth under wood.
- **Checked, not assumed.** Every ridge tile that could hold stone does, every pond present, every
  colony walkable from the first through the passes.

## Locust

An additive finite-food landscape built on the shared Old Growth clearing geometry. See
[Locust design, budgets, reusable operations and verification](LOCUST.md) (named Vultures until 2026-09-16). New shared
operations are `pureTiles(Map, type)`, `capResourceStock` and `startingAccessFailure`; existing
map policies are unchanged.

## Old growth

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

## Canals

A lagoon city: the map cut into blocks by a grid of narrow canals, every canal just wide enough to stop
a unit and just narrow enough for a tower on one bank to shoot the other, and only a handful of sand
bridges. Towers reach across from the first minute and armies cannot, so where the first towers go is the
opening; once swimming pools are built every canal is a road.

- **Blocks.** A tiling of `block-size` (16-40, 24) - squares, or with `block-shape` hexagons
  (2026-09-14, "an option similar to the maze generator"; a hexagon's pitch is 150% of the size, so
  its blocks hold about as much as the squares) - warped by `warp` (0-100, 80), every edge an
  obstacle the warp keeps apart (`warpCorners`), every edge stroked `canal-width` corners of water
  (3-5, 3). A canal's water is a corner narrower than the stroke and a diagonal canal is sealed against a
  diagonal step only when its water is two tiles thick, which is why the narrowest offered is 3; a
  straight canal w corners wide puts the banks' grass w + 4 apart (`Channels`), so 3 is reached by a
  level-2 tower and not a level-1.
- **Homes and block kinds.** The colonies' blocks are the ones farthest apart on the block graph
  (`spreadPockets`), dealt to the colonies at random, each with a kit and no pond (the first play asked
  for the pond gone; the canal waters the block). Every other block is dealt one of fourteen kinds and a
  facing from a weighted draw (6/9/8/10/6/5/7/7/4/9/8/7/6/8 in 100): plain fields; a lake; an orchard
  round a small pond; a homestead (a 4x4 pad of grass in a ring of sand, `stampFarmPlot`, with a wheat
  and a wood clump beside it); a hamlet of two pads; a quarry; a woodlot; a wheatfield; a dune of bare
  sand; and five kinds built of stone or water (second play, 2026-09-13: "too many of them are empty of
  resources ... add some types that are more like forts, with some stone that guides enemies into a
  kill zone"): a fort (a 13-tile square of wall round a pad with one three-tile gate), a bastion (four
  corner walls, an opening in every side), a funnel (two walls in a V onto a three-tile gap with the pad
  behind it), a chicane (two staggered walls with a corridor between) and a moat (a ring of water round
  an islet with a pad and one sand causeway). Walls are drawn in the block's frame, only where the warp
  left pure grass clear of water, and a block whose walls would cut its land or a bridge off gets none
  (`blockWalls`). Every pad kind and built kind carries the ambient fields too, a tile clear of its
  walls. "Each cell has its own little surprise." Fairness is statistical.
- **Bridges.** A tree of shortest block-to-block paths from the first colony's block to every other
  colony's, so every colony can be walked to, then `extra-bridges` percent (0-100, 30) of the blocks'
  count more at random (`openLoops`), so most blocks stay islands until someone swims. A bridge is a line of sand
  corners across the canal (a tile with a sand corner is no longer pure water) reaching onto both banks.
- **Towers.** `tower-count` towers at `starting-towers` level (default 2 of level 1, from level 2 on
  2026-09-14) and two pads per colony, on its own bank against the beach (`chooseTowerSites` with `against`), covering the most of
  other blocks' land across the canal; none may close the colony's walk to a bridge
  (`settleStartingTowers`).
- **Fields.** Every block is fertile (its canal is within the growth probe's reach of all of it), so a
  modest share of every block goes under crops in patches, the bridges' landings kept clear; algae in
  every canal, sand being everywhere.

## Polder

Reclaimed land, all of it: the whole torus laid out in rows of crops with a ditch of water between every
two, sand dykes across the rows at intervals, small grass villages for the colonies and hamlets
between them. Food is effectively unlimited; the game is logistics, on the dykes and the ditches' beaches,
until someone can swim.

- **Rows.** Stripes that wrap the torus exactly (`stripePhase`), 10 of crops and 6 of water along an
  axis (a period of 16 divides every map side; the yield fit's 10 and 8 would give 18, which does not,
  for under 3% of yield) and 11 and 5 on the diagonal. `row-angle` is Random by default (the first
  play asked for any angle each round): an angle drawn per map and rounded to whole turn counts on a
  circle of `s / 16`, so the rows stay 16 apart and the angle is one of some fifty on a 256 map;
  Vertical, Horizontal and Diagonal are fixed. Dykes every
  `dyke-spacing` tiles (12-48, 24) along the rows, two corners of sand wide, laid by the phase along the
  rows (`alongStripes`) over the ditches and, since 2026-09-16, through the crop rows too, as every
  farm-row map's bridges are, so a row still under crop is never a wall a worker walks the length of.
  `water-crossings` and `crop-crossings` (both on) switch each half (`FarmBridges`). With the ditches
  uncrossed the villages and hamlets are the only ways over a ditch that wraps the torus, and a seed
  whose colonies they cannot join fails validation (one of twelve at 256/4 and 128/2).
- **Villages.** Grass discs of `village-size` (8-20, 14) on a lattice, shrunk so a whole row and ditch
  lie between two, each in a two-tile ring of sand the crops cannot cross (first play: growth "quickly
  crowds out the base"); each holds a swarm, its quarry and a few buildings, and no wheat or wood
  blocks since 2026-09-14 (the rows a few tiles away are the polder's food). Two and a half 10x4 farm
  plots per colony (`stampFarmPlot`) are spread through the rows, each the farthest a plot can stand
  from every village, hamlet and earlier plot, and at least 18 tiles from any village. `hamlets` (on) puts a
  grass disc of radius 5 half way between neighbouring villages, room for a forward inn and a tower, with
  a fruit grove.
- **Fields.** 55% of the fertile row ground under wheat and 8% under wood (15% until 2026-09-14,
  "turn down the default amount of wood"), in patches; every row tile is
  a few tiles from water, so it all regrows. Only crops could close a lane, so `openColonyRoutes` clears
  only crops.

## Old town

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

## Anthill

Solid stone carved into chambers joined by winding tunnels, every chamber with a pond at its middle
and a sand road down every tunnel to the pond at each end. Every colony starts in a queen chamber, a
cul-de-sac with one door; the other chambers are farm chambers with a ring of wheat round the pond,
plain chambers with a wheat or wood patch in turn, or dead-end treasure chambers with fruit and wheat. Stone is everywhere and never runs out, but it cannot be
built on or cleared, so room is the one scarce thing: a chamber holds a few buildings, and growing means
taking the next chamber down the tunnel.

- **Chambers and tunnels.** Sites `chamber-spacing` apart (20-40, 28; `spreadPoints`), their nearest-site
  cells the graph (`cellGraph` over `siteNeighbours`); a spanning tree through every chamber but the
  queens' (`carveSpanningTree`), one door into each queen chamber, and `loops` percent (0-60, 20) of
  the chambers' count in extra tunnels. Every open edge is a wandering tunnel `tunnel-width` wide (2-4,
  3) between its chambers' middles (`wanderingPath`), with a sand road traced down its middle that
  stops on the pond's beach at each end (`sand-roads`, on: a sand corner spoils the tiles round it, so
  nothing can be built across a tunnel; first play had AIs walling themselves in); every chamber a
  rough disc of `chamber-size` (5-10, 7), farm chambers two bigger, queen chambers three, each turned
  by the golden angle so one outline reads as many. Every chamber's pond is 40% of the chamber's
  radius (a tile more for a farm, half a tile for a queen).
- **Kinds.** Dead ends other than the queens' are treasure chambers; of the rest every other one is a
  farm chamber. A queen chamber's swarm stands between its pond and its door, and the chamber is grown
  until it holds `queen-room` building sites (20-160, 60:
  overlapping 4x4 footprints, the start scorer's measure) with `growUntilSites`, within its own cell
  and never onto a pond's beach, so no colony starts with more room than another.
- **Rock.** Stone on every uncarved tile the beaches left pure grass; `openColonyRoutes` clears crops
  in a tunnel and cuts stone only as a last resort, at a cost that keeps it to a tile or two.

## The Glacis

Every colony starts inside a finished, walled compound - swarm, inns full of wheat, hospital,
school, barracks, racetrack, a quarry, a well with a wheat and a wood patch beside it, stocked
towers on the wall, thirty-odd colonists and a garrison of level-1 warriors and explorers - and the
first quarter hour of every other landscape is skipped. Between the compounds lies the glacis: a
plain of dry grass with no water, so nothing grows on it or grows it shut, yet buildable, so a
forward inn or tower there is a deliberate move under the walls' towers. Across the plain, one to a
band between the compounds' rows, run the wadis: sunken rivers two tiles wide with farmland along
both banks and a sand ford every so often, the only regrowing food outside the walls and, until
someone builds a swimming pool, the only way over.

- **Bases.** `colonists` (16-48, 32) buys a Hamlet, Town or City base (`shared/Bases`,
  `standardBasePlan`) and that many workers; `garrison` (on) adds three eighths as many level-1
  warriors and an eighth as many explorers. The lobby's "Starting workers" is ignored.
- **Compounds.** Square walls of stone at `compound-size` (12-20, 16) from the swarm, never less
  than the base's reach plus three (a walkway and two rows for towers), with `wall-gates` (1-2, 2)
  three-tile gates on the facing side and its back; a single gate faces a wadi. The compounds stand
  on a lattice (`latticeSites`, `dealStarts`), each facing a random way; a well of 2x2 vertices
  three tiles in from the back wall with 24 wheat and 16 wood five tiles either side of it (the
  first headless play had a colony starve on a dry compound), and the quarry from the base plan.
- **Wadis.** In every band between two lines of compounds, `wadi-count` (1-3, 1) water lines of
  three vertices (two tiles of water, a mixed tile either side) spread evenly through the ground the
  compounds and a two-tile margin leave, banks of pure grass three to six steps out on both sides,
  and fords of three sand vertices every `ford-spacing` (16-48, 24) tiles along it, a whole number
  round the wrap, each band's offset its own; a lane one tile wider than each ford is kept clear of
  crops through the banks (the first roll walled every ford in with wheat). Wadis run between the
  lattice's rows, or its columns when it has one row (512x128 with four colonies). A band too
  narrow loses wadis first, then the compounds shrink to what their base needs, then the map is
  refused.
- **Towers.** `starting-towers` (0-3, 1) and `tower-count` (1-4, 4): sites on the interior directly
  against the wall (`chooseTowerSites` with `against`), scored by the plain they cover, own ground
  and others' alike, two open pads besides; none may close the walk to a gate
  (`settleStartingTowers`), and every colony keeps as many as the fewest got.
- **Resources.** Banks nearest the water first: 70% wheat and 20% wood of the bank tiles at the
  default amounts, dealt into patches by a noise field; an outcrop per 3000 tiles of plain; a grove
  of one fruit on the bank beside every ford; algae in the wadis. `secureStartingCrops` and
  `reopenCrampedStarts` run with the walls protected, since a compound full of buildings is exactly
  what the cramped-start opener would otherwise open by eating a wall.
- **Checked, not assumed.** Every wall tile stone and every gate open (`wallStanding`), every well
  present, every base complete with its worker count (`validateBase`), the same number of towers in
  every compound, every colony walkable from the first over the fords, and every colony's walk to
  its nearest ford within a compound's width of every other's.

- **Played.** Rotation tournaments (six 256×256 maps, four colonies, every cyclic team rotation,
  45,000 ticks) with four Nicowars and then four Numbis both show the AI limit the premade base
  imposes: no colony breeds beyond its 52 premade units (three births each in 45,000 ticks).
  Nicowar declines from about 60 to 20 units with 25 to 40 starvation deaths per colony; Numbi
  holds 35 to 43 units with 11 to 18 starvation deaths and twenty-odd buildings, harvesting the
  well's wheat and the banks but never feeding the swarm. The map is a human-play concept; an AI
  tournament measures survival on it, not balance.

## Allotments

Every colony starts with a whole city staked out but not built: the swarm and one inn finished and
stocked, every other building of the base a level-0 construction site with wood stacked beside it,
thirty-odd colonists waiting to be told what to finish first. Beyond the city the whole map is
parcelled: a lattice of one-tile sand lanes cuts the ground into blocks and every block holds one
lot, a pad of grass in a ring of sand that exactly one building fits on. Down every third lane runs
a ditch of water, so the lots along it are fields whose crops regrow; the blocks touching a city are
its home fields, each with a well; alternate lots away from the water are woodlots; the smallest
lots carry a quarry or a grove; the rest are open, waiting to be built on.

- **Bases.** `colonists` (16-48, 32) and `garrison` (on) as The Glacis; the base is
  `standardBasePlan` of kind Sites: 3, 5 or 8 sites by tier, with five-tile wood stacks by the
  base's four corners, so no AI has to find wood before it can build.
- **Lanes and cities.** `layLanes` at `lane-spacing` (8-16, 12), a whole number of lanes round the
  wrap so blocks differ by at most a tile. Each city takes a superblock of as many blocks as hold
  its base on pure grass (two at a pitch of 12), snapped to the lanes with its inner lanes
  suppressed and its site at the middle, facing a random way; cities must keep a whole block from
  each other, else the lanes are laid closer (down to 8), then the map is refused.
- **Ditches.** Every `ditch-every`-th (2-4, 3) lane on each axis, offset by a draw, two water
  vertices wide (one tile of water: a single vertex is a puddle units walk through), kept two tiles
  from any city and off every crossing, where the sand stays as a ford.
- **Lots.** `lot-size` (Mixed, Small, Medium, Large): pads of 6, 4 or 2 (Mixed deals them in a
  fixed cycle), each held back for a ditch's beach and shrunk to what its block holds
  (`stampLotPad`). A block touching a ditch is a field; the four blocks touching a city are home
  fields, as large as the block allows, with a well of 2x2 vertices at the pad's middle (the first
  headless play starved a colony whose home fields never regrew); of the rest, odd blocks are
  woodlots with `wood-share` (0-100, 50) percent probability, small Mixed pads alternate quarry and
  grove, and everything else is open. Fields carry 80% wheat and 10% wood of their pad at the
  default amounts, woodlots all wood, quarries and groves one clump.
- **Checked, not assumed.** Every base complete, every open lot still buildable, every colony's
  walk to a ditch's beach within two lane pitches of every other's, every colony walkable from the
  first down the lanes.

- **Played.** With four Numbis (six 256×256 maps, every cyclic team rotation, 45,000 ticks)
  every colony builds its city of sites and then sits at 49 to 52 units with almost no
  starvation, three births and no fighting: the base gets finished and nothing else happens, which
  is the AI limit rather than the map's.

## Caravanserai

Every colony starts with a finished capital - the whole base, two stocked towers, colonists and
garrison - on a disc of grass with a pond and fields to live on, and nothing else at home: no stone,
no fruit, no algae. Everything else is desert: bare sand, walkable but unbuildable and foodless.
Half way between neighbouring capitals stand the outposts, discs round a pond with a quarry, an
orchard of the three fruits, algae and wheat, the only stone, fruit and algae on the map, each the
same walk from the two capitals that share it; along the way from every capital to its outposts lie
the oases, small discs with a pond, a patch of wheat and room for one inn and one tower. A colony
extends its reach one oasis at a time, and an army that outruns its oases fights hungry.

- **Bases.** `colonists` (16-48, 32) and `garrison` (on) as The Glacis; the base carries two
  stocked level-1 towers and no quarry.
- **Capitals.** Nearly round discs (roughness 0.04: at 0.1 a capital of 15 dipped under its base's
  corners) of `capital-size` (14-22, 18), never less than the base's reach plus five, each facing
  its first outpost, with a 2x2-vertex pond four tiles in from the back and 36 wheat and 20 wood
  either side of it (24 and 16 starved a colony in the first play).
- **Outposts and oases.** One outpost at the midpoint between each colony and each of its
  `outposts-per-colony` (1-3, 2) nearest neighbours (`nearestPairs`, `midpointAcross`): a disc of
  radius 9 with a 3x3-vertex pond, a stone clump of radius 2 to its east, the orchard to its west
  and 16 wheat. Oases every `oasis-spacing` (24-56, 32) tiles along the straight way from a capital
  to an outpost (`waypointsAlong`), starting half a spacing beyond the capital and stopping half a
  spacing short of the outpost, kept apart from every other disc: radius 6, a 2x2-vertex pond to
  one side, 8 wheat. A chain may hold no oasis on a small map; capitals shrink so an outpost fits
  between them, then the map is refused.
- **Checked, not assumed.** Every base complete, every capital's pond present, every colony's walk
  to an outpost and to an oasis within sixteen steps of every other's, every oasis with room for two
  2x2 buildings, every colony walkable from the first across the sand.

  every capital starves: units hold at 52 until about tick 20,000 and fall to under ten by
  40,000, with 45 to 51 starvation deaths per colony and no combat. The telemetry shows why: a
  Numbi colony harvests its capital fields about twenty times in the whole game (against about
  190 wood), so the stocked inns run dry once and nothing refills them. A short playtest sees the
  stable plateau; the collapse is after it. Whether the capital's wheat should stand where Numbi
  will harvest it, or the map is simply for people, is the decision to make before this map is
  offered to AI games.
## Braided river

A glacial outwash plain: a wide belt of interlaced channels and gravel bars runs the long way
round the map, a dry terrace lies along either bank, and a wall of bedrock bluffs stands where the
two terraces meet across the far seam of the torus. Homes are on the terraces; the bars are the
fertile ground; on foot the only way from bank to bank is over the riffles (sand fords) that join
the bars this map, and which bars join which is redrawn every seed.

- **Threads.** `channels` sinusoidal channel threads (3 to 9; 6 by default) wrap the map a whole
  number of times, each in its own lane across the belt (`braid-width`, a percentage of the map's
  breadth, 40 by default), neighbouring threads in antiphase so every adjacent pair crosses twice a
  period and the land between two crossings is a lens-shaped bar `bar-size` tiles long (half the
  period, rounded to a whole number of periods round the lap). Each thread's phase drifts, its
  swing swells and its width (4.4 to 7 corners) swells and narrows over whole cycles of the lap,
  so the weave never repeats and the seam cannot be seen. Lanes are held between 11 and 26 tiles
  by dropping or adding threads, so bars stay bar-sized on every map: 3 threads on a 128 map, 6 on
  256 and 7 on 512 at the default width. Every channel keeps a 4-connected core of open water
  outside its riffles (`channelCoreFault`), so no unit steps over one.
- **Bars and riffles.** The bars are the land components the rasterized water leaves, the two
  terraces the components outside the belt. `channelCrossings` finds every stretch of channel two
  bars face each other across; a riffle (`SandFord`, three rows of sand across the channel) is
  tried at the middle of each chosen stretch on the sketch itself (`stampFord`, then `fordFault`:
  open water either side of it, dry across, land at both ends, and no other thread's centre line
  under its sand), and one that fails is unstamped and its stretch passed over. Chosen: one forced
  crossing per colony, from its terrace to the nearest bar with room for wheat and wood whose
  riffle works; a random spanning tree of the rest over `DisjointSets` (long stretches between
  real bars first, stretches of 4 to 8 tiles and scraps of 12 tiles or more only if the banks are
  still apart); and `extra-riffles` percent (25 by default) of the leftover bar-to-bar stretches
  as loops, never two riffles within 9 tiles of each other on one thread. A seed whose bars do
  not join the two banks is refused for another.
- **Terraces, homes and the wall.** Homes stand 8 to 13 steps up the bank on the larger terrace
  half of a slot, one slot per colony along the lap alternating banks, dealt to the colonies at
  random. The terrace beyond fifteen tiles of the water grows nothing, so a town there is never
  overgrown; the bank strip in front regrows thinly; the bars regrow richly. A sealed line of
  stone bluffs (`traceSealedLap`) waving round the seam keeps the two terraces from being one
  plain, since walking round the back of the torus would otherwise be shorter than crossing the
  braid. With `moraine` (on) a broken line of stone hummocks (`runsAndGaps`) stands four steps up
  each bank with a door at every riffle landing and none against a home.
- **Resources.** Every home gets an unscaled kit (wheat and wood beside the swarm along the bank,
  a stone clump behind) and its promised bar an unscaled wheat and wood clump; every bar's grass
  is furnished a third under crops in patches along its shores (`furnishGround`, 2:1 wheat to
  wood), the bank strips at a quarter of that, the dry terraces get finite woodlots and stone
  outcrops and, so the plain behind the moraine is not one sheet of grass (a maintainer's first look
  found it bland), patches of sand: `dry-patches` (0 to 30, 12) percent of each terrace's inland
  grass where a period-18 noise peaks (`sprinkleSand`, rounded patches rather than speckle), eight
  steps or more from water so the bank strip and the moraine's contour keep their grass, two tiles
  off the bluffs and hummocks (a sand corner spoils the tiles round it and stone stands on pure
  grass) and clear of every town's room, so no colony loses building ground to them. The bars
  nearest the belt's middle get a grove of fruit each (one per 8,000 tiles, at least
  three), and the channels algae where it regrows. `secureStartingCrops` and `connectColonies`
  run with the bluffs and moraine protected; `reopenCrampedStarts` at non-default amounts.
  `validateRequest` needs a terrace at least 22 tiles wide (a 128 map at 60% is the edge; 64 maps
  are refused) and 36 tiles of bank per colony on each bank.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires stone on every bluff
  tile, every channel's core outside its riffles, every riffle open across with walkable land at
  both ends (`fordFault`, `fordLandingWalkable`), every colony walking to colony 0, and every
  colony walking onto its promised bar and standing beside wheat and wood.
- **Played, and bimodal.** A rotation tournament (six 256×256 maps, four colonies, every cyclic
  team rotation, four Nicowars, 45,000 ticks) split the starts: on four maps every colony grew to
  66 to 155 units, while on two maps three of the four starts stalled at 15 to 25 units (peaks of
  26 to 44, nine to eighteen buildings) with almost no starvation, no trapped units, and a fifth
  of the harvest of the start that thrived. The start metrics do not separate the two (wheat two
  to five steps away, comparable fertility and room), so this is an AI stall on the dry terrace
  rather than a geometry defect the generator can measure; the defaults stand, and the retained
  maps are the material for a follow-up with other AIs and human play.
- **Played again with the dry patches.** The same protocol on revision 2 (12% of the terraces'
  inland grass in sand patches): the same economy within noise (pooled per-start units 58 to 96
  against 62 to 103, wheat harvested per colony 218 against 239, births 102 against 109), the same
  bimodal stalls on the same maps, and no significant position bias. The patches cost the terrace
  nothing a colony uses; they are the look of a dry plain.
## Drumlin field

A lake land: a swarm of long oval grass hills, all pointing the way the ice went, with water in every
hollow between them, and between the hills wind eskers, sinuous sand causeways that are the only dry
way from one drumlin to the next until a colony can swim. Every colony starts on one of the biggest
drumlins: its blunt head is the town, a collar of sand across the waist parts the town from the tail,
and the tail is the colony's farm. Everything is fertile, so farm drumlins grow shut over the game;
the beach round every drumlin stays walkable, so the shoreline is always a road.

- **Grain and swarm.** A `Grain` (`grain`: random, horizontal, vertical or diagonal; a random grain is
  one of eight whole-step headings, refitted to the widest if it leaves the homes too close) with a
  stretch of `drumlin-length` percent (150-400, 250). Homes on a lattice (`latticeSites`, dealt), then
  darts under the grain `drumlin-spacing` apart (16-32, 20, widened by whatever `water-gap` exceeds
  its default 6 by, so a wider gap widens the lakes rather than thinning the drumlins;
  `spreadPoints` with the homes fixed and kept clear by two home radii and the gap), three rounds
  of relaxation under the grain with the
  homes held still (`relaxPoints`; without it the drumlins covered a quarter of the map, with it two
  fifths of the sketch), packed radii (`packLandforms`) with `water-gap` map tiles of open water
  (4-12, 6) between any two drumlins whichever way they lie, and a `Teardrop` fitting each radius,
  its widest point 35-45% back from the head.
- **Homes.** `TeardropHome` (Homes.h): the first 45% of the length is the town (swarm 25% back from
  the head, never more than 12 tiles short of the collar), a two-tile sand collar across the waist, the tail the farm: wheat 14 and wood 12 planted
  just past the collar (`teardropHomeKit`, `plantSplitKit`), a quarry near the head's tip, and the
  rest of the tail furnished as farmland like any farm drumlin. No home pond: the lakes are the
  water. Building room is scarce by design; the lobby's seed ranking needs no allowance for it,
  since the fitted fairness model compares towns with each other rather than with an open plain.
- **Sizing.** Homes are `home-size` (8-16, 11) half-width, shrunk to fit between their nearest
  neighbours under the grain, and shrunk again (to the floor of 8) so a farm drumlin fits at the
  lattice cells' centres between them; below the floor the request is refused
  (`planHomes`, the request check, plans only this much).
- **Eskers.** The drumlins' cells under the grain (`nearestSiteLabels` with a `Grain`) give the
  neighbour graph; a near tree (`carveNearTree`, 30% length jitter) joins every drumlin and
  `esker-loops` percent (0-100, 25) of the drumlins' count opens second ways; each link is a
  wandering corridor three corners of sand wide (`carveOpenEdges`, wander 3), laid over water only.
- **Prizes.** Farmland (farm drumlins and home tails) gets wheat 30% and wood 12% of its fertile
  grass in patches, the farm drumlins an outcrop per 2000 tiles and a grove per 1500
  (`furnishGround`), and half a colony's worth of orchards of all three fruits on the drumlins
  farthest from every home and each other (`farthestSites`, `plantOrchard`). Algae on the
  best-growing half of the water.
- **Checks.** With every tile touching an esker corner shut no colony walks to another
  (`colonyLeak`: the drumlins do not touch); no town grass touches tail grass (`firstRegionLeak`:
  the collar holds); with them open every colony walks to colony 0. `openColonyRoutes` clears crops
  only, never fords: an esker that fails is a failed map.
- **Cost.** A 512 map takes about 1.4 s against 0.4-0.5 s for Rain shadow or Stone highlands:
  four whole-map labellings under the grain (three relaxation rounds and the cell graph), each
  searching the bounding box of the grain's reach ellipse round every tile.

- **Played.** A rotation tournament (six 256×256 maps, four colonies, every cyclic team
  rotation, four Nicowars, 45,000 ticks) found the lake land healthy and unhurried: pooled
  per-start peaks of 110 to 190 units, 30 to 65 warriors, one colony in 96 eliminated (the
  drumlins are islands until the pools are built), prestige on every map. One start in six
  maps stalled at 35 units. The generator's colony index 2 won 14 of 23 adjudicated games and
  had the largest population on four of six maps although sites are dealt at random and the
  start metrics show no index pattern; treat that as an open question for a larger sample, not
  a measured defect.
## Continents

A real continent from [the world atlas](WORLD_ATLAS.md): North America, South America, Africa,
Europe, Asia or Oceania (`continent`, Random by default), floating in an ocean that wraps round the
torus, with the geography a player knows. The Great Lakes, Victoria and Baikal are lakes; the Sahara,
the Gobi and the Outback are sand; the Rockies, the Andes and the Himalayas are stone; the taiga and
the Amazon are wood; the Mississippi, the Nile and the Yangtze are rivers; and the plains people
farm are wheat country. A toy rather than a tournament map: the geography is fixed, no two colonies
get the same ground, and fairness is measured after the fact (the fitted model, the lobby's best of
several rolls) rather than proved by symmetry. What it promises is that every colony can start and
that the continent looks like itself.

- **The fit.** The region is fitted inside a sea margin (a 32nd of the shorter side, at least 3
  tiles: 4 on a 128 map, 8 on a 256, 16 on a 512), centred, turned a quarter turn when that fits a
  rectangle larger (`orientation`: Turn to fit, or Upright), and resampled by majority to the
  undermap's corners (`Raster`). The margin is the torus's seam, so a continent never meets itself
  across the wrap. The rest of a rectangle is sea; round islets in it (`islets`, off by default)
  are invented for whoever wants swimming prizes, not drawn from the atlas.
- **The coast.** Land narrower than three tiles, specks of sea and islets under twelve tiles go
  (`cleanLandmass`); a filled pool takes the land round it. Ocean and lakes are water, deserts and
  ice caps sand, everything else grass with its character planted on it.
- **Rivers.** The great rivers (`rivers`, on) are lines of pure water a tile wide, bridged at every
  diagonal step so nothing slips between two tiles, with a sand ford wherever a noise field of period
  24 peaks along the river within 12 tiles. On maps where a tile is less than 0.3 of an atlas cell
  (128 and below) rivers are omitted: a river's scar is three tiles wide whatever the map's size,
  and there it would take more land than the lakes do.
- **Sites.** Candidates are squares of pure grass on the mainland (the largest walkable piece, with
  rivers counted walkable since they are forded and islets excluded), 4 tiles out on every side (a
  9x9 square, enough for a swarm and its clearing), at least 6 tiles from any river (a colony inside
  a river loop is boxed in whatever the ground count says), and of those the better half by the
  fertile farmable grass within 10 tiles (`windowCount` over the grass where `cropGrowthField` gives
  a crop a chance) when that leaves two per colony and still spreads, so nobody starts in a range
  or on a cramped cape while the plains stand empty; otherwise every square competes. `farthestSites`
  spreads the colonies by walking distance, preferring at each pick a site with 900 walkable tiles
  within 24 steps (its catchment on the bare sketch); the room relaxes to 3 and then 2 while the
  closest pair is under 12 steps, and a spread whose squares would overlap fails the request.
  Territories grow from each colony's square (`growTerritories`, equal area, wandering borders); each
  site walks to the middle of its territory for up to two rounds, a round undone when it brings the
  closest pair under 70% of the spread's spacing; then the sites are dealt at random.
- **Oases.** A site whose ground could not regrow its crops (mean growth chance over the 21x21 square
  round it under 2500 of 65536, measured on the beached sketch with `cropGrowthField`) gets a pond of
  32 water corners (about 20 pure tiles, which waters a 30-tile square) dug 6 to 11 steps away on its
  own ground (`digPond`), and up to two more, each two steps farther out, while it stays under the
  floor. The floor scales with `oases` (0-200, 100); at 0 the geography stands and a dry colony starts
  on its finite kit. The floor comes from the first playtest: colonies starting under about 2000
  stalled once their kit was cut, colonies above 2500 grew.
- **Furnishing.** Each kind of land is a region furnished with its kit (`furnishBiome`): plains
  `farmland`, forest `woodland`, steppe `savanna`, tundra `barrens`, ranges `highland` (`mountains`,
  on; off, a range is savanna). Fields go where the water makes crops regrow, a dry reserve of finite
  crops on the rest, cover in patches from a noise field. Round every home two clearings: no ambient
  deposit at all within 5 tiles (an 11x11 square; on a fertile river bank the fields had filled the
  home square itself) and no cover within 8 (17x17, room to build before cutting). Every colony's kit
  (20 wheat, 16 wood, a quarry) is unscaled and goes in the inner clearing; islets, when asked for,
  carry a prize each (`stockIslands`); algae in the shallows.
- **Routes.** `openColonyRoutes` under a cost model (a clearable deposit 3, stone 6, water 10) with a
  lane three wide cuts a pass through scree or a ford where a colony cannot otherwise walk to the
  first; then the crop guarantee, then cramped starts reopened at non-default amounts, then the
  routes once more, since a top-up can land on the lane just cut.
- **Checked, not assumed.** The design rebuilt from the request, the mainland off the map's seam,
  and every colony's walk from the first.

Verified 2026-09-15: a local matrix of 624 requests (every size and shape from 64 to 512 on each
axis, 2 to 12 colonies, every continent, and 240 random draws over every control) generated 562;
all 62 refusals were requests with a 64-tile side that the continent cannot hold (40 of them
Oceania, 12 Asia), refused up front, and every combination with both sides at 128 or more
generated for every continent and colony count. At 256x256 with four colonies fairness (weakest
over strongest) runs 0.78 to 0.97 by continent and seed, the worst colony's building sites within
its catchment 54 at the tenth percentile and 167 at the median, and its mean fertility 1055 and
1886. In 56 four-colony games of Nicowar against Maxima across the continents on the pre-tuning
build, no colony of 224 failed to grow past twelve units, while the colonies eliminated first had
started with the least building room or the least fertile ground, which is what the site scoring,
the home clearings and the fertility floor above answer; a confirmation run of 28 games on the
tuned build had the worst colony's building sites per map at 61 to 310 (25 to 58 before), peak
units and buildings up, and more games decided by elimination. Linux and macOS produce identical
golden rows. Nobody has played it by hand yet.

- **Played, as a toy plays.** A rotation tournament (six 256×256 maps, four colonies, every
  cyclic team rotation, four Nicowars, 45,000 ticks) shows the geography deciding: pooled
  per-start peaks from 90 to 200 units, a root-mean-square position bias of 38 points, and on
  four of six maps one start that never passed about 30 units and was eliminated in every
  rotation by a rival 55 to 120 tiles away (more combat than starvation deaths). The start
  metrics (fertility 0.3 to 0.9, 180 to 500 sites) do not flag those starts, so what dooms
  them is who reaches them first, not what they hold. That is the contract the map states; the
  retained maps are the material for anyone who wants to move the never-grow starts.
## Plantations

An archipelago of small farm islands. Every island is a plantation: a square plot of grass ringed
with one vertex of sand at its heart, the only ground a building can stand on, and wheat or wood
over everything between that ring and the beach; the sea waters the crops, since no tile of an
island is more than about ten from the water. Two sand lanes run from every plot to the shore.
Nothing joins the islands: units swim, and the straits between the Voronoi cells (`Channels`'
`straitsBetweenCells`) are an exact corner width, four by default, so only a level-3 tower reaches
the next island's first grass. A plot is smaller than a base (10 tiles square), so every colony
holds several islands from the first minute: its home island with the swarm and a completed
swimming pool, and granted islands with a swarm, a pool and an inn each. On every one of a colony's
plots the swarm and the pool stand side by side along the top, the pool against the right edge
with a clear tile all round for its level-1 upgrade (4x4 to 6x6) and the swarm against that ring,
so a plot of 10 is exactly swarm, ring, pool, ring (8 until 2026-09-16, when a pool could not
upgrade). Every shipped AI built swarms on islands without a pool and pools on islands without a
swarm, breeding units that could never leave, so the pair is seeded rather than left to them (`claimNeighbourCells` deals them
round by round, as many to each colony as to any other), with a rock islet beside them. The rest
are neutral plantations of wheat, wood or both, orchard islets and rock islets. Every island is
first stamped as the nominal rounded square and the homes and granted islands dealt on that; then,
where the seed threw at least twice the islands the colonies need, the islands are reshaped: a
colony's own keep their size and vary only in squareness, wobble and a slight stretch (one that
loses its plot goes back to nominal), and the neutral ones draw the whole range, smaller or larger
(a large one fills its cell to the strait), stretched along a random heading with the area held,
rounder or squarer, more or less wobbled, so the archipelago reads as one coast's islands rather
than a tray of the same biscuit without changing what any colony was dealt. Room is the
scarcity and the swim is the cost; fairness is statistical (farthest-apart homes, nearest
granted islands) and the lobby keeps the best-scoring roll. See the generator header for the
sizes at the defaults, the shrinking order on small maps (crop band, then granted count, then
refusal) and every constant's reason.

- **Played.** A rotation tournament (six 256×256 maps, four colonies, every cyclic team
  rotation, four Nicowars, 45,000 ticks) found the archipelago even and hard-fought: pooled
  per-start peaks of 124 to 158 units, 28 to 44 warriors, three eliminations in 96
  colony-games, a root-mean-square position bias of zero, and 45 to 65 starvation deaths per
  colony as swimmers outrun their inns, which is the map's cost of the swim. The defaults stand.
- **Seen.** A maintainer's first look found the islands too alike, all one size and all round;
  revision 2 reshapes them as above. A first cut reshaped the colonies' islands with the rest,
  and the same tournament paid for it with a 30-point position bias and pooled per-start units
  from 104 to 183: what a colony is dealt must stay what it was. Reshaped after the deal, the
  same tournament came back to revision 1's numbers: position bias zero, pooled per-start units
  87 to 128 (revision 1: 81 to 125), wheat harvested per colony 208 against 201, births 190
  against 189, seven eliminations in 96 colony-games.

## Compatibility notes

- A `GeneratorDefinition::legacyId` is a stable compatibility identifier, not a display or sort
  order — `GeneratorRegistry::builtins()`'s constructor order is purely the product-facing
  catalog order. Once assigned, an id is never reused, including after a generator is retired.
- A generator's `revision` changes only when a change to `generate()` changes what a given seed
  actually produces; a pure refactor, comment or renumbering change leaves it untouched. Bumping
  it is how a reviewer knows a seed's output isn't expected to match a prior build byte-for-byte.
- `GeneratorControl`s are validated by `GeneratorRegistry`'s constructor: every control's default
  must land on a valid step from its minimum, and (`allowedValues` aside) `(maximum - minimum)`
  must be evenly divisible by `step`. A `GeneratorControl::Kind::Toggle` control must be exactly 0 to 1 in
  steps of 1, with no allowed values, power-of-two formatting or terrain weight; the lobby and
  editor show it as a checkbox. Every control label needs matching entries in
  `data/texts.en.txt` (`[Label]` / `Label`) and `data/texts.keys.txt` (`[Label]`), the same as any
  other UI string.

## Verification tools

- `test/MapGeneratorStudy.cpp` builds to `MapGeneratorStudy`, a CLI harness that invokes the real
  production generators directly: `MapGeneratorStudy <method> <seed> <profile-dir> [key=value...]
  [tuning] [headroom] [quality] [dump=path] [overlay=kind]`. `quality` reports `StartQualityReport`/
  `ColonyQuality` per colony; `dump=` writes a plain-text terrain/resource grid for direct
  inspection or scripted flood-fill checks; `overlay=` writes one measure per tile beside it
  (`growth`, the crop growth chance; `sites`, where a 4x4 building fits; `chop`, the cost from the
  nearest colony clearing crops on the way; `owner`, which colony that is); `--catalog` dumps every registered generator's
  controls as JSON, with each control's `kind` (`range` or `toggle`).
- `test/MapGeneratorDefaultsTest.cpp` builds to `MapGeneratorDefaultsTest`, asserting the
  registry's and every control's contract: discrete domains, shape bounds, topology, home
  footprints, exact worker counts, seed repeatability and RNG stream isolation, the
  lobby/editor UI's own control-editing behavior, and the shared toolkit's own guarantees:
  the point dispersion ends at a mutual best response checked against a brute-force score,
  the distance flood matches a Chebyshev oracle on the torus, with obstacles and repeated
  sources, and `test/MapGeneratorToolkitChecks.h` checks every module of `shared/` on a map
  built by hand — floods and their limits, beaches and islands, patches and algae bands, the
  cheapest route, strokes and shape fills, fields and clumps, settlements and the colony walk, the crop guarantee through and around a
  wall, a buried colony's room, balanced starts, per-landmass scatter, lattice noise, the
  wedge frame and the context's shuffle — so a change to a module fails there before it shows
  up as a changed golden fingerprint downstream. `test/MapGeneratorLandscapeChecks.h` does the same
  for the landscape modules: symmetry groups against their orbits, morphology and the distance
  transform against brute force, crop growth and dry zones, cost models and route opening, sites
  and cell graphs, pattern wavelengths and seamless stripes, wandering paths, the channel
  arithmetic against `towerReach`, building room, region homes and every biome kit.
- `test/MapGeneratorGoldenTest.cpp` builds to `MapGeneratorGoldenTest` and keeps the revision
  rule honest. `test/map-generator-golden.txt` records, per platform, the fingerprint (terrain,
  resources and colony starts) every registered generator produces for three seeds at 256, one
  at 128 and 512, and one each with two and eight colonies, keyed by the generator's revision.
  `MapGeneratorGoldenTest <profile>` regenerates this platform's rows and fails on any map that
  changed at an unchanged revision or any generator whose revision moved without the table
  following; `--update` rewrites this platform's rows and refuses (without `--force`) to record
  a changed map under an unchanged revision; `--print` writes the rows to stdout, which is how
  a platform's rows are first bootstrapped from a CI log. Generation is deterministic per
  platform, not across platforms, so rows carry the platform they were made on. A platform with
  no rows reports and passes, so a new machine can run the check before its rows exist; CI
  passes `--require-rows`, which fails instead, so the table has to carry rows for every
  platform CI builds on (`linux-x86_64` beside the maintainers' `macos-arm64`). `--sweep` rolls
  every playable landscape at the colony counts and sizes the lobby offers, five seeds at 128
  and 256 and three at 512, prints the success rate per cell and fails any valid cell where no
  seed generated: that is what a player would see as a failed generation. CI runs all three.
- `test/MapGeneratorProfileFixture.cpp` builds to `MapGeneratorProfileFixture
  <profile-dir> <seed> <rounds>`, a load generator for external sampling profilers (macOS
  `sample`, Linux `perf record`): it round-robins every registered generator for `rounds` passes,
  drawing shared and generator-specific controls at random each attempt the same way
  `GenerationRequest::randomizeControls` does, and prints a per-generator attempt/success/timing
  table. It is not wired into CI and makes no coverage claim; point a profiler at its PID while it
  runs, or use its own timings for a quick before/after comparison at a fixed seed and round count.
- The normal client's [map CLI](CLI.md) generates maps and PNG previews with
  `--generate-map`, loads maps/saves with `--preview-map`, and lists settings with
  `--list-map-generators`. It supports config files and CLI controls, and reuses the lobby/picker preview renderer.
  Compare a new generator with its nearest neighbours at 128, 256, and 512 tiles
  using the documented batch commands before showing it.
- `tools/new_map_generator.py <id> "<Display name>"` scaffolds a generator that builds, registers
  and passes its own validation: header and source in the designed shape (colonies on a lattice,
  a home and pond each, the kit and crop guarantee), the registry entry, the SConscript line, the
  next unused legacy id and its translation keys with English placeholders. Its golden rows come
  from `MapGeneratorGoldenTest <profile> --update`.
- The cppunit suite under `test/` (`scons && ./TestsRunner`) covers the rest of the engine and
  must stay green alongside all of the above.

Generators validate their own construction results rather than trusting the geometry to always
succeed: a moat must connect to land at both bridge ends, jagged outlines must leave legal
settlement footprints, and Fjord's core must keep every player peninsula connected (outside
lake-connected mode, where that's expected not to hold). Difficult small or crowded combinations
can still fail outright, but do so with a reproducible stage diagnostic, and are discarded by
`GenerationService`'s candidate sampling rather than surfaced to a player.

## Observing generator internals

`GenerationContext::telemetry` collects typed generator/helper observations when explicitly
enabled; `GenerationService` returns them in `GenerationResult` on success and failure.
It defaults off so ordinary generation and validation probes avoid collection overhead.
This is separate from `StartQuality` and final-map analysis. See [TELEMETRY.md](TELEMETRY.md)
for stable keys, bounded retention, failure reports and bulk analysis.

## Fractal maps

See [Fractal maps and recursive geometry](FRACTAL_MAPS.md) for the two generators,
coordinate units, home economies, stopping and crossing policies, and worked toolkit examples.

### Reusable site validation

The recursive maps also extend existing toolkit operations: `Drawing::fillRectangle`,
`BalancedStarts::selectSeparatedSites`, the explicit swimming overload in `Grid`,
`Resources::resourceFrontages`, `Growth::cropSpreadEnvelope`, and the complete building
arrangement check in `Room`. See [interfaces and reuse examples](FRACTAL_MAPS.md#reusable-operations-on-existing-toolkit-modules).
These operations carry no fractal economy settings and make no RNG draws. Existing
call sites retain their previous behavior; generators opt in to the new operations.
## Lava shield

See [the design and tuning notes](LAVA_SHIELD.md). Its primary flows use independent
angular interval weights and `downhillPath`; optional forks borrow collision checks
from Coral. `stampFarmPlot` reserves dry towns, `reserveSandRoute` protects their
crater approaches, and `chooseScoredSettlements` ranks fully furnished proposals.
`seedForPatchCapacity` is a bounded Planting retry operation: when a normal
starter patch finds too little usable area, it ranks nearby legal seeds by
eligible frontage, then caller-supplied preference such as fertility. It neither
changes the patch-growth predicate nor spends work on already sufficient fields.
The resource and terrain rules of other generators are unchanged.
