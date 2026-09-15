// SPDX-License-Identifier: GPL-3.0-or-later
#include "RiceTerracesGenerator.h"
#include "Farmland.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Points.h"
#include "Roads.h"
#include "Room.h"
#include "Settlements.h"
#include "Sketch.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
using namespace MapGeneration;

// Rice terraces: each colony farms the concentric contours around its own hill. Sand stairs
// cross the fields and irrigation ditches; the summit is a town, the valleys are shared ground.
// These are terrain contours, not engine elevation: towers shoot across them without a height
// bonus. A summit tower covers a stair's final approach, not the whole climb from the valley.
// Swimming bypasses the water barriers, and harvesting can open crop bands, but neither can
// grow crops across the sand separating the town, the stairs and the commons from the farms.
namespace
{
// Radius 16 leaves room for a swarm, upgrade footprints and several ordinary AI buildings.
// Keep it fixed rather than squeezing the town to accommodate an overfull request. Two sand
// corners plus the mixed boundary tiles prevent eight-neighbour crop growth entering it.
constexpr double kSummit = 16;
constexpr double kCap = 2;
constexpr double kValley = 14;
// A stair is five corners across even on its narrow axis. At arbitrary angles its centre
// retains multiple walkable tiles after four-corner terrain conversion. Towers sit inside
// the summit, to one side of the mouth, never on the stair itself.
constexpr double kStairHalf = 2.5;
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<ShapePoint> hills;
	std::vector<int> homeOf, hillOf;
	std::vector<unsigned char> stairs, valley;
	Farm farm;
	double radius = 0, phase = 0;
	int bands = 0;
	std::string failure;
};

Layout design(const GenerationRequest &r, GenerationContext &context)
{
	const RiceTerracesOptions o(r);
	Layout L;
	L.t = {1 << r.wDec, 1 << r.hDec};
	const Torus &t = L.t;
	const int n = t.size(), hills = r.nbTeams + o.extraHills;
	// Sequence draws explicitly: C++ does not order function arguments, so two
	// bounded() calls inside latticeSites(...) can swap x/y between compilers.
	const int originX = int(context.bounded("rice-layout", t.w));
	const int originY = int(context.bounded("rice-layout", t.h));
	L.hills = latticeSites(t.w, t.h, hills, originX, originY).sites;
	// Deal before assigning owners: the unoccupied hills and odd-count lattice positions must
	// not always favour the same team index. This is comparable spacing, not exact tile symmetry.
	dealStarts(context, L.hills);
	const double separation = std::min<double>(nearestSiteDistance(t, L.hills), std::min(t.w, t.h));
	const double budget = std::min<double>(o.hillRadius, (separation - kValley) / 2);
	// A circle contains every row angle. Use the axial yield fit as a single radial
	// period, rather than changing width by angle and breaking the concentric silhouette.
	const FarmRows fit = bestFarmRows(0);
	const FarmRows rows{fit.crops * o.bandWidth / 100, fit.water * o.bandWidth / 100};
	L.bands = int(std::floor((budget - kSummit - 2 * kCap) / rows.period()));
	if (L.bands < 1)
	{
		L.failure = "Rice terraces need room for a summit, a full crop/water band and valleys; "
					"use a larger map, fewer hills, narrower bands or a larger hill radius.";
		return L;
	}
	// Finish at a complete water band and an outer sand cap. Never leave a truncated crop
	// band connected to valley grass: it would spread into the commons during a long game.
	L.radius = kSummit + 2 * kCap + L.bands * rows.period();
	L.phase = context.bounded("rice-stairs", 3600) * (2 * kPi / 3600);
	L.terrain.assign(n, GRASS);
	L.homeOf.assign(n, -1);
	L.hillOf.assign(n, -1);
	L.valley.assign(n, 1);
	// The reusable farm operation owns row/cap/crossing geometry. This generator
	// supplies the fitted budget and later assigns summit and valley ownership.
	const ContourFarmStyle style{rows, kSummit, kCap, L.bands, o.stairs, kStairHalf, L.phase};
	ContourFarm contours = layContourFarm(L.terrain, t, L.hills, style);
	L.farm = std::move(contours.farm);
	L.stairs = std::move(contours.crossings);
	// The river follows Voronoi valley boundaries: points almost equally close to two hills.
	// Periodic sand fords interrupt it every 24 tiles on both axes, so the river changes
	// valley approaches without partitioning the torus. One hill has no inter-hill river.
	std::vector<Site> sites;
	for (const ShapePoint &hill : L.hills)
		sites.push_back({int(hill.x), int(hill.y)});
	int riverCorners = 0;
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		const NearestSites nearest = nearestTwoSites(t, sites, x, y);
		const int h = nearest.first;
		const double first = std::sqrt(double(nearest.firstDistanceSquared));
		const double second = nearest.second < 0 ? first + t.w + t.h
												 : std::sqrt(double(nearest.secondDistanceSquared));
		if (first > L.radius)
		{
			if (o.river && hills > 1 && second - first < 3 && first > L.radius + 3)
			{
				const bool ford = x % 24 < 5 || y % 24 < 5;
				L.terrain[i] = ford ? SAND : WATER;
				riverCorners += !ford;
			}
			continue;
		}
		L.valley[i] = 0;
		L.hillOf[i] = h;
		if (first < kSummit)
		{
			if (h < r.nbTeams)
				L.homeOf[i] = h;
			continue;
		}
	}
	layBeaches(L.terrain, t);
	context.telemetry.measure("rice.hills.actual", hills);
	context.telemetry.measure("rice.hills.radius-budget", budget);
	context.telemetry.measure("rice.hills.radius-actual", L.radius);
	context.telemetry.measure("rice.bands.per-hill", L.bands);
	context.telemetry.measure("rice.bands.crop-width", rows.crops);
	context.telemetry.measure("rice.bands.water-width", rows.water);
	context.telemetry.measure("rice.stairs.per-hill", o.stairs);
	context.telemetry.measure("rice.stairs.phase-radians", L.phase);
	context.telemetry.measure("rice.river.water-corners", riverCorners);
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "rice terraces layout";
	const RiceTerracesOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	const Torus &t = L.t;
	Map &map = game.map;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();
	writeUndermap(map, L.terrain);
	context.stage = "rice terraces summits";
	const auto home = [&](int k)
	{
		std::vector<unsigned char> mask(n, 0);
		for (int i = 0; i < n; ++i)
			mask[i] = L.homeOf[i] == k && map.isGrass(i % t.w, i / t.w);
		return mask;
	};
	const auto anchor = [&](int k)
	{ return MapGeneratorPoint(int(L.hills[k].x) - 2, int(L.hills[k].y) - 2); };
	if (!settleColonies(game, context, "rice-starts", home, anchor))
		return false;
	context.stage = "rice terraces first meal";
	for (int k = 0; k < teams; ++k)
	{
		// The first feeding deadline arrives before some AI openings finish
		// building AND filling an inn across the summit cap. A small completed
		// inn supplies that first meal, not an ongoing food subsidy. It sits
		// halfway toward the first stair, leaving both its mouth and the swarm
		// margins free. Fruit remains exclusively a valley reward.
		if (placeStartingBuilding(game, k, "inn", 0, L.hills[k].x + 7 * std::cos(L.phase),
								  L.hills[k].y + 7 * std::sin(L.phase), 3, home(k), {WHEAT}) < 0)
		{
			context.detail = "A summit has no room for its starter inn.";
			return false;
		}
	}
	context.telemetry.measure("rice.inns.per-colony", 1);
	context.stage = "rice terraces defenses";
	// Place one tower beside EACH mouth rather than scoring all stairs together (which can
	// concentrate every tower at one entrance). Search only three tiles around its designed
	// position. Failure is explicit: silently dropping one would leave one colony exposed.
	for (int k = 0; k < teams && o.towers > 0; ++k)
		for (int s = 0; s < o.stairs; ++s)
		{
			const double angle = L.phase + s * 2 * kPi / o.stairs;
			const double radial = kSummit - 3;
			const double x = L.hills[k].x + radial * std::cos(angle) - 2 * std::sin(angle);
			const double y = L.hills[k].y + radial * std::sin(angle) + 2 * std::cos(angle);
			// The whole upper stair width must be covered, not just its centre.
			// The shared helper filters candidates by the actual building's range.
			std::vector<MapGeneratorPoint> mouth;
			for (int side = -2; side <= 2; ++side)
				mouth.emplace_back(int(std::lround(L.hills[k].x + (kSummit + 1) * std::cos(angle) -
												   side * std::sin(angle))),
								   int(std::lround(L.hills[k].y + (kSummit + 1) * std::sin(angle) +
												   side * std::cos(angle))));
			// Three unsupplied towers recruit three of the default four workers
			// for stone before the first inn has food. Playtest telemetry exposed
			// early Cortex losses during this opening. Give each defensive tower
			// its finite reserve as well as its magazine; no ongoing income or
			// combat rules change, and later replenishment remains the town's job.
			if (placeTower(game, k, o.towers - 1, x, y, 3, home(k), true, mouth, true) < 0)
			{
				context.detail = "A summit stair has no room for a tower covering its mouth.";
				return false;
			}
		}
	context.telemetry.measure("rice.towers.per-colony", o.towers > 0 ? o.stairs : 0);
	context.telemetry.choice("rice.towers.supplies", "magazine-and-stone-reserve");
	context.stage = "rice terraces crops";
	const auto free = [&](int i) { return clearGround(map, i % t.w, i / t.w); };
	// Seed food beside EVERY stair, on the inner edge of the first crop band.
	// A single starter patch made a summit inn's opening haul depend on which
	// side of town the AI chose. Telemetry showed an empty first inn and early
	// losses even after the tower delivery jobs were removed. Three small near
	// patches give alternative gathering edges without filling the summit or
	// changing the permanent stairs. Wood needs only one opening patch; its
	// faster spread must not compete with wheat at every entrance.
	for (int k = 0; k < teams; ++k)
	{
		const auto eligible = [&](int i)
		{ return L.hillOf[i] == k && L.farm.row[i] == 0 && free(i); };
		int starterWheat = 0;
		for (int stair = 0; stair < o.stairs; ++stair)
		{
			const KitFrame frame{int(L.hills[k].x), int(L.hills[k].y),
								 L.phase + stair * 2 * kPi / o.stairs};
			// Two corners into the crop band is close to home but leaves the
			// containment cap intact. The five-corner side offset clears the
			// sand stair even after corner-to-tile conversion; search stays in
			// row zero, and a shortfall is explicit rather than planting in town.
			const int wheat =
				plantPatchNear(map, t, frame.at(kSummit + kCap + 2, -5, 5), WHEAT, 12, eligible);
			if (wheat < 8)
			{
				context.detail = "A stair's starter wheat plot does not fit.";
				return false;
			}
			starterWheat += wheat;
		}
		const KitFrame woodFrame{int(L.hills[k].x), int(L.hills[k].y), L.phase};
		if (plantPatchNear(map, t, woodFrame.at(kSummit + kCap + 5, 6, 5), WOOD, 12, eligible) < 8)
		{
			context.detail = "A stair's starter wood plot does not fit.";
			return false;
		}
		context.telemetry.measure("rice.starter.wheat-tiles", starterWheat, k);
		// One permanent quarry tile inside the town guarantees early upgrades. Unlike
		// wheat and wood it never spreads, so it cannot engulf summit building space.
		const int quarry = seedNear(t, int(L.hills[k].x) + 8, int(L.hills[k].y), 3,
									[&](int i) { return L.homeOf[i] == k && free(i); });
		if (quarry < 0)
		{
			context.detail = "A summit quarry does not fit.";
			return false;
		}
		map.setResource(quarry % t.w, quarry / t.w, STONE, 1);
	}
	// Separate each hill's budget so raster ordering cannot give one hill all the wood.
	// A small starter guarantee survives 0%; all additional terrace crops scale normally.
	// The guarantee is ON the terrace, outside the summit containment ring.
	for (int k = 0; k < int(L.hills.size()); ++k)
	{
		int area = 0;
		for (int i = 0; i < n; ++i)
			area += L.hillOf[i] == k && L.farm.row[i] >= 0 && L.farm.row[i] % 2 == 0 &&
					map.isGrass(i % t.w, i / t.w);
		const int wheat = int(scaledCount(area * 45 / 100, o.wheat));
		const int wood = int(scaledCount(area * 8 / 100, o.wood));
		const int actual = plantFarm(map, t, L.farm, wheat, wood,
									 [&](int i) { return L.hillOf[i] == k && free(i); });
		context.telemetry.measure("rice.crops.requested", wheat + wood, k);
		context.telemetry.measure("rice.crops.placed", actual, k);
		if (actual < wheat + wood)
			context.telemetry.fallback("rice.crops.saturated", "Available crop row grass filled.",
									   k);
	}
	// Only persistent fruit and quarry outcrops in the commons. Wheat and the faster wood
	// stay within capped terraces; valley towns therefore retain their expansion room.
	std::vector<int> commons;
	for (int i = 0; i < n; ++i)
		if (L.valley[i] && free(i))
			commons.push_back(i);
	for (int type : {STONE, CHERRY})
	{
		const bool quarry = type == STONE;
		const int wanted =
			int(scaledCount(int(commons.size()) / (quarry ? 1200 : 600) + (quarry ? 1 : 3),
							quarry ? o.stone : o.fruit));
		const char *stream = quarry ? "rice-stone" : "rice-fruit";
		const int actual = scatterClumps(
			context, t, commons, wanted, stream, free,
			[&](MapGeneratorPoint p)
			{
				placeResourceClump(map, context, p,
								   quarry ? STONE : CHERRY + int(context.bounded(stream, 3)), 1);
			});
		context.telemetry.measure(quarry ? "rice.quarries.requested" : "rice.groves.requested",
								  wanted);
		context.telemetry.measure(quarry ? "rice.quarries.placed" : "rice.groves.placed", actual);
		if (actual < wanted)
			context.telemetry.fallback("rice.commons.saturated",
									   "Valley deposit placement exhausted its bounded search.",
									   type);
	}
	seedAlgae(map, context, t, "rice-algae", o.algae, AlgaeBand::anyWater(100));
	// The shared guarantee can supply an awkwardly rasterized start. It does not alter
	// terrain or create another stair. The finished summit room and every sand stair are
	// checked below, so a repair cannot silently erase the map's defining geometry.
	// The generic first-crop rescue may plant a radius-two clump on ANY reachable
	// grass. In a contour map that includes the dry summit town. On sparse-wood,
	// dense-wheat combinations the wood rescue really runs: bulk seeds 34101
	// showed its clump spilling into protected building ground. Supply the
	// shared topup mask rather than repairing the spill after the fact. Every
	// tile of an emergency clump must stay on a crop row of some hill; sand
	// stairs, caps, towns, water rows and valley commons remain crop-free.
	std::vector<unsigned char> cropTopupGround(n, 0);
	for (int i = 0; i < n; ++i)
		cropTopupGround[i] = L.hillOf[i] >= 0 && L.farm.row[i] >= 0 &&
								L.farm.row[i] % 2 == 0 && !L.stairs[i];
	secureStartingCrops(game, context, t, 24, 32, 0, nullptr, &cropTopupGround);
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	if (const std::string mismatch = designMismatch(L, game.map, "rice terraces");
		!mismatch.empty())
		return mismatch;
	const Map &map = game.map;
	const Torus &t = L.t;
	const auto open = walkableTiles(map);
	const auto buildable = buildableTiles(map);
	const auto workers = unitTilesByTeam(map, context.request.nbTeams);
	// Check final tiles, not merely the corner sketch: the complete sand cores of stairs
	// must survive beaches, planting, colony placement and all later repair stages.
	const auto sand = pureTiles(L.terrain, t, SAND);
	for (int i = 0; i < t.size(); ++i)
		if (L.stairs[i] && sand[i] && (!open[i] || !map.isSand(i % t.w, i / t.w)))
			return "A rice terrace stair is blocked.";
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		std::vector<unsigned char> home(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			home[i] = L.homeOf[i] == k;
		// Test crop containment as a future footprint, not a lucky initial planting.
		// Any eight-connected PURE grass component reachable from the summit must be
		// disjoint from terrace crop rows. Water and sand remain barriers to growth
		// even after every crop and building is removed.
		const auto grass = pureTiles(L.terrain, t, GRASS);
		const auto fromHome = stepsFrom(t, home, grass);
		const auto fromWorkers = stepsFrom(t, tileMask(t, workers[k]), open);
		for (int i = 0; i < t.size(); ++i)
		{
			if (fromHome[i] >= 0 && L.farm.row[i] >= 0)
				return "Terrace crops could grow into a summit.";
			if (home[i] && map.isResource(i % t.w, i / t.w) &&
				(map.getResource(i % t.w, i / t.w).type == WHEAT ||
				 map.getResource(i % t.w, i / t.w).type == WOOD))
				return "A crop repair entered the protected summit.";
			if (L.hillOf[i] == k && L.stairs[i] && sand[i] && fromWorkers[i] < 0)
				return "A colony cannot walk the full length of every stair.";
		}
		// These overlap: 100 anchors are a generous contiguous budget, not 100 buildings.
		if (buildSites(t, buildable, home) < 100)
			return "A rice terrace summit lacks building room.";
	}
	return walkFromFirstColony(map, context.request.nbTeams, "rice terraces",
							   "along the stairs and valley fords")
		.error;
}
} // namespace
RiceTerracesOptions::RiceTerracesOptions(const GenerationRequest &r)
	: extraHills(r.option("extra-hills")), hillRadius(r.option("hill-radius")),
	  bandWidth(r.option("band-width")), stairs(r.option("stairs")),
	  towers(r.option("starting-towers")), river(r.option("valley-river") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}
GeneratorDefinition riceTerracesDefinition()
{
	return {"rice-terraces",
			46,
			"Rice terraces",
			5,
			false,
			{{"extra-hills", "Unoccupied hills", 0, 4, 1, 0, ControlGroup::Layout},
			 {"hill-radius", "Hill radius", 44, 100, 4, 60, ControlGroup::Layout},
			 {"band-width", "Contour band width", 80, 120, 10, 100, ControlGroup::Terrain},
			 {"stairs", "Stairs per hill", 2, 4, 1, 3, ControlGroup::Layout},
			 {"starting-towers", "Starting tower level", 0, 3, 1, 1, ControlGroup::Layout},
			 GeneratorControl::toggle("valley-river", "Valley river", true, ControlGroup::Terrain),
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
