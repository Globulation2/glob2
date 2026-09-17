// SPDX-License-Identifier: GPL-3.0-or-later
#include "BreachableHighlandsGenerator.h"
#include "Centrepieces.h"
#include "Drawing.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GraphMaze.h"
#include "Growth.h"
#include "Grid.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Room.h"
#include "Sketch.h"
#include "Tessellation.h"
#include "Walls.h"
#include <algorithm>
#include <string>
#include <vector>
using namespace MapGeneration;

// Broad valleys enclosed by permanent stone ridges. Open passes connect the whole map;
// wooded saddles offer additional connections between expansion valleys. Clearing a saddle
// changes the front, while every home keeps one ordinary exit and renewable local crops.
// Water is confined to contained farm basins, away from the saddles: cleared wood stays cut.
// The warped cells and passage graph vary by seed. Starts are dispersed and randomly dealt,
// not exactly symmetric; the lobby's start score is a heuristic, not a fairness guarantee.
namespace
{
// A basin uses a 12-tile crop radius inside a sand ring at 14. The remaining valley is
// construction ground. A minimum centre-to-edge clearance of 28 leaves a dry buffer between
// the largest pond (radius 6 plus roughness) and even an 11-tile ridge. The finished fertility
// check below, rather than this estimate, is authoritative for whether saddle wood can regrow.
constexpr int kFarmRadius = 12, kFarmRing = 14, kCentreClearance = 28;
// Home guarantees are added to ambient plots, including at 0% abundance. They are reserves,
// not measured yields. The room floor counts overlapping 4x4 anchors, not 32 separate buildings;
// generous valley aprons and game checks provide the practical expansion budget.
constexpr int kHomeWheat = 54, kHomeWood = 24, kMinimumRoom = 32;
constexpr double kRoadHalfWidth = 1.5;
// Sand blotches on each valley's open ground (maintainer review 2026-09-16, as for Hedgerow
// Country: "some terrain variety to break up the monotony"): two to four rough discs of radius 2
// to 4, clear of the farm ring and its beaches, kBlotchKeepOut tiles from any ridge, saddle or
// lane, and never on a home's swarm apron. Sand holds no crop and waters nothing, so a blotch
// cannot water a saddle or close a pass.
constexpr int kBlotchKeepOut = 3, kBlotchTries = 12;

struct Layout
{
	Torus t{1, 1};
	Tessellation cells;
	TerrainSketch terrain;
	std::vector<int> labels, homes, homeOf;
	std::vector<unsigned char> stone, wood, road, protectedTiles;
	std::vector<TileGate> gates;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const BreachableHighlandsOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	if (t.w < 128 || t.h < 128)
	{
		L.failure = "Breachable highlands needs both sides at least 128 tiles long.";
		return L;
	}
	L.cells = squareTessellation(t.w, t.h, o.valleySize);
	auto &g = L.cells;
	if (g.columns < 2 || g.rows < 2 || g.cellCount() < request.nbTeams + 2)
	{
		L.failure = "Leave at least two expansion valleys and two valleys along each axis; use "
					"a larger map, smaller valleys or fewer colonies.";
		return L;
	}
	// Choose separated home cells while keeping all non-home cells connected. Homes are leaves
	// of the initial graph: there is no transit traffic through a starting town. Only expansion
	// cells get saddle endpoints, so chopping cannot unexpectedly open the back of a home.
	// This spreads geometric positions, not travel distances or future strategic advantage.
	L.homes = spreadPockets(g, request.nbTeams);
	if (int(L.homes.size()) != request.nbTeams)
	{
		L.failure =
			"These valleys cannot fit separate home exits; use fewer colonies or a larger map.";
		return L;
	}
	const int dx = context.bounded("breachable-layout", g.columns);
	const int dy = context.bounded("breachable-layout", g.rows);
	for (int &home : L.homes)
		home = g.transform(home, dx, dy, false, false);
	dealStarts(context, L.homes);
	std::vector<unsigned char> isHome(g.cellCount(), 0), open(g.edges.size(), 0);
	for (int home : L.homes)
		isHome[home] = 1;
	// A spanning tree guarantees a route without any clearing. Its long detours make the
	// optional extra edges useful. The shared backtracker changes that tree with the seed;
	// independent streams keep resource sliders from changing the passage network.
	if (!carveSpanningTree(g, context, "breachable-passes", isHome, open))
	{
		L.failure = "The expansion valleys could not be connected.";
		return L;
	}
	openPocketDoors(g, context, "breachable-passes", L.homes, isHome, open);
	// Keep at least one closed expansion edge for a saddle, even on the smallest layout.
	const auto graph = cellGraph(g);
	std::vector<int> candidates = closedEdges(graph, open, isHome);
	if (candidates.empty())
	{
		L.failure =
			"No additional ridge connects expansion valleys; use fewer colonies or a larger map.";
		return L;
	}
	context.shuffle(candidates.begin(), candidates.end(), "breachable-saddles");
	const int extra =
		std::min(int(candidates.size()) - 1, (int(candidates.size()) * o.extraPasses + 50) / 100);
	for (int k = 0; k < extra; ++k)
		open[candidates[k]] = 1;
	// Prefer spare ridges whose endpoints have the longest existing detour. An arbitrary
	// spare edge can duplicate a nearby pass and make clearing uninteresting. This graph
	// distance is only a route-choice heuristic (not tile travel or clearing time); ties keep
	// the seeded shuffle above, and the final validator checks the actual raster crossing.
	const auto detour =
		edgeDetours(graph, open, std::vector<int>(candidates.begin() + extra, candidates.end()));
	std::stable_sort(candidates.begin() + extra, candidates.end(),
					 [&](int a, int b) { return detour[a] > detour[b]; });
	const int saddleCount = std::max(1, ((int(candidates.size()) - extra) * o.saddles + 50) / 100);
	std::vector<unsigned char> wooded(g.edges.size(), 0);
	for (int k = extra; k < extra + saddleCount; ++k)
	{
		wooded[candidates[k]] = 1;
		context.telemetry.measure("breachable.saddle.detour-edges", detour[candidates[k]],
								  k - extra);
	}
	context.telemetry.measure("breachable.valleys.actual", g.cellCount());
	context.telemetry.measure("breachable.saddles.candidates", candidates.size());
	context.telemetry.measure("breachable.saddles.actual", saddleCount);
	context.telemetry.measure("breachable.passes.extra", extra);
	context.telemetry.measure("breachable.ridges.depth", o.ridgeDepth);
	// Warp only as far as the shared polygon solver preserves centre clearance and separates
	// unrelated boundaries. This avoids tiny sliver valleys and collapsed passes on rectangles.
	// Edges retain identities: on a two-column torus the same pair of valleys can meet twice.
	const std::vector<unsigned char> walls(g.edges.size(), 1);
	warpCorners(g, warpLimit(g) * o.warp / 100, walls, 24, kCentreClearance, context,
				"breachable-warp");
	L.labels = labelTiles(g);
	if (L.labels.empty())
	{
		L.failure = "The valleys did not tile the map.";
		return L;
	}
	const int n = t.size();
	L.stone.assign(n, 0);
	L.wood.assign(n, 0);
	L.road.assign(n, 0);
	L.homeOf.assign(n, -1);
	std::vector<int> homeTeam(g.cellCount(), -1);
	for (int k = 0; k < request.nbTeams; ++k)
		homeTeam[L.homes[k]] = k;
	for (int i = 0; i < n; ++i)
		L.homeOf[i] = homeTeam[L.labels[i]];
	// Full ridges first; then replace short sections by open passes or solid wood. Drawing a
	// thick continuous stroke through shared corners prevents diagonal movement between cells.
	// Ridge depth also sets roughly how many rows of wood must be cleared at a saddle. This
	// is a spatial cost, not a promise of a fixed number of ticks or workers. Stone is eternal
	// and supplies quarry frontage; reducing ambient resources must not weaken these walls.
	for (int e = 0; e < int(g.edges.size()); ++e)
	{
		const auto ends = g.edgeEnds(e, g.edges[e].cells[0]);
		const ShapePoint a = tilePoint(ends.first), b = tilePoint(ends.second);
		strokePath(L.stone, t, {{a.x, a.y, o.ridgeDepth / 2.0}, {b.x, b.y, o.ridgeDepth / 2.0}});
	}
	for (int e = 0; e < int(g.edges.size()); ++e)
	{
		if (!open[e] && !wooded[e])
			continue;
		// The shared crossing follows this exact edge across the wrap and stops at
		// the farm ring, so neither its cut nor its approach enters the central basin.
		auto path = cellCrossing(g, e, kFarmRing, o.passWidth / 2.0);
		std::vector<unsigned char> crossing(n, 0);
		strokePath(crossing, t, path);
		// Retain the entire tile plug, including edge pixels, for validation. Sand approaches
		// stop short of wood, and only the plug changes material: clearing it later leaves a
		// grass passage that players can also build on. An open pass carries a sand lane.
		TileGate gate{{g.edges[e].cells[0], g.edges[e].cells[1]}, {}};
		for (int i = 0; i < n; ++i)
			if (crossing[i] && L.stone[i])
			{
				gate.tiles.push_back(i);
				L.stone[i] = 0;
				L.wood[i] = wooded[e];
			}
		if (gate.tiles.empty())
		{
			L.failure = "A pass missed its ridge.";
			return L;
		}
		L.gates.push_back(std::move(gate));
		for (auto &point : path)
			point.halfWidth = kRoadHalfWidth;
		strokePath(L.road, t, path);
	}
	// All water is well inland. A sand rim contains crops and joins the approach roads.
	// Each valley's pond is one of the shared centrepiece designs (maintainer review 2026-09-16:
	// "a few different designs and patterns for each square"); Round is the map's own rough pond.
	// Every home draws the same design, since a design's water sets how fast its crops regrow.
	// Designs with square corners are drawn a corner smaller, so none reaches further from the
	// centre than the rough round pond the saddle clearance was measured against.
	std::vector<unsigned char> water(n, 0), islet(n, 0);
	const RadialShape pond(o.pondSize, 0.15, context, "breachable-ponds");
	const auto drawDesign = [&]
	{
		const auto design =
			PondDesign(context.bounded("breachable-pond-designs", int(PondDesign::Count)));
		return pondDesignMinimumRadius(design) > o.pondSize - 1 ? PondDesign::Round : design;
	};
	const PondDesign homeDesign = drawDesign();
	const int homeTurn = int(context.bounded("breachable-pond-designs", 2));
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		const ShapePoint c = tilePoint(g.cells[cell].centre);
		const double turn = context.bounded("breachable-ponds", 360) * kPi / 180;
		PondDesign design = drawDesign();
		int quarter = int(context.bounded("breachable-pond-designs", 2));
		if (isHome[cell])
		{
			design = homeDesign;
			quarter = homeTurn;
		}
		const bool rounded = design == PondDesign::Round || design == PondDesign::Ring ||
							 design == PondDesign::Diamond;
		const int radius = rounded ? o.pondSize : o.pondSize - 1;
		if (design == PondDesign::Round)
			fillShape(water, t, c.x, c.y, pond, turn);
		else
			for (int dy = -radius; dy <= radius; ++dy)
				for (int dx = -radius; dx <= radius; ++dx)
					if (const char at = pondDesignAt(design, radius, quarter, dx, dy))
						(at == 'w' ? water : islet)[t.at(int(c.x) + dx, int(c.y) + dy)] = 1;
		strokePath(L.road, t, arcPath(c.x, c.y, kFarmRing, 0, 2 * kPi, 1.2, 1));
		// A grass access lane survives only until crops spread into it. Join both banks to
		// the outer ring with sand instead: the pond and these spokes divide the fertile
		// annulus into separate wheat/wood plots, so faster wood growth cannot circle the
		// shore into the food plot. Water takes precedence below, preserving the pond.
		// A one-corner half-width also makes a robust walkable harvesting lane after the
		// four-corner terrain conversion, without spending more of the small farm on sand.
		strokePath(L.road, t, {{c.x - kFarmRing, c.y, 1.0}, {c.x + kFarmRing, c.y, 1.0}});
		// Wood gets the southwest quarter, within reach of the western town. The southeast
		// quarter becomes a second wheat plot. Wheat has the engine's extra one-in-three
		// growth gate, whereas wood does not: equal farm areas proved food-poor in games.
		// Three quarters for wheat is a throughput heuristic, not a promised carrying capacity.
		strokePath(L.road, t, {{c.x, c.y, 1.0}, {c.x, c.y + kFarmRing, 1.0}});
	}
	// Undermap corners beside a deposit must remain grass; otherwise a sand approach could
	// erode a ridge or open a diagonal bypass around its wood plug.
	L.protectedTiles.resize(n);
	for (int i = 0; i < n; ++i)
		L.protectedTiles[i] = L.stone[i] || L.wood[i];
	const auto protectedCorners = tileCorners(t, L.protectedTiles);
	L.terrain.assign(n, GRASS);
	for (int i = 0; i < n; ++i)
		L.terrain[i] = water[i] ? WATER
					   : (L.road[i] || islet[i]) && !protectedCorners[i] ? SAND
																		  : GRASS;
	{
		std::vector<unsigned char> busy(n, 0);
		for (int i = 0; i < n; ++i)
			busy[i] = L.protectedTiles[i] || L.road[i];
		busy = dilate(t, busy, kBlotchKeepOut);
		int blotches = 0;
		for (int cell = 0; cell < g.cellCount(); ++cell)
		{
			const int x = g.centreTileX(cell), y = g.centreTileY(cell), half = o.valleySize / 2;
			const int wanted = 2 + int(context.bounded("breachable-blotches", 3));
			for (int b = 0, tries = 0; b < wanted && tries < kBlotchTries; ++tries)
			{
				const int dx = int(context.bounded("breachable-blotches", 2 * half + 1)) - half;
				const int dy = int(context.bounded("breachable-blotches", 2 * half + 1)) - half;
				const int radius = 2 + int(context.bounded("breachable-blotches", 3));
				const double turn = context.bounded("breachable-blotches", 360) * kPi / 180;
				const int i = t.at(x + dx, y + dy);
				// The home swarm's apron, round its anchor 19 west and 12 north of the pond.
				const bool apron = dx >= -30 && dx <= -8 && dy >= -23 && dy <= -1;
				if (L.labels[i] != cell || busy[i] || apron ||
					dx * dx + dy * dy < (kFarmRing + radius + 4) * (kFarmRing + radius + 4))
					continue;
				const RadialShape shape(radius, 0.35, context, "breachable-blotches");
				forEachTileInShape(t, x + dx, y + dy, shape, turn,
								   [&](int tile, double, double)
								   {
									   if (!busy[tile] && L.labels[tile] == cell &&
										   L.terrain[tile] == GRASS)
										   L.terrain[tile] = SAND;
								   });
				++b;
				++blotches;
			}
		}
		context.telemetry.measure("breachable.blotches", blotches);
		context.telemetry.choice("breachable.ponds.home-design", pondDesignName(homeDesign));
	}
	layBeaches(L.terrain, t);
	const auto grass = pureTiles(L.terrain, t, GRASS);
	const auto fertility = cropGrowthField(L.terrain, t);
	for (int i = 0; i < n; ++i)
		if ((L.protectedTiles[i] && !grass[i]) || (L.wood[i] && fertility.at(i % t.w, i / t.w)))
		{
			L.failure =
				"A farm reaches a ridge or waters a saddle; use larger valleys or smaller ponds.";
			return L;
		}
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "breachable layout";
	const BreachableHighlandsOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	context.stage = "breachable terrain";
	writeUndermap(map, L.terrain);
	for (int i = 0; i < t.size(); ++i)
		if (L.protectedTiles[i])
			map.setResource(i % t.w, i / t.w, L.wood[i] ? WOOD : STONE, 1);
	for (int k = 0; k < context.request.nbTeams; ++k)
		game.addTeam();
	context.stage = "breachable colonies";
	if (!settleColonies(
			game, context, "breachable-starts",
			[&](int team)
			{
				auto mask = homeGrassMask(map, t, L.homeOf, team);
				for (int i = 0; i < t.size(); ++i)
					mask[i] = mask[i] && !L.protectedTiles[i];
				return mask;
			},
			[&](int team)
			{
				const int cell = L.homes[team];
				// Bias the town toward the wheat half of the basin. The old due-west start
				// made the first food route go around the ring before reaching harvestable
				// wheat; permanent farm lanes alone did not cure the resulting food pressure
				// in Maxima games. A swarm centre about 17 west / 10 north of the pond
				// keeps a dry apron while favouring repeated food deliveries over the less
				// frequent opening wood trips. Settlement still checks the whole footprint.
				return MapGeneratorPoint(L.cells.centreTileX(cell) - 19,
										 L.cells.centreTileY(cell) - 12);
			}))
		return false;
	context.stage = "breachable farms";
	const auto reserved = swarmSurroundings(t, context);
	for (int cell = 0; cell < L.cells.cellCount(); ++cell)
	{
		const int cx = L.cells.centreTileX(cell), cy = L.cells.centreTileY(cell);
		const bool home = std::find(L.homes.begin(), L.homes.end(), cell) != L.homes.end();
		// Give food two independently seeded plots; a growth search cannot cross sand from
		// one into the other. Allocate two thirds of wheat to the northern half and one third
		// to the southeast quarter. This keeps the original northern reserve while adding a
		// second renewable harvest front, rather than just deepening an inaccessible deposit.
		for (int type : {WHEAT, WOOD})
		{
			const int amount = type == WHEAT ? o.wheat : o.wood;
			const int count = int(scaledCount(type == WHEAT ? 84 : 32, amount)) +
							  (home ? (type == WHEAT ? kHomeWheat : kHomeWood) : 0);
			int placed = 0;
			for (int plot = 0; plot < (type == WHEAT ? 2 : 1); ++plot)
			{
				const bool north = type == WHEAT && plot == 0;
				const int side = type == WHEAT ? 1 : -1;
				const auto eligible = [&](int i)
				{
					const int ox = t.offsetX(cx, i % t.w), oy = t.offsetY(cy, i / t.w);
					return L.labels[i] == cell && !reserved[i] && !L.protectedTiles[i] &&
						   ox * ox + oy * oy <= kFarmRadius * kFarmRadius &&
						   (north ? oy <= -2 : oy >= 2 && side * ox >= 2) &&
						   clearGround(map, i % t.w, i / t.w);
				};
				const int diagonal = (o.pondSize + 4) * 3 / 4;
				const int seed = seedNear(t, cx + (north ? 0 : side * diagonal),
										  cy + (north ? -(o.pondSize + 3) : diagonal), 5, eligible);
				const int quota = type == WOOD ? count : north ? count - count / 3 : count / 3;
				if (seed >= 0)
					placed += growPatch(map, t, seed, type, quota, eligible);
			}
			context.telemetry.measure(type == WHEAT ? "breachable.farm.wheat-requested"
													: "breachable.farm.wood-requested",
									  count, cell);
			if (placed < count)
				context.telemetry.fallback(
					"breachable.farm.saturated",
					"The contained plot is full; extra abundance cannot consume the town apron.",
					cell);
			context.telemetry.measure(type == WHEAT ? "breachable.farm.wheat-tiles"
													: "breachable.farm.wood-tiles",
									  placed, cell);
		}

		// Each neutral valley specialises in one fruit. Rewards are distributed across the
		// route graph, with a dry, buildable apron beside them for inns. There is no central
		// all-fruit jackpot; collecting varieties encourages holding several expansion valleys.
		if (!home)
		{
			const auto eligible = [&](int i)
			{
				return L.labels[i] == cell && !L.protectedTiles[i] &&
					   t.dist2(cx, cy, i % t.w, i / t.w) < 24 * 24 &&
					   clearGround(map, i % t.w, i / t.w);
			};
			const int seed = seedNear(t, cx + 19, cy + 6, 4, eligible);
			if (seed >= 0)
				growPatch(map, t, seed, CHERRY + cell % 3, int(scaledCount(6, o.fruit)), eligible);
		}
	}
	seedAlgae(map, context, t, "breachable-algae", o.algae, AlgaeBand::anyWater(25));
	// Protection includes wood plugs: start repairs may never pre-clear a strategic saddle.
	secureStartingCrops(game, context, t, 24, 32, 0, &L.protectedTiles);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, 100, o.algae, o.fruit}, 24, 32, 0,
						&L.protectedTiles);
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const BreachableHighlandsOptions o(context.request);
	const Map &map = game.map;
	if (const auto error = designMismatch(L, map, "breachable highlands"); !error.empty())
		return error;
	const Torus &t = L.t;
	const auto fertility = Fertility::forMap(map, false);
	for (int i = 0; i < t.size(); ++i)
	{
		const int type = map.getResource(i % t.w, i / t.w).type;
		if (L.stone[i] && type != STONE)
			return "A permanent ridge was cleared.";
		if (L.wood[i] && (type != WOOD || fertility.at(i % t.w, i / t.w)))
			return "A wooded saddle is open or can regrow.";
	}
	const auto walk =
		walkFromFirstColony(map, context.request.nbTeams, "the valleys", "through open passes");
	if (!walk.error.empty())
		return walk.error;
	// Every valley, including unoccupied expansions, has a reachable dry apron.
	for (int cell = 0; cell < L.cells.cellCount(); ++cell)
	{
		const int cx = L.cells.centreTileX(cell), cy = L.cells.centreTileY(cell);
		bool pondWater = false;
		for (int y = -o.pondSize; y <= o.pondSize && !pondWater; ++y)
			for (int x = -o.pondSize; x <= o.pondSize && !pondWater; ++x)
				pondWater = map.isWater(t.x(cx + x), t.y(cy + y));
		if (!pondWater)
			return "A valley lost its farm pond.";
		bool reached = false;
		for (int y = -18; y <= 18; ++y)
			for (int x = -18; x <= 18; ++x)
			{
				const int i = t.at(cx + x, cy + y);
				if (L.labels[i] == cell && x * x + y * y > 15 * 15 && walk.steps[i] >= 0)
					reached = true;
			}
		if (!reached)
			return "An expansion valley cannot be reached through the open passes.";
	}
	// Validate future crop connectivity, ignoring today's deposits and buildings. Growth
	// uses eight neighbours, so even a diagonal grass seam would eventually let wood
	// invade wheat. The north half and both southern quarters must stay separate.
	std::vector<unsigned char> grass(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		grass[i] = map.isGrass(i % t.w, i / t.w);
	const auto cropRegions = connectedRegions(grass, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<int> cropPlot(t.size(), -1);
	for (int cell = 0; cell < L.cells.cellCount(); ++cell)
	{
		const int cx = L.cells.centreTileX(cell), cy = L.cells.centreTileY(cell);
		for (int y = -kFarmRadius; y <= kFarmRadius; ++y)
			for (int x = -kFarmRadius; x <= kFarmRadius; ++x)
			{
				const int i = t.at(cx + x, cy + y);
				if (!grass[i] || x * x + y * y > kFarmRadius * kFarmRadius || !y)
					continue;
				cropPlot[i] = y < 0 ? 1 : x < 0 ? 2 : 4;
			}
	}
	if (labelComponents(cropRegions, cropPlot).conflictTile >= 0)
		return "A farm access lane lets wood spread into the wheat plot.";
	// The same partition operation underlies crop containment and gate validation:
	// connectivity must respect labels, including diagonal paths across the torus.
	// Gate validation additionally opens each complete plug in isolation and checks
	// that it touches the intended valleys. Material/growth checks remain above.
	const auto gates = checkGatePartition(t, walkableTiles(map), L.labels, L.gates);
	if (gates.leakTile >= 0)
		return "A ridge leaks between valleys outside its designed gates.";
	if (gates.badGate >= 0)
		return "A pass or cleared saddle does not reach both valleys.";
	// Structural connectivity is only the first floor: it does not establish traffic capacity,
	// AI expansion or strategic fairness. Count remaining home construction anchors as a
	// separate floor, then use retained AI games to inspect the economy as it develops.
	const auto buildable = buildableTiles(map);
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		auto region = homeGrassMask(map, t, L.homeOf, k);
		if (buildSites(t, buildable, region, 4) < kMinimumRoom)
			return "A starting valley has too little building room.";
	}
	return "";
}
} // namespace

BreachableHighlandsOptions::BreachableHighlandsOptions(const GenerationRequest &r)
	: valleySize(r.option("valley-size")), ridgeDepth(r.option("ridge-depth")),
	  passWidth(r.option("pass-width")), extraPasses(r.option("extra-passes")),
	  saddles(r.option("wooded-saddles")), warp(r.option("warp")), pondSize(r.option("pond-size")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition breachableHighlandsDefinition()
{
	return {
			"breachable-highlands",
			37,
			"Breachable highlands",
			5,
			false,
			{{"valley-size", "Valley size", 64, 96, 8, 64, ControlGroup::Layout},
			 {"ridge-depth", "Ridge depth", 3, 11, 2, 5, ControlGroup::Terrain},
			 {"pass-width", "Pass width", 6, 12, 2, 8, ControlGroup::Terrain},
			 {"extra-passes", "Extra open passes", 0, 30, 10, 10, ControlGroup::Layout},
			 {"wooded-saddles", "Wooded saddles", 25, 100, 25, 50, ControlGroup::Layout},
			 {"warp", "Warp", 0, 100, 10, 80, ControlGroup::Terrain},
			 {"pond-size", "Pond size", 3, 6, 1, 6, ControlGroup::Terrain},
			 // Structural stone and saddle wood remain at every abundance. Ridge depth sets the
			 // clearing investment; wood amount controls the renewable farm wood only.
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			// "Arena" is reserved for maps built around one shared, contested battleground
			// (Carousel, Amphitheatre, ...); this is a lattice of separate walled valleys, each a
			// colony's own stronghold, so it gets the other terrain value that pattern uses (Forts).
			{"terrain:stronghold", "feature:mountains", "feature:stone-walls", "style:siege",
			 "fairness:stamped-lattice"}};
}
