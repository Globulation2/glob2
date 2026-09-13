// SPDX-License-Identifier: GPL-3.0-or-later
#include "OldGrowthGenerator.h"
#include "Contact.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Growth.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
using namespace MapGeneration;

// Old growth: a dry continent under unbroken forest. Every colony starts in a clearing with its own
// pond and kit, and beyond the clearing stands wood in every direction. There is no other water but
// a few lakes far from every home, so almost none of the forest ever grows back (wood regrows only
// where the engine's growth probe, 15 tiles each way, finds water): what a colony cuts stays cut, and
// the map opens as the game goes on, the opposite of a swamp that grows shut. Contact happens only
// where someone has cut through, so where a colony's workers clear is where its front will be.
//
// Deep in the forest, at the same cutting distance from every home and on each home's own side, a
// hidden grove waits: a pocket of fruit, wheat and stone that rewards the colony that cuts to it
// first. The lakes are the other prize: the only ground where wood regrows, ringed with wheat and
// an orchard, and as far from every home as the map allows.
//
// Homes sit on a lattice (Orbits.h) and everything else is measured from them, so fairness is by
// construction where the lattice is exact and statistical otherwise (the lobby keeps the best of
// several seeds). With the trails switch a cut trail joins every colony to the first from the
// start; without it (the default) the colonies start entirely apart, and the validator checks the
// map by cutting cost rather than by walking.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Deposits block movement
// and building, wood is clearable, and clearing is slow work, so the forest is a wall that any
// colony can breach anywhere at a price in worker-hours; that makes the front a choice. Wood far
// from water never regrows, so every cut is permanent and the map's history is written in it; wood
// near the home pond does regrow, so a colony never runs out. The lakes' orchards are the only
// fruit not hidden.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say: wheat and wood beside the pond
// (the wood regrows there, being beside water), and a quarry.
constexpr int kHomeWheat = 14, kHomeWood = 12, kHomeQuarry = 2;
// The forest starts this far beyond a home's rough disc, so the clearing's edge is open ground to
// build against and the pond's beach never meets a tree.
constexpr double kClearingMargin = 3.0;
// Lakes keep this far from every clearing and from each other's shores, so a lake's regrowth never
// reaches a home's forest edge (the growth probe reaches 15 tiles) and two lakes read as two.
constexpr int kLakeGap = 20;
// A hidden grove lies at this share of the cutting cost half way to the nearest rival: nearer its
// own home than anyone else's, but deep enough that reaching it is a decision. Its pocket is a
// clearing of this radius.
constexpr int kGroveDepthPercent = 65;
constexpr double kGroveRadius = 3.5;
// What a hidden grove holds: a fruit grove of radius 1 (5 tiles), a wheat patch and a stone clump.
constexpr int kGroveWheat = 16, kGroveStone = 1;
// What a lake holds: wheat round its shore and an orchard of all three fruits.
constexpr int kLakeWheat = 40;

struct Layout
{
	Torus t{1, 1};
	double homeRadius = 0, spacing = 0;
	std::vector<ShapePoint> homes, kits;
	std::vector<int> homeOf;
	std::vector<unsigned char> water, clearing, forest, lake;
	std::vector<int> lakeCentres;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const OldGrowthOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// Homes on the roomiest lattice, shrunk to leave a stretch of forest between neighbours at least
	// as wide as a clearing, so no two clearings ever touch.
	L.homes = latticeSites(t.w, t.h, teams, context.bounded("growth-layout", std::uint32_t(t.w)),
						   context.bounded("growth-layout", std::uint32_t(t.h)))
				  .sites;
	L.spacing = std::min(t.w, t.h);
	for (size_t a = 0; a < L.homes.size(); ++a)
		for (size_t b = a + 1; b < L.homes.size(); ++b)
			L.spacing =
				std::min(L.spacing, std::hypot(t.offsetX(int(L.homes[a].x), int(L.homes[b].x)),
											   t.offsetY(int(L.homes[a].y), int(L.homes[b].y))));
	L.homeRadius = std::min<double>(o.homeSize, std::floor(L.spacing / 3 - kClearingMargin));
	if (!homeHasRoom(L.homeRadius))
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	L.clearing.assign(n, 0);
	for (const ShapePoint &home : L.homes)
		for (int i = 0; i < n; ++i)
			if (std::hypot(t.offsetX(int(home.x), i % t.w), t.offsetY(int(home.y), i / t.w)) <=
				L.homeRadius + kClearingMargin)
				L.clearing[i] = 1;
	L.homeOf.assign(n, -1);
	L.water.assign(n, 0);
	const RadialShape home(L.homeRadius, 0.15, context, "growth-home");
	const RadialShape pond(homePondRadius(L.homeRadius), 0.3, context, "growth-pond");
	L.kits = stampRoundHomes(t, L.homes, 0.0, home, L.homeRadius, 1, &pond, L.water, L.homeOf);

	// Lakes: each at the tile farthest from every clearing and every lake so far (the exact
	// distance transform, Morphology.h), grown to `lakeSize` tiles by distance from its seed with a
	// little noise in the key so the outline is a lake's, not a disc's. `lakes` is a count per
	// 128x128 of map, so a 256 map gets four times as many as a 128.
	L.lake.assign(n, 0);
	const int lakes = int(std::lround(o.lakes * double(n) / (128.0 * 128.0)));
	std::vector<unsigned char> keepClear = dilate(t, L.clearing, kLakeGap);
	const std::vector<int> ripple = periodicNoise(t.w, t.h, 6, context.stream("growth-lakes"));
	std::vector<int> queued(n, 0);
	for (int lake = 0; lake < lakes; ++lake)
	{
		const std::vector<std::int64_t> far = distanceSquaredTo(t, keepClear);
		int seed = -1;
		for (int i = 0; i < n; ++i)
			if (!keepClear[i] && (seed < 0 || far[i] > far[seed]))
				seed = i;
		if (seed < 0)
			break;
		const int grown = growWater(
			t, L.water, seed, o.lakeSize, [&](int i) { return !keepClear[i]; },
			[&](int i)
			{
				const double d =
					std::sqrt(double(t.dist2(seed % t.w, seed / t.w, i % t.w, i / t.w)));
				return std::int64_t(d * 1000) + std::int64_t(ripple[i]) * 2500 / 65536;
			},
			queued, lake + 1);
		if (grown <= 0)
			break;
		L.lakeCentres.push_back(seed);
		for (int i = 0; i < n; ++i)
			if (L.water[i] && !L.clearing[i])
				L.lake[i] = 1;
		keepClear = dilate(t, L.clearing, kLakeGap);
		const std::vector<unsigned char> shores = dilate(t, L.lake, kLakeGap);
		for (int i = 0; i < n; ++i)
			keepClear[i] = keepClear[i] || shores[i];
	}
	// The forest: everything that is not a clearing or water.
	L.forest.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.forest[i] = !L.clearing[i] && !L.water[i];
	return L;
}

// Hidden groves: one per colony, at kGroveDepthPercent of the cutting cost half way to its nearest
// rival, on its own side of the forest (equalCostSites, Contact.h). Each is a pocket cut in the
// forest holding a fruit grove, a wheat patch and a stone clump. Costs are measured on the planted
// forest, so the groves lie at the same worker-hours from every home whatever the forest's gaps.
void hideGroves(Map &map, const Layout &L, GenerationContext &context, const OldGrowthOptions &o,
				int teams)
{
	const Torus &t = L.t;
	const int n = t.size();
	const auto units = unitTilesByTeam(map, teams);
	std::vector<std::vector<int>> costs;
	for (int k = 0; k < teams; ++k)
		costs.push_back(costsFrom(map, t, units[k], StepCosts::chopping(4)));
	// Half way to the nearest rival, in cutting cost: the least cost at which some other colony's
	// field meets this one's, over all colonies, so every grove is measured against the same front.
	std::int64_t frontier = 0;
	int met = 0;
	for (int k = 0; k < teams; ++k)
		for (int j = k + 1; j < teams; ++j)
		{
			int least = -1;
			for (int i = 0; i < n; ++i)
				if (costs[k][i] >= 0 && costs[j][i] >= 0)
				{
					const int here = std::max(costs[k][i], costs[j][i]);
					if (least < 0 || here < least)
						least = here;
				}
			if (least >= 0)
			{
				frontier += least;
				++met;
			}
		}
	if (!met)
		return;
	const int target = int(frontier / met * kGroveDepthPercent / 100);
	std::vector<unsigned char> deep(n, 0);
	for (int i = 0; i < n; ++i)
		deep[i] = L.forest[i] && map.getResource(i % t.w, i / t.w).type == WOOD;
	const std::vector<int> sites = equalCostSites(costs, deep, target, std::max(8, target / 4));
	for (int k = 0; k < teams; ++k)
	{
		if (sites[k] < 0)
			continue;
		const int gx = sites[k] % t.w, gy = sites[k] / t.w;
		// The pocket: wood cut within kGroveRadius of the site.
		for (int dy = -4; dy <= 4; ++dy)
			for (int dx = -4; dx <= 4; ++dx)
				if (dx * dx + dy * dy <= kGroveRadius * kGroveRadius &&
					map.getResource(t.x(gx + dx), t.y(gy + dy)).type == WOOD)
					map.setNoResource(t.x(gx + dx), t.y(gy + dy), 1);
		const auto pocket = [&](int i)
		{ return t.dist2(gx, gy, i % t.w, i / t.w) <= 16 && clearGround(map, i % t.w, i / t.w); };
		if (scaledCount(1, o.fruit) > 0)
			placeResourceClump(map, context, MapGeneratorPoint(gx, gy),
							   CHERRY + int(context.bounded("growth-fruit", 3)), 1);
		if (const int seed = seedNear(t, gx - 2, gy - 2, 4, pocket); seed >= 0)
			growPatch(map, t, seed, CORN, int(scaledCount(kGroveWheat, o.wheat)), pocket);
		if (scaledCount(1, o.stone) > 0)
			if (const int seed = seedNear(t, gx + 2, gy + 2, 4, pocket); seed >= 0)
				placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w), STONE,
								   kGroveStone);
	}
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "old growth layout";
	const OldGrowthOptions o(context.request);
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

	context.stage = "old growth terrain";
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
		if (L.water[i])
			terrain[i] = WATER;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "old growth colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.homeOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team) { return homeSwarmSite(L.homes[team], 0.0, L.homeRadius); };
	if (!settleColonies(game, context, "growth-starts", homeMask, anchor))
		return false;

	context.stage = "old growth resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
		plantOpenHomeKit(
			map, t, context, L.kits[k], 0.0, L.homeRadius, kHomeWheat, kHomeWood, kHomeQuarry,
			[&](int i)
			{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); });
	// The lakes' prizes go down before the forest, so they stand in the open on the shore: wheat all
	// round the water and an orchard of the three fruits a few tiles out.
	for (int centre : L.lakeCentres)
	{
		const int cx = centre % t.w, cy = centre / t.w;
		const auto shore = [&](int i)
		{
			return L.forest[i] && t.dist2(cx, cy, i % t.w, i / t.w) <= 400 &&
				   clearGround(map, i % t.w, i / t.w);
		};
		if (const int seed = seedNear(t, cx, cy, 20, shore); seed >= 0)
			growPatch(map, t, seed, CORN, int(scaledCount(kLakeWheat, o.wheat)), shore);
		if (scaledCount(1, o.fruit) > 0)
		{
			const double spin = context.bounded("growth-fruit", 3600) / 3600.0 * 2 * kPi;
			const double radius = std::sqrt(o.lakeSize / kPi) + 4;
			plantOrchard(map, t, context, cx, cy, radius, {spin}, 4.5, 6, 1, shore);
		}
	}
	// The forest: wood on `forestDensity` percent of the forest ground, the gaps drawn from a noise
	// field so they are small openings rather than lanes. Below the 8-connected site percolation
	// threshold (about 41% open) the openings never join up into a way through, so at any density
	// the control offers (60% and above) the forest is still a wall that has to be cut.
	const std::vector<int> gaps = periodicNoise(t.w, t.h, 5, context.stream("growth-gaps"));
	std::vector<int> levels;
	for (int i = 0; i < n; ++i)
		if (L.forest[i])
			levels.push_back(gaps[i]);
	const int level = percentile(levels, 100 - o.forestDensity);
	plantCover(map, t, L.forest, WOOD, [&](int i)
			   { return gaps[i] >= level && !reserved[i] && clearGround(map, i % t.w, i / t.w); });
	if (o.hiddenGroves)
		hideGroves(map, L, context, o, teams);
	seedAlgae(map, context, t, "growth-algae", o.algae, AlgaeBand::anyWater(30));
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, {o.wheat, 100, o.stone, o.algae, o.fruit});
	// With trails, a cut trail joins every colony to the first: the cheapest cut through the wood.
	if (o.trails)
	{
		context.stage = "old growth trails";
		openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1});
	}
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	const OldGrowthOptions o(context.request);
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "old growth"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	const int teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
	{
		bool pond = false;
		for (int dy = -2; dy <= 2 && !pond; ++dy)
			for (int dx = -2; dx <= 2 && !pond; ++dx)
				pond = map.isWater(t.x(int(L.kits[k].x) + dx), t.y(int(L.kits[k].y) + dy));
		if (!pond)
			return "Colony " + std::to_string(k) + "'s clearing has lost its pond.";
	}
	// The forest is dry: nothing but the home ponds and the lakes waters it, so wood beyond their
	// reach never regrows. Checked on the finished map's own growth field.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	std::vector<unsigned char> wateredBy = dilate(t, L.water, kCropProbeReach);
	for (int i = 0; i < t.size(); ++i)
		if (L.forest[i] && !wateredBy[i] && fertility.at(i % t.w, i / t.w) > 0)
			return "The forest regrows away from any water at (" + std::to_string(i % t.w) + ", " +
				   std::to_string(i / t.w) + ").";
	// Every colony is reachable from the first: by walking with trails, by cutting without.
	if (o.trails)
		return walkFromFirstColony(map, teams, "the forest", "along the trails").error;
	const ContactReport contact = contactMatrix(map, teams, StepCosts::chopping(4));
	for (int k = 1; k < teams; ++k)
		if (contact.cost[0][k] < 0)
			return "Colony " + std::to_string(k) + " cannot be cut to from colony 0.";
	return "";
}
} // namespace

OldGrowthOptions::OldGrowthOptions(const GenerationRequest &r)
	: forestDensity(r.option("forest-density")), lakes(r.option("lakes")),
	  lakeSize(r.option("lake-size")), homeSize(r.option("home-size")),
	  hiddenGroves(r.option("hidden-groves") != 0), trails(r.option("trails") != 0),
	  wheat(r.option("wheat-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition oldGrowthDefinition()
{
	return {
		"old-growth",
		28,
		"Old growth",
		1,
		false,
		// 90% cover reads as unbroken forest with the odd glade; one lake per 128x128 of 90
		// tiles (four on a 256 map, each a few days' cutting from any home) keeps them rare enough
		// to be prizes; the forest carries no wood amount, since the forest is the map.
		{{"forest-density", "Forest density", 60, 100, 5, 90, ControlGroup::Terrain},
		 {"lakes", "Lakes", 0, 4, 1, 1, ControlGroup::Terrain},
		 {"lake-size", "Lake size", 40, 160, 10, 90, ControlGroup::Terrain},
		 {"home-size", "Home size", 10, 20, 1, 13, ControlGroup::Layout},
		 GeneratorControl::toggle("hidden-groves", "Hidden groves", true, ControlGroup::Layout),
		 // On, a trail is cut from every colony to the first before the game starts.
		 GeneratorControl::toggle("trails", "Starting trails", false, ControlGroup::Layout),
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
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
