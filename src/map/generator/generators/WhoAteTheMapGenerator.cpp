// SPDX-License-Identifier: GPL-3.0-or-later
#include "WhoAteTheMapGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Growth.h"
#include "Homes.h"
#include "Landmass.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Resources.h"
#include "Roads.h"
#include "Room.h"
#include "Sketch.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>
using namespace MapGeneration;

// A softly shaped island, chewed by a very large biscuit eater. The mouth is one cut with
// individual rounded tooth impressions along its back, not independent round bays. Its woods
// and pond clearings leave room to expand. This is deliberately asymmetric novelty geography:
// at the highest appetite bites can divide the island and colonies must build their own pools.
// Settlement and resource repairs may clear deposits, but never change the designed coastline.
namespace
{
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::string failure;
};
struct Tooth
{
	double along, across, radius;
};
struct Bite
{
	AxisFrame frame;
	double width, depth;
	double cosine, sine;
	std::vector<Tooth> teeth;
};

double roll(GenerationContext &c, const char *stream)
{
	return c.bounded(stream, 1000000) / 1000000.0;
}

std::string validateRequest(const GenerationRequest &r)
{
	const int shorter = std::min(r.wDec, r.hDec), longer = std::max(r.wDec, r.hDec);
	if (shorter < 7 || longer > 9 || longer - shorter > 1 || r.nbTeams > (shorter == 7 ? 4 : 8))
		return "Who Ate the Map? needs 128–512 tile sides, an aspect ratio at most 2:1, "
			   "and at most 4 colonies on a 128-tile side or 8 on larger maps.";
	return {};
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	Layout L;
	L.failure = validateRequest(request);
	if (!L.failure.empty())
		return L;
	const WhoAteTheMapOptions o(request);
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const double side = std::min(t.w, t.h), cx = t.w * 0.5, cy = t.h * 0.5;
	const double phase = roll(context, "eaten-outline") * 2 * kPi;
	const double phase2 = roll(context, "eaten-outline") * 2 * kPi;
	const auto radius = [&](double a)
	{ return 0.425 * (1 + 0.055 * std::sin(3 * a + phase) + 0.035 * std::cos(2 * a + phase2)); };
	std::vector<unsigned char> land(t.size(), 0);
	int original = 0;
	for (int i = 0; i < t.size(); ++i)
	{
		const double x = (i % t.w - cx) / t.w, y = (i / t.w - cy) / t.h;
		land[i] = std::hypot(x, y) < radius(std::atan2(y, x));
		original += land[i];
	}
	const double heading = roll(context, "eaten-bites") * 2 * kPi;
	const int count = 2 + o.appetite;
	std::vector<Bite> bites;
	for (int b = 0; b < count; ++b)
	{
		// Neighbouring large chomps can meet at the highest appetite. Other bites are smaller;
		// the survivor is several chunks of the original island, never a manufactured archipelago.
		const double a = heading +
						 (b == 1   ? (o.appetite == 2 ? 2.0 : kPi)
						  : b == 2 ? (o.appetite == 2 ? 4.05 : 1.55)
						  : b == 3 ? 5.1
								   : 0.0) +
						 (roll(context, "eaten-bites") - 0.5) * 0.22;
		const int profile = context.bounded("eaten-bites", 3);
		const double edge = radius(a);
		const double x = cx + t.w * edge * std::cos(a), y = cy + t.h * edge * std::sin(a);
		const double inward = std::atan2(cy - y, cx - x);
		// Broad jaws keep their individual teeth visible even when two bites meet. A deep
		// narrow ellipse instead leaves long straight-looking sides, like a knife cut.
		const double jaw = side * (o.appetite == 2 ? (b < 2 ? 0.245 : 0.135) : 0.185);
		double shift = side * (o.appetite == 0 ? -0.095 : -0.045);
		if (o.appetite == 2 && b < 2)
			shift = side * 0.105;
		if (b >= 2)
			shift = -side * 0.09;
		const double depth = jaw * (profile == 0 ? 0.94 : 1.0);
		Bite bite{{x + shift * std::cos(inward), y + shift * std::sin(inward), inward},
				  jaw,
				  depth,
				  std::cos(inward),
				  std::sin(inward),
				  {}};
		const int teeth = (side == 128 && o.appetite < 2) ? 4 : 6;
		const double toothRadius = jaw * std::sin(kPi / (2 * teeth)) * 1.04;
		for (int j = 0; j < teeth; ++j)
		{
			const double theta = -kPi / 2 + kPi * (j + 0.5) / teeth;
			const double r = toothRadius * (0.93 + 0.12 * roll(context, "eaten-bites"));
			const double skew = profile == 2 ? 0.07 * std::sin(theta) : 0;
			bite.teeth.push_back({(depth - 0.32 * r) * std::cos(theta) * (1 + skew),
								  (jaw - 0.32 * r) * std::sin(theta), r});
		}
		bites.push_back(bite);
		context.telemetry.choice("eaten.bite.profile",
								 profile == 0   ? "shallow"
								 : profile == 1 ? "deep"
												: "lopsided",
								 b);
		context.telemetry.measure("eaten.bite.depth", depth, b);
		context.telemetry.measure("eaten.bite.teeth", teeth, b);
	}
	int bitten = 0;
	for (int i = 0; i < t.size(); ++i)
		if (land[i])
			for (const Bite &b : bites)
			{
				// The mouth's rotation is fixed for the whole raster, not recalculated per tile.
				const double dx = i % t.w - b.frame.x, dy = i / t.w - b.frame.y;
				const ShapePoint p{dx * b.cosine + dy * b.sine, -dx * b.sine + dy * b.cosine};
				bool cut = p.x * p.x / (b.depth * b.depth) + p.y * p.y / (b.width * b.width) <= 1;
				for (const Tooth &tooth : b.teeth)
				{
					const double dx = p.x - tooth.along, dy = p.y - tooth.across;
					cut |= dx * dx + dy * dy <= tooth.radius * tooth.radius;
				}
				if (cut)
				{
					land[i] = 0;
					++bitten;
					break;
				}
			}
	// Drop uninhabitable specks without rounding the tooth impressions away.
	land = cleanLandmass(t, land, {32, 0, 1});
	if (o.appetite < 2)
		land = largestRegion(t, land);
	L.terrain.assign(t.size(), WATER);
	for (int i = 0; i < t.size(); ++i)
		if (land[i])
			L.terrain[i] = GRASS;
	std::vector<unsigned char> sea(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		sea[i] = !land[i];
	const auto shore = stepsFrom(t, sea);
	// Ponds belong to the landscape, not one identical pond per colony. Keep the coast intact.
	const auto ponds = spreadPoints(t, side == 128 ? 32 : 42, context, "eaten-pond-sites");
	int pondCount = 0;
	for (const auto &p : ponds)
		if (shore[t.at(p.x, p.y)] > 13)
		{
			const RadialShape pond(3.5 + 2 * roll(context, "eaten-pond-shape"), 0.22, context,
								   "eaten-pond-shape");
			forEachTileInShape(t, p.x, p.y, pond, 0,
							   [&](int i, double, double) { L.terrain[i] = WATER; });
			++pondCount;
		}
	layBeaches(L.terrain, t);
	context.telemetry.measure("eaten.bites.placed", bites.size());
	context.telemetry.measure("eaten.land.original", original);
	context.telemetry.measure("eaten.land.bitten", bitten);
	context.telemetry.measure("eaten.land.removed-percent", 100.0 * bitten / original);
	context.telemetry.measure("eaten.ponds.placed", pondCount);
	return L;
}

// A detached colony must fit its town and swimming infrastructure on its own island.
// Overlapping origins exaggerate room on narrow crescents: count spaced footprints too.
struct IslandCapacity
{
	std::vector<int> labels, capacity;
	std::vector<unsigned char> service;
	int mainland = 0;
};

std::vector<int> spacedPlots(const Torus &t, const std::vector<unsigned char> &usable,
							 const std::vector<int> &labels)
{
	const auto origins = buildAnchors(t, usable);
	std::vector<unsigned char> used(t.size(), 0);
	std::vector<int> counts(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		if (origins[i])
		{
			bool clear = true;
			for (int dy = 0; dy < 6; ++dy)
				for (int dx = 0; dx < 6; ++dx)
					clear &= !used[t.at(i % t.w + dx, i / t.w + dy)];
			if (!clear)
				continue;
			++counts[labels[i]];
			for (int dy = 0; dy < 6; ++dy)
				for (int dx = 0; dx < 6; ++dx)
					used[t.at(i % t.w + dx, i / t.w + dy)] = 1;
		}
	return counts;
}

IslandCapacity islandCapacity(const Layout &L)
{
	const Torus &t = L.t;
	const auto grass = pureTiles(L.terrain, t, GRASS);
	const auto water = pureTiles(L.terrain, t, WATER);
	std::vector<unsigned char> open(t.size());
	for (int i = 0; i < t.size(); ++i)
		open[i] = !water[i];
	IslandCapacity result;
	result.labels = connectedRegions(open, t.w, t.h, true, GridNeighbors::Eight);
	std::vector<int> area(t.size(), 0), inland(t.size(), 0);
	result.service.resize(t.size());
	result.capacity.resize(t.size());
	for (int i = 0; i < t.size(); ++i)
		if (grass[i])
			++area[result.labels[i]];
	result.mainland = std::max_element(area.begin(), area.end()) - area.begin();
	result.capacity[result.mainland] = 8;
	bool detachedTown = false;
	for (int i = 0; i < t.size(); ++i)
		detachedTown |= i != result.mainland && area[i] >= 1200;
	// Most maps have no island large enough to settle. Their mainland never uses
	// this swimming-space budget, so skip two distance fields and footprint packing.
	if (!detachedTown)
		return result;
	std::vector<unsigned char> sand(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		for (int dy = 0; dy <= 1; ++dy)
			for (int dx = 0; dx <= 1; ++dx)
				sand[i] |= L.terrain[t.at(i % t.w + dx, i / t.w + dy)] == SAND;
	const auto dw = stepsFrom(t, water), ds = stepsFrom(t, sand);
	for (int i = 0; i < t.size(); ++i)
		if (grass[i])
		{
			const int label = result.labels[i];
			result.service[i] = dw[i] >= 6 && ds[i] >= 2;
			inland[label] += result.service[i];
		}
	const auto plots = spacedPlots(t, result.service, result.labels);
	for (int i = 0; i < t.size(); ++i)
		result.capacity[i] = std::min({area[i] / 1200, inland[i] / 400, plots[i] / 10});
	result.capacity[result.mainland] = 8;
	return result;
}

std::vector<int> chooseSites(const Layout &L, GenerationContext &context)
{
	const Torus &t = L.t;
	const auto grass = pureTiles(L.terrain, t, GRASS);
	const auto room = buildAnchors(t, grass, 13);
	std::vector<unsigned char> open(t.size(), 0);
	const auto water = pureTiles(L.terrain, t, WATER);
	for (int i = 0; i < t.size(); ++i)
		open[i] = !water[i];
	const auto islands = islandCapacity(L);
	const auto &labels = islands.labels;
	std::vector<int> sizes(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		if (open[i])
			++sizes[labels[i]];
	const auto fertility = meanFertilityField(cropGrowthField(L.terrain, t), t, 12);
	std::vector<int> candidates;
	for (int i = 0; i < t.size(); ++i)
		if (room[t.at(i % t.w - 6, i / t.w - 6)] && fertility[i] >= 1900 &&
			sizes[labels[i]] >= 900 && islands.capacity[labels[i]] > 0)
			candidates.push_back(i);
	if (candidates.empty())
		return {};
	const auto origins = buildAnchors(t, grass);
	std::vector<signed char> checked(t.size(), -1);
	const auto suitable = [&](int i)
	{
		if (checked[i] < 0)
		{
			int ground = 0, plots = 0;
			for (int j : reachFrom(t, {i}, open, 24).tiles)
			{
				ground += grass[j];
				plots += origins[j];
			}
			checked[i] = ground >= 650 && plots >= 280;
		}
		return checked[i] != 0;
	};
	// A greedy spread can paint itself into a corner on a crescent. Retry the first
	// candidate, not the island or its resource settings, before declaring it too small.
	const auto spread = [&](size_t first, int separation)
	{
		std::vector<int> sites;
		std::vector<int> occupants(t.size(), 0);
		std::vector<int> distance(t.size(), std::numeric_limits<int>::max());
		int chosen = -1;
		for (size_t j = 0; j < candidates.size(); ++j)
			if (suitable(candidates[(first + j) % candidates.size()]))
			{
				chosen = candidates[(first + j) % candidates.size()];
				break;
			}
		if (chosen < 0)
			return std::vector<int>{};
		for (int k = 0; k < context.request.nbTeams; ++k)
		{
			if (k)
			{
				long long best = -1;
				chosen = -1;
				for (int i : candidates)
				{
					if (occupants[labels[i]] >= islands.capacity[labels[i]])
						continue;
					int direct = t.size();
					for (int s : sites)
						direct = std::min(direct, t.dist2(i % t.w, i / t.w, s % t.w, s / t.w));
					if (direct < separation * separation)
						continue;
					const int d = std::min(distance[i], int(std::sqrt(double(direct))) * 2);
					const long long score = 1LL * d * (3000 + std::min(6000u, fertility[i]));
					if (score > best && suitable(i))
					{
						best = score;
						chosen = i;
					}
				}
				if (chosen < 0)
					return std::vector<int>{};
			}
			sites.push_back(chosen);
			++occupants[labels[chosen]];
			const auto walk = stepsFrom(t, tileMask(t, {chosen}), open);
			for (int i : candidates)
				if (walk[i] >= 0)
					distance[i] = std::min(distance[i], walk[i]);
		}
		return sites;
	};
	std::vector<int> sites;
	int trials = 0;
	while (trials++ < 48)
	{
		sites = spread(context.bounded("eaten-starts", candidates.size()), trials <= 24 ? 24 : 20);
		if (sites.size() == size_t(context.request.nbTeams))
			break;
	}
	context.telemetry.measure("eaten.starts.trials", std::min(trials, 48));
	if (sites.size() != size_t(context.request.nbTeams))
		return {};

	if (trials > 24 && sites.size() == size_t(context.request.nbTeams))
		context.telemetry.fallback(
			"eaten.starts.crowded",
			"Crescent needs twenty-tile spacing; farm and town capacity unchanged");
	std::set<int> occupied;
	for (int s : sites)
		occupied.insert(labels[s]);
	context.telemetry.measure("eaten.starts.components", occupied.size());
	context.telemetry.measure("eaten.starts.candidates", candidates.size());
	dealStarts(context, sites, "eaten-start-deal");
	return sites;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "bitten island";
	const auto L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const auto &t = L.t;
	const WhoAteTheMapOptions o(context.request);
	const auto sites = chooseSites(L, context);
	if (sites.size() != size_t(context.request.nbTeams))
	{
		context.detail = "The surviving island has too few spacious fertile starts.";
		return false;
	}
	Map &map = game.map;
	writeUndermap(map, L.terrain);
	const auto grass = pureTiles(L.terrain, t, GRASS);
	for (int k = 0; k < context.request.nbTeams; ++k)
		game.addTeam();
	if (!settleColonies(
			game, context, "eaten-settlement", [&](int) { return grass; },
			[&](int k) { return MapGeneratorPoint(sites[k] % t.w - 2, sites[k] / t.w - 2); }))
		return false;
	const auto bare = walkableTiles(map);
	const auto labels = connectedRegions(bare, t.w, t.h, true, GridNeighbors::Eight);
	const auto units = unitTilesByTeam(map, context.request.nbTeams);
	const auto fertility = Fertility::forMap(map, false);
	std::vector<unsigned char> reserved = swarmSurroundings(t, context), ambient(t.size(), 1);
	for (int s : sites)
		for (int dy = -14; dy <= 14; ++dy)
			for (int dx = -14; dx <= 14; ++dx)
			{
				const int i = t.at(s % t.w + dx, s / t.w + dy);
				if (dx * dx + dy * dy <= 14 * 14)
					ambient[i] = 0;
				if (dx * dx + dy * dy < 6 * 6)
					reserved[i] = 1;
			}
	context.stage = "shore and clearing farms";
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		const int s = sites[k];
		const auto walk = stepsFrom(t, tileMask(t, units[k]), bare);
		for (int type : {WHEAT, WOOD})
		{
			const auto kit =
				growPatchesNear(map, t, s % t.w, s / t.w, 20, type, type == WHEAT ? 100 : 65,
								[&](int i)
								{
									return !reserved[i] && walk[i] >= 0 && walk[i] <= 24 &&
										   fertility.at(i % t.w, i / t.w) > 0 &&
										   clearGround(map, i % t.w, i / t.w);
								});
			context.telemetry.measure(type == WHEAT ? "eaten.home.wheat" : "eaten.home.wood",
									  kit.tiles, k);
		}
		plantOpenHomeKit(map, t, context, {double(s % t.w), double(s / t.w)}, 0, 12, 0, 0, 1,
						 [&](int i)
						 {
							 return !reserved[i] && walk[i] >= 0 && walk[i] <= 24 &&
									clearGround(map, i % t.w, i / t.w);
						 });
	}
	context.stage = "wooded heartland";
	const auto patch = periodicNoise(t.w, t.h, 14, context.stream("eaten-woodland"));
	const auto split = periodicNoise(t.w, t.h, 8, context.stream("eaten-fields"));
	// Finite inland reserves form woods too: fertility alone would paint only a coastal fringe.
	std::vector<int> woodOrder;
	for (int i = 0; i < t.size(); ++i)
		if (ambient[i] && !reserved[i] && clearGround(map, i % t.w, i / t.w))
			woodOrder.push_back(i);
	std::stable_sort(woodOrder.begin(), woodOrder.end(),
					 [&](int a, int b) { return patch[a] > patch[b]; });
	const int woodBudget =
		std::min(int(woodOrder.size() * 0.58), int(scaledCount(woodOrder.size() / 5, o.wood)));
	for (int j = 0; j < woodBudget; ++j)
		map.setResource(woodOrder[j] % t.w, woodOrder[j] / t.w, WOOD, 1);
	furnishGround(
		map, t, context, fertility,
		[&](int i) { return ambient[i] && !reserved[i] && clearGround(map, i % t.w, i / t.w); },
		[&](int i) { return float(split[i]); }, [&](int i) { return patch[i]; },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(area / 12, o.wheat)), 0,
								 int(scaledCount(area / 850, o.stone)),
								 int(scaledCount(area / 1100, o.fruit))};
		},
		"eaten-stone", "eaten-fruit");
	seedAlgae(map, context, t, "eaten-algae", o.algae, AlgaeBand::shallows(1, 6, 60));
	openCrampedStarts(game, context, 48, 24);
	secureStartingCrops(game, context, t);
	// Clear paths on existing land only, including an exit to the beach on every inhabited island.
	std::vector<unsigned char> coast(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		coast[i] = bare[i] && !grass[i];
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		if (!openRoad(map, t, units[k], coast))
			return false;
		for (int j = 0; j < k; ++j)
			if (labels[units[j][0]] == labels[units[k][0]] &&
				!openRoad(map, t, units[k], tileMask(t, units[j])))
				return false;
	}
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const auto L = design(context.request, replay);
	if (!L.failure.empty())
		return L.failure;
	const auto &t = L.t;
	for (int i = 0; i < t.size(); ++i)
		if (game.map.getUMTerrain(i % t.w, i / t.w) != L.terrain[i])
			return "Settlement changed the bitten coastline or ponds.";
	const auto buildable = buildableTiles(game.map);
	const auto anchors = buildAnchors(t, buildable);
	const auto islands = islandCapacity(L);
	auto service = islands.service;
	for (int i = 0; i < t.size(); ++i)
		service[i] &= buildable[i];
	const auto servicePlots = spacedPlots(t, service, islands.labels);
	std::vector<int> occupants(t.size(), 0);
	const auto units = unitTilesByTeam(game.map, context.request.nbTeams);
	const auto open = walkableTiles(game.map);
	const auto fertility = Fertility::forMap(game.map, false);
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		const int component = islands.labels[units[k][0]];
		if (component != islands.mainland &&
			(++occupants[component] > islands.capacity[component] ||
			 servicePlots[component] < 2 * occupants[component]))
			return "A detached colony lacks local swimming infrastructure space.";
		const auto reach = floodFrom(t, tileMask(t, units[k]), open, 24);
		int room = 0;
		std::set<int> wheat, wood;
		int fertileWheat = 0, fertileWood = 0;
		for (int i : reach.visited)
		{
			room += anchors[i];
			for (const auto &d : kCardinalSteps)
			{
				const int type =
					game.map.getResource(t.x(i % t.w + d[0]), t.y(i / t.w + d[1])).type;
				const int j = t.at(i % t.w + d[0], i / t.w + d[1]);
				if (type == WHEAT && wheat.insert(j).second && fertility.at(j % t.w, j / t.w) > 0)
					++fertileWheat;
				if (type == WOOD && wood.insert(j).second && fertility.at(j % t.w, j / t.w) > 0)
					++fertileWood;
			}
		}
		if (room < 40 || wheat.size() < 12 || wood.size() < 8 || fertileWheat < 4 ||
			fertileWood < 4)
			return "An island colony lacks accessible crops or construction room.";
	}
	const auto swimming = groundUnitTiles(game.map, true);
	const auto reach = stepsFrom(t, tileMask(t, units[0]), swimming);
	for (const auto &team : units)
		if (reach[team[0]] < 0)
			return "An island colony cannot reach its rivals even with swimming.";
	if (context.request.option("appetite") < 2)
		return walkFromFirstColony(game.map, context.request.nbTeams, "the bitten island", "")
			.error;
	return {};
}
} // namespace
WhoAteTheMapOptions::WhoAteTheMapOptions(const GenerationRequest &r)
	: appetite(r.option("appetite")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition whoAteTheMapDefinition()
{
	return {"who-ate-the-map",
			66,
			"Who Ate the Map?",
			1,
			false,
			{GeneratorControl::choice("appetite", "Appetite",
									  {"A Little Nibble", "Hungry", "Who Ate the Map?"}, 1),
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount"),
			 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
			generate,
			true,
			validateRequest,
			validateWorld,
			{"terrain:novelty", "feature:novelty-shapes", "feature:forest", "feature:islands",
			 "feature:lakes", "style:tight-building"}};
}
