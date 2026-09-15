// SPDX-License-Identifier: GPL-3.0-or-later
#include "BraidedDeltaGenerator.h"
#include "Channels.h"
#include "Farmland.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Growth.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Room.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>
using namespace MapGeneration;

// Braided Delta is a river ladder wrapped round the torus. Winding longitudinal channels
// repeatedly exchange water through staggered side channels. Each exchange closes another
// water loop around an elongated island; there is no trunk mouth or privileged central island.
//
// PLAY INTENT: follow a bank to a ford, establish feeding/training on another island, or swim
// directly across a channel. Sand fords and beaches provide permanent circulation, while broad
// grass interiors carry buildings. Resource abundance changes supplies, never the land budget.
// This is an asymmetric landscape: comparable home clearings and spread-out seats are a starting
// heuristic, not a proof of equal contact times or equally valuable neighbouring islands.
namespace
{
constexpr const char *kLayout = "braided-delta-layout";
// Four vertices of radius leave a genuine water barrier after corner-to-tile conversion, even
// on bends. Beaches cost another vertex and pure grass needs four grass corners (Channels.h).
constexpr double kChannelRadius = 4.0;
// A 20x20 pure-grass town can hold several non-overlapping 4x4 buildings with worker lanes.
// The two-vertex sand rim stops adjacent crops spreading into it; it is not a no-growth flag.
constexpr int kTownWidth = 20;
constexpr int kTownOffset = 25;

struct Crossing
{
	ShapePoint from, to;
};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	Farm towns;
	std::vector<ShapePoint> sites, homes;
	std::vector<Crossing> crossings;
	std::vector<int> homeOf;
	std::vector<unsigned char> approaches;
	std::string failure;
	bool transpose = false;
	int along = 0, across = 0, cells = 0;
	double lane = 0, cell = 0, phase = 0, shift = 0;

	ShapePoint point(double u, double v) const
	{
		return transpose ? ShapePoint{v, u} : ShapePoint{u, v};
	}
	// Integer harmonics make the meander continuous at both torus seams. All channels share
	// the main displacement, so they cannot squeeze an island by wandering towards each other.
	double wave(double u) const
	{
		return 5 * std::sin(2 * kPi * u / along + phase) +
			   2 * std::sin(4 * kPi * u / along + phase * 0.7);
	}
	double bank(double u, int row) const { return shift + row * lane + wave(u); }
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const BraidedDeltaOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	// Run rivers along the longer dimension. On squares the seed chooses their orientation.
	L.transpose = t.h > t.w || (t.h == t.w && context.bounded(kLayout, 2));
	L.along = L.transpose ? t.h : t.w;
	L.across = L.transpose ? t.w : t.h;
	L.lane = double(L.across) / o.braids;
	// Island size is a minimum transverse land budget, including room beyond the town. More
	// frequent rejoins shorten islands, but never below twice that budget or 64 tiles: side
	// channels, their beaches, the town and two approaches must fit before detail is drawn.
	const int minimumLength = std::max(64, 2 * o.islandSize);
	const int desiredLength = minimumLength + (3 - o.rejoins) * 32;
	L.cells = std::max(1, L.along / desiredLength);
	L.cell = double(L.along) / L.cells;
	if (L.across < 128 || L.lane < o.islandSize + 16 || L.cell < minimumLength)
	{
		L.failure = "The braid channels leave too little island ground; enlarge the map or reduce "
					"braid count or island size.";
		return L;
	}
	if (request.nbTeams > o.braids * L.cells)
	{
		L.failure = "There are fewer roomy islands than colonies; enlarge the map or increase "
					"rejoining frequency.";
		return L;
	}
	L.phase = context.bounded(kLayout, 65536) * (2 * kPi / 65536);
	L.shift = context.bounded(kLayout, L.across);
	L.terrain.assign(t.size(), GRASS);
	std::vector<unsigned char> water(t.size(), 0);
	// A half-tile sample interval makes curves smooth without relying on a rasterizer to guess
	// their shape. These are independent masks; painting a later reach never erases another.
	for (int row = 0; row < o.braids; ++row)
	{
		std::vector<StrokePoint> path;
		for (double u = 0; u <= L.along; u += 0.5)
		{
			const auto p = L.point(u, L.bank(u, row));
			path.push_back({p.x, p.y, kChannelRadius});
		}
		strokePath(water, t, path);
		// Adjacent rows have different exchange stations, avoiding one cross-map choke point.
		const double stagger = row * L.cell / o.braids;
		for (int c = 0; c < L.cells; ++c)
		{
			const double start = c * L.cell + stagger;
			path.clear();
			for (int n = 0; n <= 128; ++n)
			{
				const double f = n / 128.0;
				const double u = start + 0.45 * L.cell * f + 6 * std::sin(kPi * f);
				const auto p = L.point(u, L.bank(u, row) + L.lane * (f * f * (3 - 2 * f)));
				path.push_back({p.x, p.y, kChannelRadius});
			}
			strokePath(water, t, path);
			// Propose a ford at each side reach midpoint. Together with main-channel fords,
			// these offer routes around both ends of an island; junction fitting follows below.
			const double middle = start + 0.225 * L.cell + 6;
			const double v = L.bank(middle, row) + L.lane / 2;
			L.crossings.push_back({L.point(middle - 8, v), L.point(middle + 8, v)});
			// The town sits downstream of the interval midpoint: a bent side channel leans
			// into the upstream half of the island. The extra 27% keeps the complete sand rim
			// clear on a 64-tile interval; the explicit footprint check below is authoritative.
			const double u = start + L.cell / 2 + 0.27 * L.cell;
			L.sites.push_back(L.point(u, L.bank(u, row) + kTownOffset));
		}
		// Spacing is measured along each main channel; rounding to whole periods closes the
		// seam. Propose at least two fords per channel to avoid a compulsory crossing.
		const int count = std::max(2, int(std::ceil(double(L.along) / o.crossingSpacing)));
		const double offset = context.bounded(kLayout, 65536) / 65536.0;
		for (int n = 0; n < count; ++n)
		{
			const double u = (n + offset) * L.along / count;
			const double v = L.bank(u, row);
			L.crossings.push_back({L.point(u, v - 9), L.point(u, v + 9)});
		}
	}
	for (int i = 0; i < t.size(); ++i)
		if (water[i])
			L.terrain[i] = WATER;
	layBeaches(L.terrain, t);
	// Fords near a split may end in another channel. Move each proposed crossing a bounded
	// distance along its bank until both ends reach pure grass; omit exhausted candidates
	// rather than painting a land bridge lengthwise down a river. Final island reachability
	// remains mandatory, so an omission cannot silently strand a useful island.
	const auto grassBeforeFords = pureTiles(L.terrain, t, GRASS);
	// Label the islands BEFORE fords join them. Approach roads must stay on their own island;
	// otherwise the nearest clearing search can cross a ford and service the wrong bank.
	std::vector<unsigned char> land(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		land[i] = L.terrain[i] != WATER;
	const auto islandOf = connectedRegions(land, t.w, t.h, true, GridNeighbors::Cardinal);
	std::vector<Crossing> fitted;
	for (const auto &proposed : L.crossings)
	{
		const double dx = proposed.to.x - proposed.from.x;
		const double dy = proposed.to.y - proposed.from.y;
		const double length = std::hypot(dx, dy);
		for (int offset : {0, 4, -4, 8, -8, 12, -12, 16, -16})
		{
			Crossing ford = proposed;
			ford.from.x -= offset * dy / length;
			ford.to.x -= offset * dy / length;
			ford.from.y += offset * dx / length;
			ford.to.y += offset * dx / length;
			const auto grassAt = [&](ShapePoint p)
			{ return grassBeforeFords[t.at(int(std::floor(p.x)), int(std::floor(p.y)))]; };
			if (!grassAt(ford.from) || !grassAt(ford.to))
				continue;
			if (bridgeAcross(L.terrain, t, ford.from, ford.to, 2.0) > 0)
				fitted.push_back(ford);
			break;
		}
	}
	context.telemetry.measure("braided-delta.crossings.requested", L.crossings.size());
	if (fitted.size() < L.crossings.size())
		context.telemetry.fallback("braided-delta.crossings.omitted",
								   "junction or overlapping ford");
	L.crossings = std::move(fitted);

	// Reserve the full grass rectangle AND its sand rim before furnishing. Check all of that
	// budget first: stampFarmPlot must not raise land over a channel to rescue a cramped island.
	L.towns.water.assign(t.size(), 0);
	L.towns.plot.assign(t.size(), 0);
	L.towns.sand.assign(t.size(), 0);
	int adjustedClearings = 0;
	for (auto &p : L.sites)
	{
		const auto fits = [&](ShapePoint centre)
		{
			const int x0 = int(centre.x) - kTownWidth / 2;
			const int y0 = int(centre.y) - kTownWidth / 2;
			for (int dy = -3; dy <= kTownWidth + 3; ++dy)
				for (int dx = -3; dx <= kTownWidth + 3; ++dx)
					if (L.terrain[t.at(x0 + dx, y0 + dy)] != GRASS)
						return false;
			return true;
		};
		// The smallest accepted islands can lose a corner of the nominal rectangle to a
		// meander (retained seed 53006 at 128x128). Translate the WHOLE clearing by at most
		// six tiles, nearest first, rather than shrinking its room or filling river water.
		// Check all candidates against the finished shoreline; no extra random draws are used.
		const ShapePoint nominal = p;
		bool found = fits(p);
		for (int radius = 2; !found && radius <= 6; radius += 2)
			for (int dy = -radius; !found && dy <= radius; dy += 2)
				for (int dx = -radius; !found && dx <= radius; dx += 2)
				{
					if (std::max(std::abs(dx), std::abs(dy)) != radius)
						continue;
					const ShapePoint candidate{nominal.x + dx, nominal.y + dy};
					if (fits(candidate))
					{
						p = candidate;
						found = true;
						++adjustedClearings;
					}
				}
		if (!found)
		{
			L.failure = "A delta island cannot fit its protected clearing; enlarge the map "
						"or reduce braid count.";
			return L;
		}
		stampFarmPlot(L.terrain, t, L.towns, int(p.x) - kTownWidth / 2, int(p.y) - kTownWidth / 2,
					  {kTownWidth, kTownWidth, 2});
	}
	context.telemetry.measure("braided-delta.clearings.adjusted", adjustedClearings);

	// A ford alone does not preserve a route: in revision-1 playtests, Numbi grew armies but
	// all four colonies lost ground contact when bank crops closed the approaches. Connect
	// BOTH ends of every fitted ford to the protected town rim on that same island. These
	// three-corner-wide sand lanes cannot grow crops or carry buildings. They cost a little
	// farmland, but do not require an AI to discover swimming or clear a crop wall to fight.
	// Use the shared shortest-walk primitive on the designed land, not a post-game repair.
	// Diagonal steps cost 14 versus 10 for cardinal steps, approximating geometric distance
	// without floating-point path priorities or long Manhattan detours. No water corner may be
	// painted, so these lanes cannot create extra river crossings.
	const auto townCorners = tileCorners(t, L.towns.plot);
	L.approaches.assign(t.size(), 0);
	int connectedEnds = 0, approachCorners = 0, longestApproach = 0;
	for (const auto &ford : L.crossings)
		for (const auto &end : {ford.from, ford.to})
		{
			const int source = t.at(int(std::floor(end.x)), int(std::floor(end.y)));
			const int island = islandOf[source];
			std::vector<unsigned char> goal(t.size(), 0);
			for (int i = 0; i < t.size(); ++i)
				goal[i] = islandOf[i] == island && L.towns.sand[i];
			const auto route =
				cheapestWalk(t, GridNeighbors::Eight, {source}, goal,
							 [&](int, int next, int dx, int dy)
							 {
								 return islandOf[next] == island && !townCorners[next]
											? (dx && dy ? 14 : 10)
											: -1;
							 });
			if (route.empty())
			{
				L.failure = "A delta ford cannot reach its island clearing.";
				return L;
			}
			++connectedEnds;
			longestApproach = std::max(longestApproach, int(route.size()) - 1);
			for (int i : route)
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int next = t.at(i % t.w + dx, i / t.w + dy);
						if (L.terrain[next] != WATER && !townCorners[next])
						{
							L.terrain[next] = SAND;
							approachCorners += !L.approaches[next];
							L.approaches[next] = 1;
						}
					}
		}
	context.telemetry.measure("braided-delta.approaches.connected_ends", connectedEnds);
	context.telemetry.measure("braided-delta.approaches.sand_corners", approachCorners);
	context.telemetry.measure("braided-delta.approaches.longest_steps", longestApproach);
	// Farthest-point seats spread early economies among islands. Shuffle the candidate list
	// to vary the first seat and ties, then deal the selected seats so index zero has no fixed role.
	auto candidates = L.sites;
	context.shuffle(candidates.begin(), candidates.end(), "braided-delta-seats");
	while (int(L.homes.size()) < request.nbTeams)
	{
		size_t best = 0;
		int bestDistance = -1;
		for (size_t i = 0; i < candidates.size(); ++i)
		{
			int distance = std::numeric_limits<int>::max();
			for (const auto &home : L.homes)
				distance = std::min(distance, t.dist2(int(home.x), int(home.y),
													  int(candidates[i].x), int(candidates[i].y)));
			if (distance > bestDistance)
			{
				bestDistance = distance;
				best = i;
			}
		}
		L.homes.push_back(candidates[best]);
		candidates.erase(candidates.begin() + best);
	}
	dealStarts(context, L.homes, "braided-delta-deal");
	L.homeOf.assign(t.size(), -1);
	for (int team = 0; team < request.nbTeams; ++team)
		for (int dy = -10; dy < 10; ++dy)
			for (int dx = -10; dx < 10; ++dx)
				L.homeOf[t.at(int(L.homes[team].x) + dx, int(L.homes[team].y) + dy)] = team;
	context.telemetry.measure("braided-delta.braids", o.braids);
	context.telemetry.measure("braided-delta.islands", L.sites.size());
	context.telemetry.measure("braided-delta.island_length", L.cell);
	context.telemetry.measure("braided-delta.channel_separation", L.lane);
	context.telemetry.measure("braided-delta.crossings", L.crossings.size());
	context.telemetry.choice("braided-delta.orientation", L.transpose ? "vertical" : "horizontal");
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "braided delta layout";
	const BraidedDeltaOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	Map &map = game.map;
	writeUndermap(map, L.terrain);
	for (int k = 0; k < context.request.nbTeams; ++k)
		game.addTeam();
	context.stage = "braided delta colonies";
	if (!settleColonies(
			game, context, "braided-delta-starts",
			[&](int team) { return homeGrassMask(map, t, L.homeOf, team); },
			[&](int team)
			{
				return MapGeneratorPoint(t.x(int(L.homes[team].x) - 2),
										 t.y(int(L.homes[team].y) - 2));
			}))
		return false;
	context.stage = "braided delta resources";
	const auto water = pureTiles(L.terrain, t, WATER);
	const auto distance = stepsFrom(t, water);
	// Reserve guaranteed bank supplies BEFORE ambient decoration. Bulk seed 711038 at
	// wheat=300%, wood=0% previously let wheat/stone/fruit occupy the wood kit's bank.
	// The generic emergency topup then planted wood inside the sand-rimmed town; later
	// crop growth trapped the colony in its own building clearing. Kit-first ordering
	// gives food and wood priority over ambient abundance without carving new terrain,
	// weakening the town budget, or depending on a particular AI clearing the crop wall.
	// Guaranteed small starter patches are deliberately retained at 0%. Keep them outside the
	// town, beside its rim on the upstream bank, so survival does not consume construction room.
	for (const auto &p : L.homes)
	{
		const double u = L.transpose ? p.y : p.x, v = L.transpose ? p.x : p.y;
		const auto wheat = L.point(u - 6, v - 17), wood = L.point(u + 6, v - 17);
		plantKit(
			map, t, context,
			{{int(wheat.x), int(wheat.y), 5}, {int(wood.x), int(wood.y), 5}, {0, 0, 0}, 18, 18, -1},
			[&](int i) { return !L.towns.plot[i] && clearGround(map, i % t.w, i / t.w); });
	}

	// Bank crops occupy short separated patches, not an unbroken wall. Town rims and coastal
	// beaches remain empty even at maximum abundance. Deposits near water can renew; the
	// protected towns supply lasting building ground if the rest of the bank later overgrows.
	int planted = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		if (L.towns.plot[i] || !clearGround(map, i % t.w, i / t.w))
			continue;
		const int u = L.transpose ? i / t.w : i % t.w;
		const int v = L.transpose ? i % t.w : i / t.w;
		const int patch = (u / 8 + v / 8) % 4;
		int type = -1, chance = 0;
		if (distance[i] >= 2 && distance[i] <= 10 && u % 16 < 10)
		{
			type = patch < 2 ? WHEAT : WOOD;
			chance = int(scaledCount(24, type == WHEAT ? o.wheat : o.wood));
		}
		else if (distance[i] > 10)
		{
			type = patch == 0 ? STONE : CHERRY + patch - 1;
			chance = int(scaledCount(2, type == STONE ? o.stone : o.fruit));
		}
		if (type >= 0 && context.bounded("braided-delta-resources", 100) < unsigned(chance) &&
			map.isResourceAllowed(i % t.w, i / t.w, type))
		{
			map.setResource(i % t.w, i / t.w, type, 1);
			++planted;
		}
	}
	seedAlgae(map, context, t, "braided-delta-algae", o.algae, AlgaeBand::anyWater(90));
	secureStartingCrops(game, context, t);
	context.telemetry.measure("braided-delta.ambient_tiles", planted);
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (const auto error = designMismatch(L, game.map, "braided delta"); !error.empty())
		return error;
	// Inspect the finished world: a terrain-only flood misses resource walls and buildings.
	const auto walk =
		walkFromFirstColony(game.map, context.request.nbTeams, "the delta", "over its fords");
	if (!walk.error.empty())
		return walk.error;
	// Sand rims contain outside crops, but cannot contain a seed planted inside a town.
	// Check after every resource repair so future guarantee changes cannot consume its room.
	if (cropSeedsIn(game.map, L.towns.plot))
		return "A delta town contains crops that could overgrow its building ground.";
	// Compare the complete finished shoreline, including fords. Reconstructing only map
	// dimensions would not detect an accidental repair that fills a channel or cuts a town.
	const auto grass = pureTiles(L.terrain, L.t, GRASS);
	const auto water = pureTiles(L.terrain, L.t, WATER);
	for (int i = 0; i < L.t.size(); ++i)
	{
		if (bool(grass[i]) != game.map.isGrass(i % L.t.w, i / L.t.w) ||
			bool(water[i]) != game.map.isWater(i % L.t.w, i / L.t.w))
			return "The delta shoreline or a crossing has changed.";
	}
	const auto buildable = buildableTiles(game.map);
	for (size_t k = 0; k < L.sites.size(); ++k)
	{
		std::vector<unsigned char> region(L.t.size(), 0);
		const auto p = L.sites[k];
		for (int dy = -10; dy < 10; ++dy)
			for (int dx = -10; dx < 10; ++dx)
			{
				const int i = L.t.at(int(p.x) + dx, int(p.y) + dy);
				region[i] = walk.steps[i] >= 0;
			}
		// These are overlapping legal origins, not 64 independent buildings. This floor checks
		// that both home and neutral islands retain a substantial accessible construction patch.
		if (buildSites(L.t, buildable, region) < 64)
			return "A delta island has lost its reachable building clearing.";
	}
	return "";
}
} // namespace

BraidedDeltaOptions::BraidedDeltaOptions(const GenerationRequest &r)
	: braids(r.option("braid-count")), rejoins(r.option("rejoining-frequency")),
	  islandSize(r.option("island-size")), crossingSpacing(r.option("crossing-spacing")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition braidedDeltaDefinition()
{
	return {"braided-delta",
			34,
			"Braided Delta",
			3,
			false,
			{{"braid-count", "Braid count", 2, 5, 1, 2, ControlGroup::Terrain},
			 {"rejoining-frequency", "Rejoining frequency", 1, 3, 1, 2, ControlGroup::Terrain},
			 {"island-size", "Island size", 32, 48, 8, 32, ControlGroup::Layout},
			 {"crossing-spacing", "Crossing spacing", 32, 96, 16, 64, ControlGroup::Layout},
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			designFailure<design>,
			validateWorld};
}
