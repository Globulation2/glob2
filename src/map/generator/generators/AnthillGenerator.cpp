// SPDX-License-Identifier: GPL-3.0-or-later
#include "AnthillGenerator.h"
#include "Drawing.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "GraphMaze.h"
#include "Grid.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Resources.h"
#include "Roads.h"
#include "Room.h"
#include "Settlements.h"
#include "Sketch.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
using namespace MapGeneration;

// Anthill: a map of solid stone, carved into chambers joined by winding tunnels two or three tiles
// wide. Every colony starts in a queen chamber of its own, a cul-de-sac with a single door and a
// pond; the other chambers are farm chambers with a pond and a field, or dead-end treasure
// chambers with fruit and wheat and nothing else. Stone is everywhere and never runs out, but it
// cannot be built on and cannot be cleared, so the one thing scarce on this map is room: a chamber
// holds a few buildings, not a colony, and growing means taking the next chamber down the tunnel.
// Towers shoot through the rock into neighbouring tunnels.
//
// The chambers are scattered sites (Points.h) with their nearest-site cells as the graph the
// tunnels are carved on (GraphMaze.h): a spanning tree, a door into every queen chamber, and a
// share of extra tunnels for loops. Fairness is statistical, the lobby keeping the best of several
// seeds; every queen chamber is grown to hold the same number of building sites (Room.h), so no
// colony starts with more room than another.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Buildings need pure grass
// and stone is never cleared, so the map's building room is fixed at generation and the fight is
// over chambers; tunnels a column wide are closed by one building or one tower; the ponds in the
// farm chambers are the only water, so a colony that wants its fields to regrow holds a farm
// chamber; and a tower scans square rings with no line of sight, so the rock between two tunnels
// is no cover at all.
namespace
{

// The queen chamber's pond (radius 2, a dozen corners of water with a beach a unit walks round) and
// its kit, unscaled whatever the amounts say: wheat and wood either side of the pond. No quarry: the
// walls are stone.
constexpr double kPondRadius = 2.0;
constexpr int kHomeWheat = 12, kHomeWood = 10;
// A farm chamber is this much bigger than an ordinary chamber, to hold its pond and field; its field
// is this much wheat and wood.
constexpr int kFarmExtra = 2, kFarmWheat = 12, kFarmWood = 8;
// A treasure chamber holds a fruit grove of radius 1 (5 tiles) and this much wheat.
constexpr int kTreasureWheat = 8;
// Tunnels wander sideways by this share of the chamber spacing, and swell and narrow by this share
// of their width, so they read as dug rather than drawn.
constexpr double kWanderShare = 0.18, kWidthJitter = 0.25;
// The queen chamber starts this much bigger than an ordinary chamber and grows until it holds
// `queenRoom` building sites, never more than this many tiles.
constexpr int kQueenExtra = 1, kQueenGrowthLimit = 300;

struct Layout
{
	Torus t{1, 1};
	std::vector<Site> sites;
	std::vector<int> label; // every tile's nearest site
	std::vector<int> homeSite, farmSite, treasureSite;
	std::vector<unsigned char> open, water; // tunnels and chambers; ponds
	std::vector<int> homeOf;
	std::vector<ShapePoint> homes, kits;
	std::vector<double> axes; // each home's facing: away from its door
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const AnthillOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// The chambers' sites and the graph of neighbouring cells the tunnels follow.
	L.sites = spreadPoints(t, o.chamberSpacing, context, "anthill-sites");
	L.label = nearestSiteLabels(t, L.sites, o.chamberSpacing, context, "anthill-warp", 150, 0);
	const CellGraph graph = cellGraph(t, L.sites, siteNeighbours(t, L.label, int(L.sites.size())));
	L.homeSite = spreadPockets(graph, teams);
	if (int(L.homeSite.size()) != teams || int(L.sites.size()) < teams + 2)
	{
		L.failure = "Too many colonies for this map; use a bigger map, closer chambers or fewer "
					"colonies.";
		return L;
	}
	// The tunnels: a spanning tree through every chamber but the queens', a door into each queen
	// chamber, and `loops` percent of the chambers' count in extra tunnels.
	std::vector<unsigned char> isPocket(L.sites.size(), 0);
	for (int site : L.homeSite)
		isPocket[site] = 1;
	std::vector<unsigned char> openEdge(graph.edgeCells.size(), 0);
	if (!carveSpanningTree(graph, context, "anthill-maze", isPocket, openEdge))
	{
		L.failure = "The chambers could not all be joined; use closer chambers.";
		return L;
	}
	const std::vector<int> doors =
		openPocketDoors(graph, context, "anthill-maze", L.homeSite, isPocket, openEdge);
	openLoops(graph, context, "anthill-maze", isPocket, o.loops, openEdge);
	// Dead ends other than the queens' are treasure chambers; of the rest, every other one is a
	// farm chamber (index parity, which is as random as the sites are).
	std::vector<int> exitEdge;
	const std::vector<int> ends = deadEnds(graph, openEdge, isPocket, exitEdge);
	std::vector<unsigned char> isEnd(L.sites.size(), 0);
	for (int site : ends)
		isEnd[site] = 1;
	L.treasureSite = ends;
	for (int site = 0; site < int(L.sites.size()); ++site)
		if (!isPocket[site] && !isEnd[site] && site % 2 == 0)
			L.farmSite.push_back(site);

	// Carving: every open edge a wandering tunnel between its two chambers' middles, then every
	// chamber a rough disc, queen and farm chambers bigger.
	L.open.assign(n, 0);
	L.water.assign(n, 0);
	std::mt19937 &tunnels = context.stream("anthill-tunnels");
	for (int edge = 0; edge < int(openEdge.size()); ++edge)
		if (openEdge[edge])
		{
			const Site a = L.sites[graph.edgeCells[edge][0]], b = L.sites[graph.edgeCells[edge][1]];
			carveCorridor(L.open, t, {a.x + 0.5, a.y + 0.5}, {b.x + 0.5, b.y + 0.5},
						  o.tunnelWidth / 2.0, kWanderShare * o.chamberSpacing, kWidthJitter,
						  tunnels);
		}
	const RadialShape chamber(o.chamberSize, 0.3, context, "anthill-chambers");
	const RadialShape farm(o.chamberSize + kFarmExtra, 0.3, context, "anthill-chambers");
	const RadialShape queen(o.chamberSize + kQueenExtra, 0.25, context, "anthill-chambers");
	const RadialShape pond(kPondRadius, 0.3, context, "anthill-ponds");
	std::vector<int> kind(L.sites.size(), 0); // 0 chamber, 1 farm, 2 queen
	for (int site : L.farmSite)
		kind[site] = 1;
	for (int site : L.homeSite)
		kind[site] = 2;
	for (int site = 0; site < int(L.sites.size()); ++site)
	{
		// Each chamber turned by a different angle, so one outline reads as many.
		const double turn = site * 2.399963; // the golden angle
		const RadialShape &shape = kind[site] == 2 ? queen : kind[site] == 1 ? farm : chamber;
		fillShape(L.open, t, L.sites[site].x + 0.5, L.sites[site].y + 0.5, shape, turn);
	}
	// Ponds: in every farm chamber at its middle; in every queen chamber on the far side from the
	// door, with the swarm towards the door, so the kit lies between them and the way out is clear.
	for (int site : L.farmSite)
		fillShape(L.water, t, L.sites[site].x + 0.5, L.sites[site].y + 0.5, pond, 0.0);
	L.homeOf.assign(n, -1);
	for (int k = 0; k < teams; ++k)
	{
		const int site = L.homeSite[k];
		const Site s = L.sites[site];
		const ShapePoint centre{s.x + 0.5, s.y + 0.5};
		// The door's direction: towards the chamber across the door edge.
		const int across = graph.other(doors[k], site);
		const double axis = std::atan2(-double(t.offsetY(s.y, L.sites[across].y)),
									   -double(t.offsetX(s.x, L.sites[across].x)));
		const double pondOut = o.chamberSize + kQueenExtra - kPondRadius - 2.5;
		const ShapePoint pondAt = polarPoint(centre.x, centre.y, pondOut, axis);
		fillShape(L.water, t, pondAt.x, pondAt.y, pond, 0.0);
		L.homes.push_back(centre);
		L.kits.push_back(pondAt);
		L.axes.push_back(axis);
	}
	// The queen chambers grow until each holds `queenRoom` building sites: the tiles nearest the
	// middle first, within the chamber's own cell so it never meets a neighbour, and never onto water
	// or the beach a pond will get (two tiles round it).
	const std::vector<unsigned char> shore = dilate(t, L.water, 2);
	std::vector<unsigned char> buildable(n, 0);
	for (int i = 0; i < n; ++i)
		buildable[i] = !shore[i];
	for (int k = 0; k < teams; ++k)
	{
		const int site = L.homeSite[k];
		std::vector<unsigned char> region(n, 0), eligible(n, 0);
		for (int i = 0; i < n; ++i)
		{
			region[i] = L.open[i] && L.label[i] == site;
			eligible[i] = L.label[i] == site && !shore[i];
		}
		growUntilSites(t, region, buildable, eligible, o.queenRoom, kQueenGrowthLimit, [&](int i)
					   { return t.dist2(L.sites[site].x, L.sites[site].y, i % t.w, i / t.w); });
		for (int i = 0; i < n; ++i)
			if (region[i])
				L.open[i] = 1;
	}
	for (int i = 0; i < n; ++i)
		if (L.water[i])
			L.open[i] = 1;
	for (int k = 0; k < teams; ++k)
		for (int i = 0; i < n; ++i)
			if (L.open[i] && L.label[i] == L.homeSite[k])
				L.homeOf[i] = k;
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "anthill layout";
	const AnthillOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "anthill terrain";
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
		if (L.water[i])
			terrain[i] = WATER;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	// The rock: stone on every tile that is not carved, wherever pure grass allows it.
	std::vector<unsigned char> rock(n, 0);
	for (int i = 0; i < n; ++i)
		if (!L.open[i] && map.isResourceAllowed(i % t.w, i / t.w, STONE))
		{
			map.setResource(i % t.w, i / t.w, STONE, 1);
			rock[i] = 1;
		}

	context.stage = "anthill colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.homeOf[i] == team && map.isGrass(i % t.w, i / t.w) && !rock[i];
		return ground;
	};
	// The swarm stands between the chamber's middle and its door.
	const auto anchor = [&](int team)
	{
		const ShapePoint p = polarPoint(L.homes[team].x, L.homes[team].y, 2.0, L.axes[team] + kPi);
		return MapGeneratorPoint(int(std::lround(p.x)) - 2, int(std::lround(p.y)) - 2);
	};
	if (!settleColonies(game, context, "anthill-starts", homeMask, anchor))
		return false;

	context.stage = "anthill resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
	{
		const auto eligible = [&](int i)
		{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
		// Wheat and wood either side of the pond, towards the swarm.
		const KitFrame frame{int(std::lround(L.kits[k].x)), int(std::lround(L.kits[k].y)),
							 L.axes[k]};
		plantKit(map, t, context,
				 {frame.at(-2, -(kPondRadius + 3), 6), frame.at(-2, kPondRadius + 3, 6),
				  frame.at(0, 0, 0), kHomeWheat, kHomeWood, -1},
				 eligible);
	}
	// Farm chambers: a field round the pond; treasure chambers: a fruit grove and a wheat patch.
	for (int site : L.farmSite)
	{
		const Site s = L.sites[site];
		const auto here = [&](int i)
		{ return L.open[i] && L.label[i] == site && clearGround(map, i % t.w, i / t.w); };
		if (const int seed = seedNear(t, s.x - 4, s.y, 6, here); seed >= 0)
			growPatch(map, t, seed, CORN, int(scaledCount(kFarmWheat, o.wheat)), here);
		if (const int seed = seedNear(t, s.x + 4, s.y, 6, here); seed >= 0)
			growPatch(map, t, seed, WOOD, int(scaledCount(kFarmWood, o.wood)), here);
	}
	for (int site : L.treasureSite)
	{
		const Site s = L.sites[site];
		const auto here = [&](int i)
		{ return L.open[i] && L.label[i] == site && clearGround(map, i % t.w, i / t.w); };
		if (scaledCount(1, o.fruit) > 0 && here(t.at(s.x, s.y)))
			placeResourceClump(map, context, MapGeneratorPoint(s.x, s.y),
							   CHERRY + int(context.bounded("anthill-fruit", 3)), 1);
		if (const int seed = seedNear(t, s.x - 3, s.y - 3, 5, here); seed >= 0)
			growPatch(map, t, seed, CORN, int(scaledCount(kTreasureWheat, o.wheat)), here);
	}
	seedAlgae(map, context, t, "anthill-algae", o.algae, AlgaeBand::anyWater(12));
	secureStartingCrops(game, context, t, 24, 32, 0, &rock);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, 100, o.algae, o.fruit}, 24, 32, 0, &rock);
	// The tunnels join every chamber; only crops in a tunnel could close one, and only those are
	// cleared. Stone is cut only as a last resort, at a cost that keeps it to a tile or two.
	context.stage = "anthill routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, 12, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "anthill"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		bool pond = false;
		for (int dy = -2; dy <= 2 && !pond; ++dy)
			for (int dx = -2; dx <= 2 && !pond; ++dx)
				pond = map.isWater(t.x(int(L.kits[k].x) + dx), t.y(int(L.kits[k].y) + dy));
		if (!pond)
			return "Colony " + std::to_string(k) + "'s queen chamber has lost its pond.";
	}
	return walkFromFirstColony(map, context.request.nbTeams, "the anthill", "through the tunnels")
		.error;
}
} // namespace

AnthillOptions::AnthillOptions(const GenerationRequest &r)
	: chamberSpacing(r.option("chamber-spacing")), chamberSize(r.option("chamber-size")),
	  queenRoom(r.option("queen-room")), tunnelWidth(r.option("tunnel-width")),
	  loops(r.option("loops")), wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition anthillDefinition()
{
	return {
		"anthill",
		32,
		"Anthill",
		1,
		false,
		// Chambers 28 apart give a 256 map about a hundred of them; a chamber of radius 5 holds
		// three or four buildings; a queen chamber grown to 40 building sites (overlapping 4x4
		// footprints, the start scorer's measure) holds a swarm, an inn and a couple more; tunnels
		// three wide take a column of units and are closed by one tower; a fifth of the chambers'
		// count in loops keeps a way round most blockades.
		{{"chamber-spacing", "Chamber spacing", 20, 40, 2, 28, ControlGroup::Layout},
		 {"chamber-size", "Chamber size", 4, 8, 1, 5, ControlGroup::Layout},
		 {"queen-room", "Queen room", 20, 120, 10, 40, ControlGroup::Layout},
		 {"tunnel-width", "Tunnel width", 2, 4, 1, 3, ControlGroup::Terrain},
		 {"loops", "Loops", 0, 60, 10, 20, ControlGroup::Terrain},
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		[](const GenerationRequest &r) -> std::string
		{
			GenerationContext probe(r);
			return design(r, probe).failure;
		},
		validateWorld};
}
