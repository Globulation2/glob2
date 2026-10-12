# Generator shared toolkit

## On this page

- [Grid](#grid)
- [Topology](#topology)
- [Geometry](#geometry)
- [Drawing](#drawing)
- [Wedge](#wedge)
- [Sketch](#sketch)
- [LatticeNoise](#latticenoise)
- [RecursiveGeometry](#recursivegeometry)
- [HierarchicalCrossings](#hierarchicalcrossings)
- [HeightMap, Noise](#heightmap-noise)
- [Planting](#planting)
- [Resources](#resources)
- [Roads](#roads)
- [Settlements](#settlements)
- [Walls](#walls)
- [Tessellation](#tessellation)
- [GraphMaze](#graphmaze)
- [ClearingLandscape](#clearinglandscape)
- [ScoredSettlements](#scoredsettlements)
- [Territories](#territories)
- [Farmland](#farmland)
- [Towers](#towers)
- [Homes](#homes)
- [WalkBandStarts](#walkbandstarts)
- [DesignCache](#designcache)
- [Routes](#routes)
- [Orbits](#orbits)
- [Morphology](#morphology)
- [Growth](#growth)
- [Contact](#contact)
- [Points](#points)
- [Patterns](#patterns)
- [Solve](#solve)
- [Rivers](#rivers)
- [Channels](#channels)
- [Room](#room)
- [Biomes](#biomes)
- [WorldAtlas](#worldatlas)
- [Raster](#raster)
- [Landmass](#landmass)
- [BalancedStarts](#balancedstarts)
- [Pipeline](#pipeline)
- [Terrain](#terrain)
- [StartQuality](#startquality)
- [GenerationContext](#generationcontext)
- [legacy/Regions, legacy/Distances, legacy/StartingPositions](#legacyregions-legacydistances-legacystartingpositions)

## Grid

`Torus` wrap and offset arithmetic; `floodFrom`/`stepsFrom`, the one eight-connected breadth-first flood (with a step limit and the visit order for callers that need it); `walkableTiles` and `groundUnitTiles` passability masks; `unitTilesByTeam` and `firstColonyCutOff`; `Axes`, the map's axes turned so that u runs along its longer side and v across it, the frame of a belt that wraps the map the long way (Ring world, Braided river); `reachFrom`, the same flood for one that is small beside the map (a few thousand tiles on a map of hundreds of thousands): the same tiles in the same order with their steps, at the cost of the tiles reached rather than of the map, which `floodFrom` pays on every call to clear its step field and scan its source mask

## Topology

`connectedRegions` component labelling with explicit wrap and neighbour policy; `labelComponents` (assign component owners and report the first mixed-label component, keeping unlabelled connecting ground); `regionAdjacency` and `graphDistances` over sparse labels; `DisjointSets`, union-find for Kruskal-style networks (Stone highlands' passes, `carveNearTree`)

## Geometry

`kPi`; `ShapeTransform` (invertible stretch and rotation); `Stretch`, which places a layout designed in a circle on a map's shorter side onto a rectangular map as an ellipse touching all four sides (`toFill`, `apply`, `undo`, `heading`), exactly the identity on a square map; `RadialShape`, a seeded rough outline with a per-angle `radiusAt()`; `stampShape` and `stampRoughDisc` into a label grid; `AxisFrame`, a frame along a heading from an origin (`at(along, across)` and `project`), in which a colony's trail, gates or towers are designed once; `Teardrop`, a blunt-headed tapering outline (two half-ellipses sharing the widest cross-section) with `halfWidthAt`, `contains`, `reach` under a stretch and `fitting` a packed radius

## Drawing

Drawing on the torus: `strokePath`, a thick path of points each with its own half width, rasterized through the wrap; `tracePath`, a path traced one tile thick with no gaps (a sand road); `bezierPath`, a quadratic curve as such a path; `polarPoint`; `forEachTileInShape` and `fillShape`, a `RadialShape` filled at any centre and turned to any heading within its bounding box, optionally stretched into an oval; `stretchPath`, a designed path placed on the map by a `Stretch` with its half widths kept in tiles; `bentPath`, a tapered path leaving a point along a heading and bowing sideways; `wanderingPath` and `carveCorridor`, a path (and its stroke) that wanders from one point to another the short way round the torus, leaving and arriving exactly where asked, its width swelling and narrowing (a tunnel, a lane, a trail that doesn't look ruled); `pathClearance` and `pathBounds`, the water between two stroked paths (optionally ignoring a branch's root) and a cheap bounding circle; `growBranches`, a tree grown level by level by forking in two at every tip to a `ForkStyle`, every branch offered to a caller's `accept` before it is kept and a refused one retried once at half length; `arcPath`, a circular arc as a stroke; `zigzagPath`, a switchback trail in an `AxisFrame` with each leg's straight run returned apart from its turns; `ringWithGates`, every tile of a ring visited with the gate it lies in (a wall with ramps, a moat with bridges); exact fixed-point geometry for designs that must rasterize the same everywhere: `SubtilePoint` (16 units a tile), `sealedSegmentTiles` and `traceSealedPath` (a line no unit can cross even diagonally, at any slant), `forEachTileInPolygon` and `fillPolygon` (tile centres inside a polygon, shared edges split exactly, through the wrap); `traceSealedLap`, a sealed line right round the torus along one axis (a wall of bluffs at the back of a belt); `forEachTileInTeardrop` and `fillTeardrop`, a `Teardrop` the same way with each tile's place along and across its axis; `traceRay`, such a line traced along a heading until a stop (a lane from a plot to the shore); `downhillPath`, a radially monotone walk with correlated angular drift, bounded heading and tapered width (lava/root/drainage flows); `splinePath`, a Catmull-Rom curve through waypoints for a stream, a gorge or a wash

## Wedge

`WedgeFrame`: the map as one equal wedge per colony round the centre, so a feature designed once in a wedge's frame is stamped into every wedge alike; given a `Stretch` it measures every cell in the round design frame, so the same design fills a rectangular map; `Blob`, a stretched, turned rough disc in that frame; `WedgeField`, periodic noise sampled in the wedge frame so a layer planted by it comes out the same in every wedge

## Sketch

`TerrainSketch`, the map's vertex terrain designed in memory; `layBeaches`, the order-independent beach pass; `raiseIslands`; `sprinkleSand`, decorative sand patches on inland grass following a caller's noise, kept a strip of grass away from every beach; `writeVertices`; `pureTiles`, the tiles whose four corners all hold one terrain (the sketch as the game will draw it; also available directly from a finished map), and `tileCorners`, the reverse; `growWater`, one body of water grown to an exact tile count by a caller's key (Stone highlands' ponds, Amphitheatre's bays); `keepRoadInland` and `roadTiles`, a sand road kept off every beach and the tiles its vertices spoil

## LatticeNoise

`PeriodicNoise`, value noise that tiles the torus exactly and samples anywhere, with `periodicNoise`/`fractalNoise` (integer fields per tile, octaves) and `torusNoise` (four octaves in [-1, 1]) sampled from it, plus `percentile` and `noisyShare` (the given share of a region where a noise field is highest, as a mask: cover in patches rather than speckle)

## RecursiveGeometry

Integer recursive halves/thirds, retained region hierarchy and stop reasons; uniformly spaced rectangular Hilbert paths

## HierarchicalCrossings

Required connectivity, marginal travel-benefit shortcuts, seeded ties and explicit budget shortfalls

## HeightMap, Noise

Perlin noise faded across the wrap, and the stamped height fields the height-field generators shape

## Planting

The deposits a generator places by hand: `clearGround`, `growPatch`, `seedNear`, `plantPatchNear` (bounded seed search plus a counted compact patch), `plantKit` (a home's wheat, wood and stone from three seeds, or no stone for a home already walled in it), `KitFrame` (a home's origin and facing, so one kit design lands the same way round every home), `swarmSurroundings`, `clearAroundSwarms`, `seedAlgae` (any water or a shallows band; optionally only its best-growing share, shared out equally between the colonies' wedges), `algaeGrowthChance` (each water tile's chance of passing the engine's algae growth test, which needs water within 15 tiles and solid sand within 30 at a reflected offset), `stockIslands`, `plantFields` (the preferred tiles dealt into wheat and wood patches by an unrelated split key), `scatterClumps` (clumps dropped on random eligible tiles of a region), `plantRound` and `plantOrchard` (a clump, or groves of the three fruits, at the same point round a circle at every colony's angle), `plantCover` (one deposit over every allowed tile of a region: a forest, a wheat plain, undergrowth); The deposits a generator places by hand: `clearGround`, `growPatch`, `growPatchesNear` (spend a resource budget across disconnected eligible pockets, returning tile and patch counts), `capResourceStock` (cap existing deposits without refilling or RNG draws; caller still controls growth), `seedNear`, `plantKit` (a home's wheat, wood and stone from three seeds, or no stone for a home already walled in it), `KitFrame` (a home's origin and facing, so one kit design lands the same way round every home), `swarmSurroundings`, `clearAroundSwarms`, `seedAlgae` (any water or a shallows band; optionally only its best-growing share, shared out equally between the colonies' wedges), `algaeGrowthChance` (each water tile's chance of passing the engine's algae growth test, which needs water within 15 tiles and solid sand within 30 at a reflected offset), `stockIslands`, `plantFields` (the preferred tiles dealt into wheat and wood patches by an unrelated split key), `scatterClumps` (clumps dropped on random eligible tiles of a region), `plantRound` and `plantOrchard` (a clump, or groves of the three fruits, at the same point round a circle at every colony's angle), `plantCover` (one deposit over every allowed tile of a region: a forest, a wheat plain, undergrowth); `clearDeposits`, the deposits on ground a design promised open (a ford's landings) cleared, a designed wall kept; The deposits a generator places by hand: `clearGround`, `growPatch`, `seedNear`, `plantKit` (a home's wheat, wood and stone from three seeds, or no stone for a home already walled in it), `KitFrame` (a home's origin and facing, so one kit design lands the same way round every home), `swarmSurroundings`, `clearAroundSwarms`, `seedAlgae` (any water or a shallows band; optionally only its best-growing share, shared out equally between the colonies' wedges), `algaeGrowthChance` (each water tile's chance of passing the engine's algae growth test, which needs water within 15 tiles and solid sand within 30 at a reflected offset), `stockIslands`, `plantFields` (the preferred tiles dealt into wheat and wood patches by an unrelated split key), `scatterClumps` (clumps dropped on random eligible tiles of a region), `plantRound` and `plantOrchard` (a clump, or groves of the three fruits, at the same point round a circle at every colony's angle), `plantCover` (one deposit over every allowed tile of a region: a forest, a wheat plain, undergrowth), `plantCoverShare` (cover over the top share of chosen tiles by a noise level, in patches; Canals' woodlots, Plantations' crop bands); The deposits a generator places by hand: `clearGround`, `growPatch`, `seedNear`, `seedForPatchCapacity` (on-demand local eligible-frontage ranking for a retry), `plantKit` (a home's wheat, wood and stone from three seeds, or no stone for a home already walled in it), `KitFrame` (a home's origin and facing, so one kit design lands the same way round every home), `swarmSurroundings`, `clearAroundSwarms`, `seedAlgae` (any water or a shallows band; optionally only its best-growing share, shared out equally between the colonies' wedges), `algaeGrowthChance` (each water tile's chance of passing the engine's algae growth test, which needs water within 15 tiles and solid sand within 30 at a reflected offset), `stockIslands`, `plantFields` (the preferred tiles dealt into wheat and wood patches by an unrelated split key), `scatterClumps` (clumps dropped on random eligible tiles of a region), `plantRound` and `plantOrchard` (a clump, or groves of the three fruits, at the same point round a circle at every colony's angle), `plantCover` (one deposit over every allowed tile of a region: a forest, a wheat plain, undergrowth)

## Resources

The ambient layer and its fairness guards: `scaledCount`/`scaledShare`, `placeResourceClump` (optional per-tile placement mask), `setScaledResource`, `scatterResources`, `guaranteeStartingResources` (optional constrained crop rescue), `openCrampedStarts`

## Roads

`cheapestRoute` and `openRoad`: the walk that crosses the fewest deposits, with only those cleared; `cheapestWalk`, the same search by any step cost (Symmetric arena's causeway routes); `openColonyRoutes`, the backstop that opens the cheapest way from colony 0's doorstep to any colony it cannot walk to under a `StepCosts` model, clearing deposits and fording water with sand (Everglades), optionally a lane of any radius wide (`clearRoute`) and never through a protected mask (Continents' passes through scree); `connectColonies`, the cheapest walk from colony 0 to every colony it cannot reach opened through deposits alone, never water or a designed wall (Watershed, Braided river); `reserveSandRoute`, a cardinal or eight-neighbour sand approach of explicit tile width with optional positive routing costs, reserved before furnishing without changing water corners or protected terrain; `openTrail`, a colony's trail to a shared objective that bends with a noise field, prefers the gaps between deposits and never clears a protected kit (Central Quarry, Hidden Oasis)

## Settlements

`placeSettlement`: whole-footprint home mask, nearest legal anchor, exact worker count, per-colony diagnostics; `placeTower`, a completed defence tower at the allowed footprint nearest a designed point, with optional full-width approach coverage and initial stone reserves; `placeStartingBuilding` uses the same footprint search and selectively supplies a completed building (a starter inn can receive wheat without giving away fruit); `placeBuilding`, a completed building of any type and level at the allowed footprint nearest a designed point (a granted swimming pool or inn; Plantations); `countBuildings`, a validator's count of a colony's completed buildings of a type

## Walls

Walls a design builds and the checks that prove they hold: `seaVertices` and `seaMargin` (the land a swimmer can stand on), `sealCoasts` (stone on every grass tile touching that margin, so a coast is sealed; City states, Carousel, Switchbacks), `islandSeaMargin` and `sealedIslandStone` (the margin and the stone of walled land in the sea with ponds and sand roads, once for Carousel and Switchbacks), `checkGatePartition` (seal explicit tile plugs, detect diagonal or wrapped boundary leaks, and prove each connected plug touches exactly its two labelled regions); `labelBorders` (a wall between labelled regions no unit can cross even diagonally, one to three tiles thick, or with doors a design leaves open; Stone highlands' ridges, Amphitheatre's borders, Carousel's walls), `designedStone` (a designed band's stone and any gaps the beach pass left), `reachesWithShut`, `pieceLeak` and `seaEntry` (with the doors shut, does any part reach another, or the sea reach inside), `towerReach` (how close a tower on some ground comes to shooting at a target: towers scan square rings with no line of sight), `walkSpread` (every colony's walk to its own target, and how uneven they are) and `firstRegionLeak` (reachable ground where two labelled regions meet that a design says must not; Maze's walls); `colonyLeak`, the first two colonies that can walk to each other with a set of tiles shut (a map's causeways or gates), crops and buildings counted as the ground they will leave; `wallStanding` (a validator's proof that every designed wall tile holds stone and every gate is open; The Glacis, Caravanserai)

## Tessellation

Polygon tilings of the torus with no terrain attached: `squareTessellation` and `hexTessellation` (cells, the corners they share and edges as identities, exact across the wrap even two cells wide), `edgeEnds`, `centreAcross`, `outline`, `transform` (the lattice's translations and mirrors), `labelTiles` (every tile's cell), `cellCrossing` (a path through a particular shared edge, outside reserved centre discs, preserving distinct toroidal parallel crossings), `shortestEdgeSteps` and `centreClearance`; `warpLimit` and `warpCorners`, corners moved at random into irregular polygons that still tile, never closing the gap between walls that share no corner below a minimum (Maze); `rasterizeBoundaries` (sealed selected edges with toroidal thickness) and `relaxWarpOutside` (bounded deterministic contraction away from an arbitrary protected tile mask)

## GraphMaze

A maze carved through any `CellGraph` (a tessellation's with `cellGraph(tiling)`, or scattered sites' with `cellGraph(torus, sites, siteNeighbours)`): `pocketsFit` and `spreadPockets` (cul-de-sac cells spread as far apart as still leaves a maze), `carveSpanningTree` (recursive backtracker), `carveNearTree` (Kruskal over jittered distances: the network a traveller would build, each cell to the next one over), `openPocketDoors`, `openLoops`, `deadEnds` (Maze, Drumlin field); `closedEdges` (candidate crossings excluding protected endpoints) and `edgeDetours` (existing route lengths for shortcut scoring, caching floods by endpoint); `farthestCells` (farthest-point spreading over eligible cells with no maze constraint) and `claimNeighbourCells`, neighbouring cells dealt to seed cells round by round with every seed ending on the same count (Plantations' granted islands)

## ClearingLandscape

Sand-contained round homes on a dealt lattice, optional home pools and distant lakes; shared by Old Growth and Locust without changing Old Growth's seed streams or layout arithmetic for passing layouts. Locust may opt into a vacancy lattice only after the ordinary room check fails

## ScoredSettlements

`chooseScoredSettlements`: compare a bounded list of complete settlement proposals in fresh worlds, score actual workers/resources/room with `StartQuality`, enforce caller viability checks, and restore all trial RNG effects; the returned winning sites are materialized by the same builder

## Territories

Ground shared out where wedges cannot be fair (a square map's corners, the wrap, odd colony counts): `growTerritories`, equal-area regions grown together from seed tiles, the smallest always taking the next cheapest tile, connected and deterministic, or shared by value when each claimant's ground has a worth per tile; `balancedTerritories`, a power diagram from one site per claimant with the weights tuned until the areas are equal, so every border is a straight line (Amphitheatre); `smoothLabels`, a majority filter over any radius that trims spurs or, at a radius of 3 or 4, straightens borders into curves; `separateTerritories`, a gap opened between neighbouring territories; `fillToNearest`, the reverse: unclaimed ground near labelled regions given to the nearest, so water between pieces becomes land; `strandedGround`, land a flood cannot reach; `growFarLake`, a lake of an exact size at the roomy far end of a region; `growLakeBeside`, one on either flank of a site, kept wholly to its side of the line from the way in; `siteAtDepth`, the roomiest site a given number of steps from a region's way in

## Farmland

`stampContainedPlot`, arbitrary grass-corner sets with sealed sand margins; `plantContainedPlot`, fertility-ranked deposits with explicit count and renewable constraints; `containedPlotsMismatch`, final eight-neighbour grass containment validation (see [Savannah](savannah.md)). Farms laid like real ones, in long rows of crops with water between: `bestFarmRows`, the crop and water widths that yield most for rows at an angle (10 and 8 tiles along an axis, 12 and 9 on the diagonal, a rough line through the exact regrowth sums `tools/farm_row_fit.py` computes); `farmYield`, the yield per tile at that angle from the same fit (0.149 along an axis, 0.113 on the diagonal); `layFarm`, rows over a region with a rim of land kept round them and, optionally, a 10x4 building plot of grass ringed with sand at its most inland point and sand bridges clean across the farm every so many tiles along the rows, water and crop rows alike, each half switched by `FarmBridges` and offered to players by every farm-row map as `water-crossings` and `crop-crossings` (`waterCrossingsControl`, `cropCrossingsControl`, both on), so workers cross the whole field on one road (a plot's grass always wins over a bridge), and a ring of sand closing the crop rows so wheat and wood never spread out of the farm; `plantFarm`, wheat along the water on every crop row and a small woodlot along one; `clearFarmPlots`; `farmReachable`, the share of a farm's crop land a colony can walk to; `growFarmFields`, farm fields grown into open water straight out of their own homes by equal yield for their row angles, held off all other land and each other, opened so no strip too narrow for its beaches survives (and only ground whose eroded core joins the home's core stays, since two cores that do not touch can overlap once grown back through a waist the coast walls then close), and joined to their homes by a broad neck. Walls round a farm are the map's business, not the farm's; `layContourFarm`, complete circular bands around central clearings, with inner/outer sand caps and radial crossings (`ContourFarmStyle`); `stampSealedOval` and `plantSealedGarden`, a holder's garden sealed by sand against a shore or a wall; `trimFieldsBeyondWater` and `frayFieldEdges`, which round off and fray the ruler-straight edges fields take along the growth probe's square contours; `removeCropSlivers`

## Towers

Tower sites chosen for the ground they cover over the walls, other colonies' (offence) or their own (defence), each weighted: `startingTowerRequest` (a request from a map's level and count controls), `chooseTowerSites` (best first, a colony at a time in turn, optionally every site directly against a wall, no stocked tower in range of another colony's tower or swarm, plus open 2x2 pads for players to build more), `settleStartingTowers` (the sequence every arena map runs: `dropBlockingSites` so no site closes a colony's walk to a goal, `evenTowerPlan` so every colony has as many sites as the fewest got, then `raiseTowers`), `towerFootprints` and `roomyGround` (no tower on a strip it could close); a tower in range of another colony's tower starts empty

## Homes

Homes built alike: `stampRoundHome`, `homeSwarmSite` and `plantHomeKit` (a round home facing out from the middle, its swarm towards its door, optional ponds kept clear of its edge, and a wheat and wood kit near the swarm with no stone, since these homes are walled in stone; `homeHasRoom` is the size floor; Carousel, Switchbacks; the axis is any heading, out from the middle on a ring), `regionHome` (a home in ground of any shape: the swarm the same walk in from the door as every other colony's, on the roomiest such tile, facing away from the door), and `furnishGround` (farmland in patches on a ground's fertile tiles, outcrops and groves; City states, Carousel, Switchbacks, Amphitheatre); Homes built alike: `TeardropHome` with `stampTeardropHome`, `teardropHomeSwarm` and `teardropHomeKit` (a home on a long hill: town head, sand collar, farm tail; Drumlin field); `topUpWheatNearby`, the wheat a crowded found start is short of, planted on clear ground and, failing that, in place of the country's own deposits

## WalkBandStarts

Starts for a natural map round one shared objective, fair by walk rather than by plot: `spreadInWalkBand` (sites spread by walking distance inside a narrow band of walking distance from the objective, with a floor on the straight-line distance, preferring watered ground whose 48-step yield lies between percentiles of a sample, then roomy ground, then any; the walk tried from a ladder of percentiles under a cap in steps) and `firstWalkTerritories` (each tile labelled with the site it walks to first). Central Quarry's isle and Hidden Oasis' gorge mouth

## DesignCache

`cachedDesign`, the last design built on a thread handed out again for the same request, its telemetry replayed and every stream wound on to where building it left that stream: a generation asks for its design two or three times (Karst towers, Central Quarry, Hidden Oasis)

## Routes

Routes between sites on the torus, the short way round: `midpointAcross`, `siteDistance`, `nearestPairs` (each site's nearest others as pairs, each once), `waypointsAlong` (stepping stones along the straight way between two points, with gaps kept at both ends), `headingAcross` and `quarterTurn` (a heading as a quarter-turn facing); Caravanserai's caravanserais and route oases

## Orbits

Fairness by symmetry groups rather than wedges: `Symmetry` (signed permutation matrices on doubled centred coordinates plus a whole-tile translation, mapping tiles and corners exactly), `pointSymmetry` (the half turn, quarter turns, mirrors and all eight square symmetries; Symmetric arena), `translationSymmetry` (the roomiest lattice of 2, 4, 8... colonies on the torus, square, staggered or sheared, with no centre and served as well on a rectangular map), `jitterSites` (bounded integer perturbations preserving caller-specified minimum spacing), `latticeSites` (colonies on that lattice, or evenly staggered rows when the count has no exact group), `stampOrbits`, `orbitSum`, `orbitNoise` and `topShare` (features and fields every symmetry leaves unchanged), `equaliseDeposits` (the gameplay RNG's amounts made equal across each orbit) and `orbitMismatch` (the finished world checked exactly symmetric, colonies permuted one to one); Fairness by symmetry groups rather than wedges: `Symmetry` (signed permutation matrices on doubled centred coordinates plus a whole-tile translation, mapping tiles and corners exactly), `pointSymmetry` (the half turn, quarter turns, mirrors and all eight square symmetries; Symmetric arena), `translationSymmetry` (the roomiest lattice of 2, 4, 8... colonies on the torus, square, staggered or sheared, with no centre and served as well on a rectangular map), `latticeSites` (colonies on that lattice, or evenly staggered rows when the count has no exact group), opt-in `roomyLatticeSites` (leave surplus composite-grid sites empty when that strictly improves spacing), `stampOrbits`, `orbitSum`, `orbitNoise` and `topShare` (features and fields every symmetry leaves unchanged), `equaliseDeposits` (the gameplay RNG's amounts made equal across each orbit) and `orbitMismatch` (the finished world checked exactly symmetric, colonies permuted one to one); `turnStencilVertex`, `turnStencilTile` and `turnStencilPoint` (a home designed once in its own frame and stamped at every colony by quarter turns, corners and tiles each turned by their own rule so every copy covers exactly the same tiles; The Glacis' forts, Allotments' villages, Caravanserai's home oases)

## Morphology

Mask arithmetic on the torus in integers: `dilate`, `erode`, `openMask`, `closeMask` (squares), `distanceSquaredTo` (exact Euclidean) and `dilateRound`, `clearance` (steps to the nearest tile outside a mask), `dropSmallRegions` (specks, or pockets on the inverted mask), `widestWalkClearance` and `narrowestPassage` (the width of a walk's narrowest point, a validator's check that a tunnel or street still takes a column of units), `slivers` (Stone highlands' pockets, Farmland's field opening, `roomyGround`, `separateTerritories`) and `bridgeDiagonals` (a line of tiles joined wherever it touched only at a corner, so a river or moat core holds against eight-connected movement); Mask arithmetic on the torus in integers: `dilate`, `erode`, `openMask`, `closeMask` (squares), `distanceSquaredTo` (exact Euclidean) and `dilateRound`, `clearance` (steps to the nearest tile outside a mask), `dropSmallRegions` (specks, or pockets on the inverted mask), `widestWalkClearance` and `narrowestPassage` (the width of a walk's narrowest point, a validator's check that a tunnel or street still takes a column of units), `roomiestTile` (the tile of a region with the most clearance, nearest a point on a tie) and `slivers` (Stone highlands' pockets, Farmland's field opening, `roomyGround`, `separateTerritories`)

## Growth

Where wheat and wood grow back, known on the sketch: `cropGrowthField` (the exact `Fertility::Field` of a `TerrainSketch`, not gated on deposits), `wateredShare`, `wetTiles` (a region promised dry checks 0), `dryZone` (where water would water a region: `kCropProbeReach` on each axis), `drainWithin`, `waterSteps` (every tile's steps to the nearest pure water), `meanFertilityAround` and `meanFertilityField` (a site's mean crop growth chance over a square, and the same for every tile at once from running sums), `digPond` (a pond of an exact size grown beside a site that has no water, kept clear of its swarm and on its own ground) and `waterDrySite` (ponds dug by `digPond` until a site's mean crop growth reaches a floor, returning the corners each attempt dug; Continents' oases, Central Quarry's dry starts); finished-map `cropSeedsIn` checks protected masks after resource repairs, and `cropSpreadEnvelope` conservatively floods wheat/wood through toroidal eight-connected grass to test long-term containment without simulating growth speed

## Contact

Distances by caller-defined comparison costs (terrain walking/swimming permissions apply; speed multipliers do not rescale these geometric or clearing objectives): `StepCosts` (open, clearable, eternal, water, building; `walking`, `chopping`, `swimming`), `stepCost`, `costsFrom`, `contactMatrix` (every colony's cost to every other), `costsToTarget`, `costSpread`, `unevenCosts` (a validator's message) and `equalCostSites` (one site per colony at the same cost from home, on its own side: groves an equal chop into a forest)

## Points

Irregular sites and cells: `spreadPoints` (dart throwing with a minimum spacing), `nearestSiteLabels` (warped Voronoi cells in sixteenths of a tile), `relaxPoints` (Lloyd relaxation on the torus) and `siteNeighbours` (which cells touch); Stone highlands' basins; Stone highlands' basins. Under a `Grain` (a whole-step direction and a stretch along it, with an integer `distance2`): `spreadPoints` with fixed sites kept clear, `nearestSiteDistances`, `nearestSiteLabels` (cells elongated with the grain) and `packLandforms` (a radius per site so every pair keeps a gap, fixed radii kept, small sites dropped); Stone highlands' basins. On uneven ground: `farthestSites` (sites spread by walking distance over a passability mask, the best of several first sites kept, so a bay between two sites counts as the walk round it) `recentreSites` (each site walked to the candidate nearest the middle of its own territory) and `closestWalk` (the fewest walking steps between any two of a set of sites); `nearestTwoSites`, an exact unwarped owner/runner-up query with squared toroidal distances, stable index ties and missing-site sentinels; `spreadRankedSites`, weighted maximin selection with a hard toroidal spacing floor and explicit partial failure

## Patterns

Structure with a grain: `turingPattern` (labyrinths and spots grown from noise by activation and inhibition, about a wavelength across, optionally stretched), `stripePhase`, `stripeSpacing`, `stripeHeading`, `stripeNormal` and `stripeDistance` (parallel stripes that wrap both seams exactly at any whole-number slant, optionally warped), `upwindSteps` (the shadow a mask casts downwind) and `traceStreamline` (a curve following a field of headings); `runsAndGaps`, a broken line of runs and gaps (a moraine's hummocks, cover with doors in it)

## Solve

Bounded annealing over caller-owned state, named `Objective` terms, per-seed `Brief` and search telemetry. Construct hard requirements and validate the finished world separately; see [search integration](constraint-search.md).

## Rivers

Periodic river construction, scored placement, bank candidates, explicit partial connectivity via `FordConnections`, and spaced/rasterized crossings. Composes with `Channels`, `Sketch`, and `Solve`.

## Channels

Water sized for its purpose: the beach arithmetic once (`kChannelSpoiledTiles`, `bankToBank`: a channel w corners wide spoils w + 3 tiles and puts banks w + 4 apart), `widestChannelTowersCross` and `narrowestChannelTowersMiss` by tower level, `beachTiles` (the rim towers stand against to cover a canal, on a sketch or a map), `bridgeAcross` (a sand bridge across water), `straitsBetweenCells` (a strait of an exact corner width along every border between labelled cells, so cells become islands; Plantations) and `crossingsPerLabel` (every colony's number of ways across); channels drawn as centre lines with a radius per point: `SandFord` with `stampFord`, `fordAlong`, `fordFault` and `fordLandingWalkable` (a ford laid across a channel and checked on the rasterized water: it interrupts open water, is dry across and lands on walkable ground), `channelCoreFault` (a channel keeps a 4-connected core of water except at its fords, so nothing steps over it) and `channelCrossings` (the stretches two labelled regions face each other across, the edges of a crossing graph; Watershed's fords, Braided river's riffles)

## Room

Building room as a design decision: `buildableTiles`, `buildAnchors` (every 4x4 footprint's top-left, across the wrap), `buildSites` (footprints in a region) and `growUntilSites` (a chamber grown until it holds exactly what it promised)

## Biomes

Kinds of land as data: `BiomeKit` (ponds, stone ring, farmland, wood share, outcrops, groves, cover of wood or stone, a dry reserve of finite crops where nothing regrows, orchard island) with `fertilePlain`, `stoneFortress`, `orchardIsland` and `forest`, and the kinds of real land `farmland`, `woodland`, `savanna`, `barrens` and `highland` (stone scree at 40%, below the percolation threshold, so a range is slow to cross but not a wall); `biomeWorth` (an estimate from the start scorer's weights to share ground by, to be tuned with the fairness tournament); `scaledBiome` (a kit's ambient layers scaled to the map's resource amounts); `sketchBiome` (its ponds, island and ring) and `furnishBiome` (its deposits)

## WorldAtlas

Real geography compiled in (see [the world atlas](world-atlas.md)): `LandClass` (ocean, lake, plain, forest, steppe, desert, mountain, tundra, ice) and a river flag per cell, `kAtlasRegions` (the six continents, run-length encoded by `tools/world_atlas.py` from Natural Earth and the Köppen-Geiger climate map), `atlasRegion` and `decodeAtlas`

## Raster

A picture laid onto the torus: `fitRaster` (a source raster fitted inside the map less a margin, centred, its aspect kept, turned a quarter turn when that fits a rectangle larger, all in integer fractions), `resampleRaster` with `resampleMajority` (each tile's most common class, sea on a tie) and `resampleAny` (a flag any covered cell carries: a river survives shrinking)

## Landmass

Rasterized coasts made playable: `cleanLandmass` (slivers under three tiles wide, specks of sea and islets nothing fits on, by `CoastCleaning` thresholds), `largestRegion` (the mainland) and `inheritLabels` (filled ground takes the classes round it)

## BalancedStarts

`chooseBalancedStarts`: boot tiles whose walks to Food and Wood material sources are as nearly equal as the finished map allows

## Pipeline

The stages round the others: `dealStarts` (the design's start sites dealt to the colonies at random, so a team number never gets the same ground map after map), `designFailure` (the registry's request check for a designed generator: the design's own failure), `settleColonies` and `settleRoundColonies` (round homes on their own grass, `homeGrassMask`), `homePondMissing` (a validator's check that every home kept its pond), `secureStartingCrops` (clear round the swarms, guarantee the crops, clear again), `reopenCrampedStarts` (at non-default amounts) and `openStartsBuriedByResources` (at any amount), `designMismatch`, `walkFromFirstColony`, `cropsBesideReach` (which crops a flood from a colony stands beside) and `coloniesApart` (the island map's opposite promise: no colony can walk to another) for validators, `startingAccessFailure` (read-only worker access to typed `MaterialAccessRule` supplies, including secondary yields from custom deposits, and nearby 4×4 building origins) and `startingFloorFailure` (the wheat, wood and building-room floor every legacy-core landscape validates), `ResourceAmounts`

## Terrain

The height-field pipeline as stages: `heightFieldTiling`, `classifyHeightField`, `paintHeightFieldTerrain`, `paintHeightFieldResources`, `chooseHeightFieldStarts`, `plantHeightFieldGroves`, composed by `generateHeightField`

## StartQuality

`scoreStarts`, the finished map's colony measurements; `FairnessModel.h` scores them and the service ranks candidates by the resulting fairness

## GenerationContext

Named `std::mt19937` streams, `bounded` draws and `shuffle`

## legacy/Regions, legacy/Distances, legacy/StartingPositions

The older area-grid toolkit: point dispersion (`splitUpPoints`, `splitUpArea`, `divideUpArea`), the legacy distance encoding, `divideUpPlayerLands` and the boot-tile placers `placeStarts`/`placeArchipelagoStarts`. Concrete islands, Isles, Contested commons and the height-field generators still build on it; nothing new should

See [lifecycle](map-generator-framework.md), [resource placement](resources-and-starts.md) and [adding a generator](adding-a-generator.md).

Related: [map generators](README.md).
