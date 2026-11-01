// SPDX-License-Identifier: GPL-3.0-or-later
#include "AnthillGenerator.h"
#include "Drawing.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "GraphMaze.h"
#include "Grid.h"
#include "LatticeNoise.h"
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

// Anthill: a map of solid rock, carved into chambers joined by winding tunnels two or three tiles
// wide. Every colony starts in a queen chamber of its own, a dry cul-de-sac with a single door;
// the other chambers are farm chambers with a pond and a field, plain chambers with a pond and a
// wheat or wood patch, dead-end treasure chambers with fruit and wheat, or quarry chambers with a
// stone deposit. The rock cannot be built on, walked or cleared, so the one thing scarce on this
// map is room: a chamber holds a few buildings, not a colony, and growing means taking the next
// chamber down the tunnel. Towers shoot over the rock into neighbouring tunnels.
//
// The chambers are scattered sites (Points.h) with their nearest-site cells as the graph the
// tunnels are carved on (GraphMaze.h): a spanning tree, a door into every queen chamber, and a
// share of extra tunnels for loops. Fairness is statistical, the lobby keeping the best of several
// seeds; every queen chamber is grown to hold the same number of building sites (Room.h), so no
// colony starts with more room than another, and opens onto a farm chamber.
//
// WHY IT PLAYS WELL (docs/map-generators/game-rules-for-map-design.md). Buildings need pure grass
// and rock is never cleared, so the map's building room is fixed at generation and the fight is
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
//
// FEEDBACK 2026-09-13 (second play): "wheat and wood are only ever seeded on one side of the chamber,
// and because of the sand roads it never expands to the other sides. We want to make sure all the
// chambers that have wheat or wood have it seeded on all sides of their pond. also I think anthill
// will play nicer if there is NO water in your home base. you can still have some starting resources
// but if you're forced to expand down the ant zones, that makes it play a lot closer to the intended
// design: one 'queen ant' chamber that's special and then farming chambers that the ants expand
// into." So: a chamber's crops are grown from eight seeds spaced round its pond, so a road through
// the chamber never cuts them off from the rest of it; and the queen chamber has no pond at all, just
// a bigger starting kit that never regrows, so growing means taking a farm chamber.
//
// REVISION 2026-10-26 (the terrain catalogue): the rock is ridge rock and outcrop terrain on the
// vertices, not stone deposits on the tiles, so the walls hold no stone. Stone comes from quarry
// chambers instead, dry chambers with a deposit at the middle in a field of scree spoil, and from
// a small clump in every queen chamber's kit. The roads are dirt track, not sand: a line one
// vertex wide spoils the tiles beside it for building and crops exactly as the sand did,
// at ordinary walking speed (a mixed tile walks at its slowest walkable corner's speed and grows
// only what every corner allows). Ridge rock stops no projectile, so towers still shoot over it.
namespace
{

// Every chamber's pond sits at its middle (FEEDBACK 2026-09-13), its radius this share of the
// chamber's: 2.8 at the default size 7, about 25 corners of water, which the growth probe finds from
// the whole chamber. A farm chamber's pond is a tile bigger. A queen chamber has none (FEEDBACK
// 2026-09-13, second play): its middle is kept clear to kQueenCore for the swarm, and the roads stop
// there.
constexpr double kPondShare = 0.4, kFarmPondExtra = 1.0, kQueenCore = 3.5;
double pondRadiusFor(int chamberSize)
{
	return std::max(2.0, kPondShare * chamberSize);
}
// The queen chamber's kit, unscaled whatever the amounts say: wheat and wood on the flanks behind the
// swarm and a second wheat patch straight behind it. Bigger than a watered home's, since none of it
// regrows (second play: no water in the home, "you can still have some starting resources").
constexpr int kHomeWheat = 24, kHomeWood = 16, kHomeWheatBehind = 16;
// Crops in a chamber are grown from this many seeds spaced evenly round its pond, each given its
// share of the count (FEEDBACK 2026-09-13, second play: crops on one side never spread past the sand
// road to the other).
constexpr int kRingSeeds = 8;
// A farm chamber is this much bigger than an ordinary chamber, to hold its pond and field; its field
// is a ring of wheat round the whole pond (FEEDBACK 2026-09-13, "no sections empty of growth": the
// ring two to four tiles out from a pond of radius 4 is about 60 tiles) and a ring of wood beyond.
constexpr int kFarmExtra = 2, kFarmWheat = 48, kFarmWood = 16;
// Every other chamber holds a wheat ring or a wood ring in turn (FEEDBACK 2026-09-13, "every
// chamber actually has something"); a treasure chamber holds a fruit grove of radius 1 (5 tiles) and
// this much wheat.
constexpr int kPlainWheat = 24, kPlainWood = 16, kTreasureWheat = 16;
// A tunnel's sand road stops this far past the pond's radius at each end: on the pond's beach, so it
// joins the water without cutting into it.
constexpr double kRoadStop = 1.5;
// Tunnels wander sideways by this share of the chamber spacing, and swell and narrow by this share
// of their width, so they read as dug rather than drawn.
constexpr double kWanderShare = 0.18, kWidthJitter = 0.25;
// The queen chamber starts this much bigger than an ordinary chamber and grows until it holds
// `queenRoom` building sites, never more than this many tiles.
constexpr int kQueenExtra = 3, kQueenGrowthLimit = 400;
// Every chamber is dug this much wider than its size (REVISION 2026-10-26): rock on the vertices
// makes the open tiles along the face mixed, so the face comes out of the rock and a chamber keeps
// the building room and crop ground it had when its walls were stone. A tile and a half, not one:
// eroding a rough outline by a tile takes more than a tile from its notches, and with one a queen
// chamber held a tenth fewer 4x4 sites than revision 5's.
constexpr double kFace = 1.5;
// The nominal tunnel outline includes its rock-face margin. A separate minimum
// walking core below protects complete cells and diagonal side clearance where
// the wandering outline narrows; the width control still shapes the wider dug walls.
constexpr double kTunnelFace = 0.5;
// A quarry chamber (REVISION 2026-10-26) is dug to a farm chamber's size but has no pond: a stone
// deposit of radius kQuarryRadius at its middle at the default stone amount, its roads stopping
// kQuarryRoadStop short of the middle so the tiles they spoil never reach the deposit. Round the
// deposit lies a field of scree spoil heaps (lumpy noise above kSpoilNoise, from kSpoilClear out to
// the walls), so a quarry is poor building land, a place to work and not a second home; a lane
// kHaulLane tiles either side of the line from every tunnel's mouth to the deposit stays grass, so
// no haul crosses scree (which walks at about two thirds speed). First previews of quarries with
// three small heaps showed every quarry a dry clearing with more 4x4 sites than a queen chamber.
// One plain chamber in kQuarryEvery is a quarry, never the one outside a queen's door, and never
// fewer than one per colony while there are candidates.
constexpr double kQuarryRadius = 2.0, kQuarryRoadStop = 4.5, kSpoilClear = 4.0, kHaulLane = 2.5,
				 kSpoilCell = 3.0, kSpoilNoise = 0.36;
constexpr int kQuarryEvery = 3;
// The queen chamber's starter stone: exactly a plus of kHomeStone tiles towards its back wall,
// invariant whatever the amounts say, as the rest of the kit is. Not placeResourceClump, which
// drops edge tiles at random: first previews gave one colony five tiles and another one.
constexpr int kHomeStone = 5;
// The rock face: a rock vertex is outcrop where smooth noise tops the threshold for its depth (on
// open ground's edge, one tile behind it, deeper), ridge rock elsewhere, so the walls read broken
// at the face and solid behind it.
constexpr double kOutcropCell = 5.0, kOutcropFace = 0.42, kOutcropBehind = 0.62, kOutcropDeep = 0.8;

// What a chamber is: the queen chambers are the colonies' homes; farm chambers hold a pond and a
// field; treasure chambers are the tunnels' dead ends; the rest are plain.
enum ChamberKind
{
	PlainChamber = 0,
	FarmChamber,
	QueenChamber,
	TreasureChamber,
	QuarryChamber
};

// The quarry deposit's radius at `stone` percent of the default amount: by area, at least 1 while
// there is any stone, none at 0.
int quarryRadiusFor(int stone)
{
	if (stone <= 0)
		return 0;
	return std::max(1, int(std::lround(kQuarryRadius * std::sqrt(stone / 100.0))));
}

struct Layout
{
	Torus t{1, 1};
	std::vector<Site> sites;
	std::vector<int> label; // every tile's nearest site
	std::vector<int> homeSite, farmSite, treasureSite, quarrySite;
	std::vector<int> kind;                        // every chamber's ChamberKind
	std::vector<unsigned char> open, water, road; // tunnels and chambers; ponds; dirt-track roads
	std::vector<unsigned char> spoil;             // the quarries' scree heaps (tiles)
	TerrainSketch terrain;                        // every vertex as it is written
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
	// spreadPockets always starts from site 0, so the deal decides which colony gets which chamber.
	dealStarts(context, L.homeSite);
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
	// The chamber outside every queen's door is a farm chamber (REVISION 2026-10-27), so every
	// colony's first chamber down the tunnel is the same kind. Revision 6's chambers dug wider made
	// a farm on the doorstep worth far more than a plain one: the colony that drew it grew to twice
	// its rivals in four-AI calibration games, and the lobby's fairness median fell from 0.93 to 0.89.
	std::vector<unsigned char> doorstep(L.sites.size(), 0);
	for (int k = 0; k < teams; ++k)
		doorstep[graph.other(doors[k], L.homeSite[k])] = 1;
	// Other dead ends than the queens' and the doorsteps are treasure chambers; of the rest, every
	// other one is a farm chamber (index parity, which is as random as the sites are).
	std::vector<int> exitEdge;
	const std::vector<int> ends = deadEnds(graph, openEdge, isPocket, exitEdge);
	std::vector<unsigned char> isEnd(L.sites.size(), 0);
	for (int site : ends)
		if (!doorstep[site])
		{
			isEnd[site] = 1;
			L.treasureSite.push_back(site);
		}
	for (int site = 0; site < int(L.sites.size()); ++site)
		if (!isPocket[site] && !isEnd[site] && (doorstep[site] || site % 2 == 0))
			L.farmSite.push_back(site);
	// One plain chamber in kQuarryEvery is a quarry (REVISION 2026-10-26); a doorstep is a farm, so
	// no colony starts with a quarry on its doorstep; at least one per colony while there are
	// candidates.
	{
		int plain = 0;
		std::vector<int> candidates;
		for (int site = 0; site < int(L.sites.size()); ++site)
			if (!isPocket[site] && !isEnd[site] && !doorstep[site] && site % 2 != 0)
			{
				++plain;
				candidates.push_back(site);
			}
		context.shuffle(candidates.begin(), candidates.end(), "anthill-quarries");
		const int wanted =
			std::min(int(candidates.size()), std::max(teams, plain / kQuarryEvery));
		L.quarrySite.assign(candidates.begin(), candidates.begin() + wanted);
		std::sort(L.quarrySite.begin(), L.quarrySite.end());
	}

	// Every chamber's kind sets its size and its pond (a treasure chamber's are a plain chamber's).
	L.kind.assign(L.sites.size(), PlainChamber);
	for (int site : L.treasureSite)
		L.kind[site] = TreasureChamber;
	for (int site : L.farmSite)
		L.kind[site] = FarmChamber;
	for (int site : L.homeSite)
		L.kind[site] = QueenChamber;
	for (int site : L.quarrySite)
		L.kind[site] = QuarryChamber;
	const std::vector<int> &kind = L.kind;
	L.pondRadius.assign(L.sites.size(), pondRadiusFor(o.chamberSize));
	for (int site : L.farmSite)
		L.pondRadius[site] += kFarmPondExtra;
	for (int site : L.homeSite)
		L.pondRadius[site] = 0; // no water in a queen chamber (second play)
	for (int site : L.quarrySite)
		L.pondRadius[site] = 0; // nor in a quarry, whose middle is its deposit
	// Where a road stops at each end: on a pond's beach, short of a queen chamber's clear middle, or
	// short of a quarry's deposit.
	const auto roadStop = [&](int site)
	{
		return kind[site] == QueenChamber	 ? kQueenCore + kRoadStop
			   : kind[site] == QuarryChamber ? kQuarryRoadStop
											 : L.pondRadius[site] + kRoadStop;
	};

	// Carving: every open edge a wandering tunnel between its two chambers' middles, with a road
	// down its middle that stops on the shore of the pond at each end (FEEDBACK 2026-09-13); then
	// every chamber a rough disc, queen and farm chambers bigger.
	L.open.assign(n, 0);
	L.water.assign(n, 0);
	L.road.assign(n, 0);
	std::vector<unsigned char> tunnelCentre(n, 0);
	std::mt19937 &tunnels = context.stream("anthill-tunnels");
	for (int edge = 0; edge < int(openEdge.size()); ++edge)
	{
		if (!openEdge[edge])
			continue;
		const int siteA = graph.edgeCells[edge][0], siteB = graph.edgeCells[edge][1];
		const Site a = L.sites[siteA], b = L.sites[siteB];
		const std::vector<StrokePoint> path =
			wanderingPath(t, {a.x + 0.5, a.y + 0.5}, {b.x + 0.5, b.y + 0.5}, o.tunnelWidth / 2.0 + kTunnelFace,
						  kWanderShare * o.chamberSpacing, kWidthJitter, tunnels);
		strokePath(L.open, t, path);
		tracePath(tunnelCentre, t, path);
		if (!o.sandRoads)
			continue;
		// The road: the path's points beyond each end's pond and beach.
		std::vector<StrokePoint> road;
		for (const StrokePoint &p : path)
			if (std::hypot(p.x - path.front().x, p.y - path.front().y) > roadStop(siteA) &&
				std::hypot(p.x - path.back().x, p.y - path.back().y) > roadStop(siteB))
				road.push_back(p);
		if (road.size() >= 2)
			tracePath(L.road, t, road);
	}
	// Rock is stamped from every closed tile's four corners. A traversable
	// cell therefore needs its entire 3x3 tile neighborhood carved. Preserve a
	// minimum centreline, including both sides of each diagonal turn, before
	// adding that face clearance; jitter must not pinch an intended door shut.
	auto walkingCore = tunnelCentre;
	for (int i = 0; i < n; ++i)
		if (tunnelCentre[i])
			for (int dy : {-1, 1})
				for (int dx : {-1, 1})
					if (tunnelCentre[t.at(t.remainderX(i) + dx, i / t.w + dy)])
					{
						walkingCore[t.at(t.remainderX(i) + dx, i / t.w)] = 1;
						walkingCore[t.at(t.remainderX(i), i / t.w + dy)] = 1;
					}
	const auto minimumTunnel = dilate(t, walkingCore, 1);
	for (int i = 0; i < n; ++i)
		L.open[i] |= minimumTunnel[i];
	const RadialShape chamber(o.chamberSize + kFace, 0.3, context, "anthill-chambers");
	const RadialShape farm(o.chamberSize + kFarmExtra + kFace, 0.3, context, "anthill-chambers");
	const RadialShape queen(o.chamberSize + kQueenExtra + kFace, 0.25, context, "anthill-chambers");
	for (int site = 0; site < int(L.sites.size()); ++site)
	{
		// Each chamber turned by a different angle, so one outline reads as many.
		const double turn = site * 2.399963; // the golden angle
		const RadialShape &shape = kind[site] == QueenChamber ? queen
								   : kind[site] == FarmChamber || kind[site] == QuarryChamber
									   ? farm
									   : chamber;
		fillShape(L.open, t, L.sites[site].x + 0.5, L.sites[site].y + 0.5, shape, turn);
	}
	// A pond at the middle of every chamber but the queens' and the quarries' (FEEDBACK 2026-09-13,
	// both plays): a queen chamber is dry, its swarm at its middle and its kit behind, so the colony
	// must take a farm chamber for anything to regrow.
	const RadialShape pondPlain(pondRadiusFor(o.chamberSize), 0.3, context, "anthill-ponds");
	const RadialShape pondFarm(pondRadiusFor(o.chamberSize) + kFarmPondExtra, 0.3, context,
							   "anthill-ponds");
	for (int site = 0; site < int(L.sites.size()); ++site)
		if (kind[site] != QueenChamber && kind[site] != QuarryChamber)
			fillShape(L.water, t, L.sites[site].x + 0.5, L.sites[site].y + 0.5,
					  kind[site] == FarmChamber ? pondFarm : pondPlain, 0.0);
	for (int i = 0; i < n; ++i)
		if (L.water[i])
			L.road[i] = 0;
	// Scree spoil round every quarry's deposit (REVISION 2026-10-26), out to its walls, except in a
	// haul lane from each tunnel's mouth to the deposit.
	{
		std::vector<std::vector<ShapePoint>> lanes(L.sites.size()); // unit headings to every mouth
		for (int edge = 0; edge < int(openEdge.size()); ++edge)
			if (openEdge[edge])
				for (int end = 0; end < 2; ++end)
				{
					const int site = graph.edgeCells[edge][end];
					if (kind[site] != QuarryChamber)
						continue;
					const Site a = L.sites[site], b = L.sites[graph.other(edge, site)];
					const double dx = t.offsetX(a.x, b.x), dy = t.offsetY(a.y, b.y);
					const double length = std::max(1.0, std::hypot(dx, dy));
					lanes[site].push_back({dx / length, dy / length});
				}
		const PeriodicNoise heaps(t.w, t.h, kSpoilCell, context.stream("anthill-spoil"));
		L.spoil.assign(n, 0);
		for (int i = 0; i < n; ++i)
		{
			const int site = L.label[i];
			if (!L.open[i] || kind[site] != QuarryChamber)
				continue;
			const int x = t.remainderX(i), y = i / t.w;
			const double dx = t.offsetX(L.sites[site].x, x), dy = t.offsetY(L.sites[site].y, y);
			if (dx * dx + dy * dy <= kSpoilClear * kSpoilClear || heaps.at(x, y) <= kSpoilNoise)
				continue;
			const bool inLane = std::any_of(lanes[site].begin(), lanes[site].end(),
											[&](const ShapePoint &u)
											{
												return dx * u.x + dy * u.y > 0 &&
													   std::fabs(dy * u.x - dx * u.y) < kHaulLane;
											});
			L.spoil[i] = !inLane;
		}
		if (context.telemetry.enabled())
			context.telemetry.measure("anthill.spoil.tiles",
									  std::count(L.spoil.begin(), L.spoil.end(), 1));
	}
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
	// or the beach a pond will get (two tiles round it). Rock stands on vertices (REVISION
	// 2026-10-26), so an open tile beside the rock is mixed and holds no building: what grows is the
	// chamber's core, the tiles with open ground all round them, and the chamber is that core with a
	// tile of rock face round it.
	const std::vector<unsigned char> shore = dilate(t, L.water, 2);
	std::vector<unsigned char> buildable(n, 0);
	for (int i = 0; i < n; ++i)
		buildable[i] = !shore[i];
	for (int k = 0; k < teams; ++k)
	{
		const int site = L.homeSite[k];
		std::vector<unsigned char> cell(n, 0), carved(n, 0), eligible(n, 0);
		for (int i = 0; i < n; ++i)
		{
			cell[i] = L.label[i] == site;
			carved[i] = L.open[i] && cell[i];
		}
		std::vector<unsigned char> core = erode(t, carved, 1);
		const std::vector<unsigned char> inner = erode(t, cell, 1);
		for (int i = 0; i < n; ++i)
			eligible[i] = inner[i] && !shore[i];
		const int sites = growUntilSites(
			t, core, buildable, eligible, o.queenRoom, kQueenGrowthLimit,
			[&](int i) { return t.dist2(L.sites[site].x, L.sites[site].y, t.remainderX(i), i / t.w); });
		context.telemetry.measure("anthill.queen.build-sites", sites, k);
		if (sites < o.queenRoom)
			context.telemetry.fallback("anthill.queen.room-shortfall",
									   "Room growth stopped below the requested anchor count.", k);
		const std::vector<unsigned char> chamber = dilate(t, core, 1);
		for (int i = 0; i < n; ++i)
			if (chamber[i])
				L.open[i] = 1;
	}
	for (int i = 0; i < n; ++i)
		if (L.water[i])
			L.open[i] = 1;
	for (int k = 0; k < teams; ++k)
		for (int i = 0; i < n; ++i)
			if (L.open[i] && L.label[i] == L.homeSite[k])
				L.homeOf[i] = k;
	// The terrain (REVISION 2026-10-26): ponds, dirt-track roads and scree spoil on open ground, and
	// rock on every vertex of a closed tile, so a closed tile is rock on all four corners and as
	// impassable as the stone was. Mixed cells against a rock face are blocked;
	// the reserved walking core stays fully clear of those border cells.
	{
		std::vector<unsigned char> closed(n, 0);
		for (int i = 0; i < n; ++i)
			closed[i] = !L.open[i];
		const std::vector<unsigned char> rock = tileCorners(t, closed);
		const std::vector<unsigned char> scree = tileCorners(t, L.spoil);
		const std::vector<int> depth = stepsFrom(t, L.open);
		const PeriodicNoise face(t.w, t.h, kOutcropCell, context.stream("anthill-rock"));
		L.terrain.assign(n, GRASS);
		for (int i = 0; i < n; ++i)
		{
			const int x = t.remainderX(i), y = i / t.w;
			if (rock[i])
			{
				// Vertex (x, y) is the corner of tiles (x - 1 .. x, y - 1 .. y); its depth is theirs.
				int d = depth[i];
				for (const int j : {t.at(x - 1, y - 1), t.at(x, y - 1), t.at(x - 1, y)})
					d = std::min(d, depth[j]);
				const double threshold = d <= 0 ? kOutcropFace : d == 1 ? kOutcropBehind : kOutcropDeep;
				L.terrain[i] = face.at(x, y) > threshold ? OUTCROP : RIDGE_ROCK;
			}
			else
				L.terrain[i] = L.water[i] ? WATER : L.road[i] ? DIRT_TRACK : scree[i] ? SCREE : GRASS;
		}
		layBeaches(L.terrain, t);
	}
	context.telemetry.measure("anthill.chambers.actual", L.sites.size());
	context.telemetry.measure("anthill.quarries.actual", L.quarrySite.size());
	context.telemetry.measure("anthill.farms.actual", L.farmSite.size());
	context.telemetry.measure("anthill.treasure-chambers.actual", L.treasureSite.size());
	if (context.telemetry.enabled())
		context.telemetry.measure("anthill.tunnels.open",
								  std::count(openEdge.begin(), openEdge.end(), 1));
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
	writeVertices(map, L.terrain);

	context.stage = "anthill colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.homeOf[i] == team && map.terrainPropertiesAt(t.remainderX(i), i / t.w).buildable;
		return ground;
	};
	// The swarm stands at the chamber's middle, a tile towards the door.
	const auto anchor = [&](int team)
	{
		const ShapePoint p = polarPoint(L.homes[team].x, L.homes[team].y, 1.0, L.axes[team] + kPi);
		return MapGeneratorPoint(int(std::lround(p.x)) - 2, int(std::lround(p.y)) - 2);
	};
	if (!settleColonies(game, context, "anthill-starts", homeMask, anchor))
		return false;

	context.stage = "anthill resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
	{
		const auto eligible = [&](int i)
		{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, t.remainderX(i), i / t.w); };
		// Wheat and wood on the flanks behind the swarm (the axis points away from the door), a stone
		// clump against the back wall (REVISION 2026-10-26: the walls are no longer stone), and a
		// second wheat patch straight behind the swarm.
		const KitFrame frame{int(std::lround(L.kits[k].x)), int(std::lround(L.kits[k].y)),
							 L.axes[k]};
		plantKit(map, t, context,
				 {frame.at(3, -5, 6), frame.at(3, 5, 6), frame.at(0, 0, 0), kHomeWheat, kHomeWood, -1},
				 eligible);
		// The starter stone on the nearest ground that holds the whole plus.
		const int plus[kHomeStone][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
		const auto holdsPlus = [&](int i)
		{
			return std::all_of(std::begin(plus), std::end(plus),
							   [&](const int *d)
							   {
								   const int x = t.remainderX(i) + d[0], y = i / t.w + d[1];
								   const int j = t.at(x, y);
								   return eligible(j) &&
										  map.isResourceAllowed(t.remainderX(j), j / t.w, STONE);
							   });
		};
		const KitSeed stoneAt = frame.at(8, 0, 8);
		if (const int seed = seedNear(t, stoneAt.x, stoneAt.y, stoneAt.within, holdsPlus); seed >= 0)
			for (const auto &d : plus)
			{
				const int j = t.at(t.remainderX(seed) + d[0], seed / t.w + d[1]);
				map.setResourceByIndex(t.remainderX(j), j / t.w, STONE, 1);
			}
		else
			context.telemetry.fallback("anthill.queen.stone-missing",
									   "No ground near the back of the queen chamber held the starter stone.", k);
		const KitSeed behind = frame.at(7, 0, 6);
		if (const int seed = seedNear(t, behind.x, behind.y, behind.within, eligible); seed >= 0)
			growPatch(map, t, seed, WHEAT, kHomeWheatBehind, eligible);
		if (context.telemetry.enabled())
		{
			int stone = 0;
			for (int i = 0; i < n; ++i)
				stone += L.homeOf[i] == k && map.getResource(i).type == STONE;
			context.telemetry.measure("anthill.queen.stone-tiles", stone, k);
		}
	}
	// Every chamber with crops has them on every side of its pond (FEEDBACK 2026-09-13, second play):
	// a patch is grown from kRingSeeds seeds spaced evenly round the pond's shore, each given its
	// share of the count, so no road through the chamber can cut its crops off from the rest of it.
	// Farm chambers ring the pond with wheat and a wider ring of wood beyond ("no sections empty of
	// growth"); every other chamber a wheat or a wood ring in turn; treasure chambers a fruit grove
	// and a wheat ring; quarries a stone deposit at the middle and nothing else.
	const int quarryRadius = quarryRadiusFor(o.stone);
	std::vector<unsigned char> quarryGround(n, 0);
	for (int i = 0; i < n; ++i)
		quarryGround[i] = L.open[i] && L.kind[L.label[i]] == QuarryChamber && !reserved[i];
	const auto ringPlant = [&](int site, double radius, int type, int count, const auto &here)
	{
		const Site s = L.sites[site];
		for (int k = 0; k < kRingSeeds; ++k)
		{
			const double a = 2 * kPi * k / kRingSeeds;
			const int seed = seedNear(t, int(std::lround(s.x + 0.5 + radius * std::cos(a))),
									  int(std::lround(s.y + 0.5 + radius * std::sin(a))), 2, here);
			if (seed >= 0)
				growPatch(map, t, seed, type, (count + kRingSeeds - 1 - k) / kRingSeeds, here);
		}
	};
	for (int site = 0; site < int(L.sites.size()); ++site)
	{
		const Site s = L.sites[site];
		const double shore = L.pondRadius[site] + 2;
		const auto here = [&](int i)
		{
			return L.open[i] && L.label[i] == site && !reserved[i] &&
				   clearGround(map, t.remainderX(i), i / t.w);
		};
		if (L.kind[site] == QueenChamber)
			continue;
		if (L.kind[site] == QuarryChamber)
		{
			if (quarryRadius > 0)
				placeResourceClump(map, context, MapGeneratorPoint(s.x, s.y), STONE, quarryRadius,
								   &quarryGround);
			continue;
		}
		if (L.kind[site] == FarmChamber)
		{
			ringPlant(site, shore, WHEAT, int(scaledCount(kFarmWheat, o.wheat)), here);
			ringPlant(site, shore + 3, WOOD, int(scaledCount(kFarmWood, o.wood)), here);
		}
		else if (L.kind[site] == TreasureChamber)
		{
			if (scaledCount(1, o.fruit) > 0)
				if (const int seed = seedNear(t, s.x - int(shore) - 2, s.y, 3, here); seed >= 0)
					placeResourceClump(map, context, MapGeneratorPoint(t.remainderX(seed), seed / t.w),
									   CHERRY + int(context.bounded("anthill-fruit", 3)), 1);
			ringPlant(site, shore, WHEAT, int(scaledCount(kTreasureWheat, o.wheat)), here);
		}
		else if ((site / 2) % 2 == 0)
			ringPlant(site, shore, WHEAT, int(scaledCount(kPlainWheat, o.wheat)), here);
		else
			ringPlant(site, shore, WOOD, int(scaledCount(kPlainWood, o.wood)), here);
	}
	seedAlgae(map, context, t, "anthill-algae", o.algae, AlgaeBand::anyWater(12));
	// The stone deposits are designed: the crop guarantees never clear them.
	std::vector<unsigned char> stone(n, 0);
	for (int i = 0; i < n; ++i)
		stone[i] = map.getResource(i).type == STONE;
	context.telemetry.measure("anthill.stone.tiles", std::count(stone.begin(), stone.end(), 1));
	secureStartingCrops(game, context, t, 24, 32, 0, &stone);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&stone);
	// The tunnels join every chamber and rock is never cut; only crops in a tunnel could close one,
	// and only those are cleared.
	context.stage = "anthill routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "anthill"); !mismatch.empty())
		return mismatch;
	// The terrain is the design's, vertex for vertex: nothing may cut the rock or lay a ford.
	for (int i = 0; i < L.t.size(); ++i)
		if (map.vertexTerrainAt(L.t.remainderX(i), i / L.t.w) != L.terrain[i])
			return "The anthill's terrain differs from its design at vertex (" +
				   std::to_string(L.t.remainderX(i)) + ", " + std::to_string(i / L.t.w) + ").";
	// Every queen chamber holds its starter stone.
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		int stone = 0;
		for (int i = 0; i < L.t.size(); ++i)
			stone += L.homeOf[i] == k && map.getResource(i).type == STONE;
		if (stone < kHomeStone)
			return "Colony " + std::to_string(k) + "'s queen chamber holds " + std::to_string(stone) +
				   " stone tiles, not its " + std::to_string(kHomeStone) + ".";
	}
	// A quarry holds its deposit whenever there is stone to place.
	if (quarryRadiusFor(AnthillOptions(context.request).stone) > 0)
		for (int site : L.quarrySite)
			if (map.getResource(L.t.at(L.sites[site].x, L.sites[site].y)).type != STONE)
				return "A quarry chamber has no stone at its middle (" +
					   std::to_string(L.sites[site].x) + ", " + std::to_string(L.sites[site].y) + ").";
	// A queen chamber is dry by design (second play); the farm chambers keep the water.
	for (int site : L.farmSite)
		if (!map.isWater(L.sites[site].x, L.sites[site].y))
			return "A farm chamber has lost its pond at (" + std::to_string(L.sites[site].x) +
				   ", " + std::to_string(L.sites[site].y) + ").";
	return walkFromFirstColony(map, context.request.nbTeams, "the anthill", "through the tunnels")
		.error;
}
} // namespace

AnthillOptions::AnthillOptions(const GenerationRequest &r)
	: chamberSpacing(r.option("chamber-spacing")), chamberSize(r.option("chamber-size")),
	  queenRoom(r.option("queen-room")), tunnelWidth(r.option("tunnel-width")),
	  loops(r.option("loops")), sandRoads(r.option("sand-roads") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount")),
	  stone(r.option("stone-amount"))
{
}

GeneratorDefinition anthillDefinition()
{
	return {
		"anthill",
		32,
		"Anthill",
		7,
		false,
		// Chambers 28 apart give a 256 map about a hundred of them; a chamber of radius 7 holds a
		// pond and a few buildings; a queen chamber grown to 60 building sites (overlapping 4x4
		// footprints, the start scorer's measure) holds a swarm, an inn and a few more; tunnels
		// three wide take a column of units and are closed by one tower; a fifth of the chambers'
		// count in loops keeps a way round most blockades.
		{GeneratorControl{"chamber-spacing", "Chamber spacing", 20, 40, 2, 28, ControlGroup::Layout}
			 .withSearchRange(24, 34),
		 // FEEDBACK 2026-09-13: chambers of 7 (was 5) with a pond each, queen chambers grown to
		 // 60 sites (was 40); tunnels stay 3 wide but carry a road so nothing is built across.
		 GeneratorControl{"chamber-size", "Chamber size", 5, 10, 1, 7, ControlGroup::Layout}
			 .withSearchRange(7, 10),
		 GeneratorControl{"queen-room", "Queen room", 20, 160, 10, 60, ControlGroup::Layout}
			 .withSearchRange(60, 120),
		 GeneratorControl{"tunnel-width", "Tunnel width", 2, 4, 1, 3, ControlGroup::Terrain}
			 .withSearchRange(3, 4),
		 GeneratorControl{"loops", "Loops", 0, 60, 10, 20, ControlGroup::Terrain}.withSearchRange(
			 10, 40),
		 // Search keeps roads open as adjacent crops and trees regrow.
		 GeneratorControl::toggle("sand-roads", "Sand roads", true, ControlGroup::Terrain)
			 .withSearchValues({1}),
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount"),
		 // REVISION 2026-10-26: the quarries' deposits; the queen chamber's starter clump is fixed.
		 GeneratorControl::percentage("stone-amount", "Stone amount")},
		generate,
		true,
		designFailure<design>,
		validateWorld,
		// An organic tunnel network in solid rock, not a constructed battle arena.
		{"terrain:natural", "feature:caves", "feature:stone-walls", "style:tight-building"}};
}
