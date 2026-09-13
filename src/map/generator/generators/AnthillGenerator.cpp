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
//
// FEEDBACK 2026-09-13 (first play): "visually striking ... but the map currently just doesn't provide
// enough water to make a playable game. the buildable area restriction feels real, maybe a bit too
// tight. ... each of the nodes just needs to dig out a larger hole with a larger pond of water in the
// center ... the default channels are so narrow that AIs are putting buildings into them and
// trapping themselves. maybe we add sand roads to all of the narrow ant tunnels to make sure they
// stay open? if the sand roads connect all the way into the water pool in the center, and we make
// sure that we stamp all of the area around the lake with resources (so there are no sections
// empty of growth), it could work really nicely. and let's make sure that every chamber actually
// has something - either wheat, wood, fruit." So: every chamber, whatever its kind, is dug bigger
// (default size 7, was 5) with a pond at its middle sized to it; a sand road runs down every tunnel
// and on to the shore of the pond at each end (a sand corner spoils the tiles round it for building,
// so nothing can be built across a tunnel); a farm chamber's pond is ringed with wheat; every other
// chamber alternates a wheat and a wood patch; queen chambers grow to 60 sites (was 40).
namespace
{

// Every chamber's pond sits at its middle (FEEDBACK 2026-09-13), its radius this share of the
// chamber's: 2.8 at the default size 7, about 25 corners of water, which the growth probe finds from
// the whole chamber. A farm chamber's pond is a tile bigger, a queen chamber's half a tile.
constexpr double kPondShare = 0.4, kFarmPondExtra = 1.0, kQueenPondExtra = 0.5;
double pondRadiusFor(int chamberSize)
{
	return std::max(2.0, kPondShare * chamberSize);
}
// The queen chamber's kit, unscaled whatever the amounts say: wheat and wood either side of the pond.
// No quarry: the walls are stone.
constexpr int kHomeWheat = 14, kHomeWood = 10;
// A farm chamber is this much bigger than an ordinary chamber, to hold its pond and field; its field
// is a ring of wheat round the whole pond (FEEDBACK 2026-09-13, "no sections empty of growth": the
// ring two to four tiles out from a pond of radius 4 is about 60 tiles) and a wood patch beyond.
constexpr int kFarmExtra = 2, kFarmWheat = 40, kFarmWood = 12;
// Every other chamber holds a wheat patch or a wood patch in turn (FEEDBACK 2026-09-13, "every
// chamber actually has something"); a treasure chamber holds a fruit grove of radius 1 (5 tiles) and
// this much wheat.
constexpr int kPlainWheat = 16, kPlainWood = 12, kTreasureWheat = 12;
// A tunnel's sand road stops this far past the pond's radius at each end: on the pond's beach, so it
// joins the water without cutting into it.
constexpr double kRoadStop = 1.5;
// Tunnels wander sideways by this share of the chamber spacing, and swell and narrow by this share
// of their width, so they read as dug rather than drawn.
constexpr double kWanderShare = 0.18, kWidthJitter = 0.25;
// The queen chamber starts this much bigger than an ordinary chamber and grows until it holds
// `queenRoom` building sites, never more than this many tiles.
constexpr int kQueenExtra = 3, kQueenGrowthLimit = 400;

struct Layout
{
	Torus t{1, 1};
	std::vector<Site> sites;
	std::vector<int> label; // every tile's nearest site
	std::vector<int> homeSite, farmSite, treasureSite;
	std::vector<unsigned char> open, water, road; // tunnels and chambers; ponds; sand roads
	std::vector<double> pondRadius;               // every chamber's pond
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

	// Every chamber's kind sets its size and its pond: 0 plain, 1 farm, 2 queen.
	std::vector<int> kind(L.sites.size(), 0);
	for (int site : L.farmSite)
		kind[site] = 1;
	for (int site : L.homeSite)
		kind[site] = 2;
	L.pondRadius.assign(L.sites.size(), pondRadiusFor(o.chamberSize));
	for (int site : L.farmSite)
		L.pondRadius[site] += kFarmPondExtra;
	for (int site : L.homeSite)
		L.pondRadius[site] += kQueenPondExtra;

	// Carving: every open edge a wandering tunnel between its two chambers' middles, with a sand road
	// down its middle that stops on the shore of the pond at each end (FEEDBACK 2026-09-13); then
	// every chamber a rough disc, queen and farm chambers bigger.
	L.open.assign(n, 0);
	L.water.assign(n, 0);
	L.road.assign(n, 0);
	std::mt19937 &tunnels = context.stream("anthill-tunnels");
	for (int edge = 0; edge < int(openEdge.size()); ++edge)
	{
		if (!openEdge[edge])
			continue;
		const int siteA = graph.edgeCells[edge][0], siteB = graph.edgeCells[edge][1];
		const Site a = L.sites[siteA], b = L.sites[siteB];
		const std::vector<StrokePoint> path =
			wanderingPath(t, {a.x + 0.5, a.y + 0.5}, {b.x + 0.5, b.y + 0.5}, o.tunnelWidth / 2.0,
						  kWanderShare * o.chamberSpacing, kWidthJitter, tunnels);
		strokePath(L.open, t, path);
		if (!o.sandRoads)
			continue;
		// The road: the path's points beyond each end's pond and beach.
		std::vector<StrokePoint> road;
		for (const StrokePoint &p : path)
			if (std::hypot(p.x - path.front().x, p.y - path.front().y) >
					L.pondRadius[siteA] + kRoadStop &&
				std::hypot(p.x - path.back().x, p.y - path.back().y) >
					L.pondRadius[siteB] + kRoadStop)
				road.push_back(p);
		if (road.size() >= 2)
			tracePath(L.road, t, road);
	}
	const RadialShape chamber(o.chamberSize, 0.3, context, "anthill-chambers");
	const RadialShape farm(o.chamberSize + kFarmExtra, 0.3, context, "anthill-chambers");
	const RadialShape queen(o.chamberSize + kQueenExtra, 0.25, context, "anthill-chambers");
	for (int site = 0; site < int(L.sites.size()); ++site)
	{
		// Each chamber turned by a different angle, so one outline reads as many.
		const double turn = site * 2.399963; // the golden angle
		const RadialShape &shape = kind[site] == 2 ? queen : kind[site] == 1 ? farm : chamber;
		fillShape(L.open, t, L.sites[site].x + 0.5, L.sites[site].y + 0.5, shape, turn);
	}
	// A pond at the middle of every chamber (FEEDBACK 2026-09-13). In a queen chamber the swarm
	// stands between the pond and the door, so the kit round the pond lies behind it and the way out
	// is clear.
	const RadialShape pondPlain(pondRadiusFor(o.chamberSize), 0.3, context, "anthill-ponds");
	const RadialShape pondFarm(pondRadiusFor(o.chamberSize) + kFarmPondExtra, 0.3, context,
							   "anthill-ponds");
	const RadialShape pondQueen(pondRadiusFor(o.chamberSize) + kQueenPondExtra, 0.3, context,
								"anthill-ponds");
	for (int site = 0; site < int(L.sites.size()); ++site)
		fillShape(L.water, t, L.sites[site].x + 0.5, L.sites[site].y + 0.5,
				  kind[site] == 2   ? pondQueen
				  : kind[site] == 1 ? pondFarm
									: pondPlain,
				  0.0);
	for (int i = 0; i < n; ++i)
		if (L.water[i])
			L.road[i] = 0;
	L.homeOf.assign(n, -1);
	for (int k = 0; k < teams; ++k)
	{
		const int site = L.homeSite[k];
		const Site s = L.sites[site];
		const ShapePoint centre{s.x + 0.5, s.y + 0.5};
		// The door's direction: towards the chamber across the door edge; the axis points away from it.
		const int across = graph.other(doors[k], site);
		const double axis = std::atan2(-double(t.offsetY(s.y, L.sites[across].y)),
									   -double(t.offsetX(s.x, L.sites[across].x)));
		L.homes.push_back(centre);
		L.kits.push_back(centre);
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
		terrain[i] = L.water[i] ? WATER : L.road[i] ? SAND : GRASS;
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
	// The swarm stands between the pond and the door: past the pond's beach (a tile), a lane, and half
	// its own footprint.
	const auto anchor = [&](int team)
	{
		const ShapePoint p = polarPoint(L.homes[team].x, L.homes[team].y,
										L.pondRadius[L.homeSite[team]] + 4.5, L.axes[team] + kPi);
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
		// Wheat and wood either side of the pond, on the flanks of the way to the door.
		const double pr = L.pondRadius[L.homeSite[k]];
		const KitFrame frame{int(std::lround(L.kits[k].x)), int(std::lround(L.kits[k].y)),
							 L.axes[k]};
		plantKit(map, t, context,
				 {frame.at(-1, -(pr + 3), 6), frame.at(-1, pr + 3, 6), frame.at(0, 0, 0),
				  kHomeWheat, kHomeWood, -1},
				 eligible);
	}
	// Farm chambers: a ring of wheat round the whole pond (grown from its shore, breadth first, so it
	// fills the ring before it reaches further out) and a wood patch beyond; every other chamber a
	// wheat patch or a wood patch in turn; treasure chambers a fruit grove and a wheat patch
	// (FEEDBACK 2026-09-13: every chamber has something, and the ground round a pond is never empty).
	std::vector<int> kind(L.sites.size(), 0);
	for (int site : L.farmSite)
		kind[site] = 1;
	for (int site : L.homeSite)
		kind[site] = 2;
	for (int site : L.treasureSite)
		kind[site] = 3;
	for (int site = 0; site < int(L.sites.size()); ++site)
	{
		const Site s = L.sites[site];
		const int reach = int(L.pondRadius[site]) + 2;
		const auto here = [&](int i)
		{
			return L.open[i] && L.label[i] == site && !reserved[i] &&
				   clearGround(map, i % t.w, i / t.w);
		};
		if (kind[site] == 2)
			continue;
		if (kind[site] == 1)
		{
			if (const int seed = seedNear(t, s.x, s.y, reach, here); seed >= 0)
				growPatch(map, t, seed, CORN, int(scaledCount(kFarmWheat, o.wheat)), here);
			if (const int seed = seedNear(t, s.x + reach + 3, s.y, 4, here); seed >= 0)
				growPatch(map, t, seed, WOOD, int(scaledCount(kFarmWood, o.wood)), here);
		}
		else if (kind[site] == 3)
		{
			if (scaledCount(1, o.fruit) > 0)
				if (const int seed = seedNear(t, s.x - reach - 1, s.y, 3, here); seed >= 0)
					placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w),
									   CHERRY + int(context.bounded("anthill-fruit", 3)), 1);
			if (const int seed = seedNear(t, s.x + reach, s.y, 3, here); seed >= 0)
				growPatch(map, t, seed, CORN, int(scaledCount(kTreasureWheat, o.wheat)), here);
		}
		else if ((site / 2) % 2 == 0)
		{
			if (const int seed = seedNear(t, s.x, s.y, reach, here); seed >= 0)
				growPatch(map, t, seed, CORN, int(scaledCount(kPlainWheat, o.wheat)), here);
		}
		else if (const int seed = seedNear(t, s.x, s.y, reach, here); seed >= 0)
			growPatch(map, t, seed, WOOD, int(scaledCount(kPlainWood, o.wood)), here);
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
	  loops(r.option("loops")), sandRoads(r.option("sand-roads") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition anthillDefinition()
{
	return {
		"anthill",
		32,
		"Anthill",
		2,
		false,
		// Chambers 28 apart give a 256 map about a hundred of them; a chamber of radius 5 holds
		// three or four buildings; a queen chamber grown to 40 building sites (overlapping 4x4
		// footprints, the start scorer's measure) holds a swarm, an inn and a couple more; tunnels
		// three wide take a column of units and are closed by one tower; a fifth of the chambers'
		// count in loops keeps a way round most blockades.
		{{"chamber-spacing", "Chamber spacing", 20, 40, 2, 28, ControlGroup::Layout},
		 // FEEDBACK 2026-09-13: chambers of 7 (was 5) with a pond each, queen chambers grown to
		 // 60 sites (was 40); tunnels stay 3 wide but carry a sand road so nothing is built across.
		 {"chamber-size", "Chamber size", 5, 10, 1, 7, ControlGroup::Layout},
		 {"queen-room", "Queen room", 20, 160, 10, 60, ControlGroup::Layout},
		 {"tunnel-width", "Tunnel width", 2, 4, 1, 3, ControlGroup::Terrain},
		 {"loops", "Loops", 0, 60, 10, 20, ControlGroup::Terrain},
		 GeneratorControl::toggle("sand-roads", "Sand roads", true, ControlGroup::Terrain),
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
