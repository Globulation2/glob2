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
//
// FEEDBACK 2026-09-13 (first play): "clearing the wood hurts workers. so to have so little water for
// food, and then on top of it have to battle through this old growth forest to reach your opponent,
// its a huge lift. Each player's home base needs to be larger by default and have a lot more pools
// of water. There should be no wood spawned in the player's home area and instead only wheat spawned
// around all the player's various pools and lakes. Second, the player's base should be surrounded
// by a ring of sand to protect the old growth from starting to grow inwards into the wheat. It's
// almost like how humans would clear a settlement within the woods." So: homes default to radius
// 20 (was 13), carry a ring of pools besides the central pond, grow wheat round every pool and no
// wood at all inside (wood is cut from the forest edge just past the sand, within the crop
// guarantee's 32 steps), and a two-tile sand ring round the clearing stops the forest spreading in
// (wheat and wood spread only onto grass, and the growth probe refuses beside sand).
//
// FEEDBACK 2026-09-13 (second play): "default base size still needs to be just a bit bigger than
// this, and we also need to preseed a ton more wheat around these pools in the player's home base."
// So: radius 24 by default (was 20), the kit's two patches 24 each (was 14), and every pool ringed
// with 32 wheat (was 12) grown from four seeds spaced round its shore so the ring is whole.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say: two wheat patches beside the central
// pond and a quarry. No wood (FEEDBACK 2026-09-13): the forest edge is the woodlot.
constexpr int kHomeWheat = 24, kHomeQuarry = 2;
// The ring of pools round a home (FEEDBACK 2026-09-13, "a lot more pools of water"): `homePools`
// pools of this radius on a ring at this share of the home's radius, each with this much wheat on its
// shore. At the default radius 24 the ring is 14 tiles out, so every pool waters the ground between
// itself and the central pond (the growth probe reaches 15) and the whole clearing is fertile.
constexpr double kPoolRadius = 2.5, kPoolRingShare = 0.6;
constexpr int kPoolWheat = 32, kPoolSeeds = 4;
// The forest starts this far beyond a home's rough disc, so the clearing's edge is open ground to
// build against and the pond's beach never meets a tree; then a ring of sand this wide keeps the
// forest from spreading into the clearing (FEEDBACK 2026-09-13). Two tiles: crops spread only onto
// an adjacent grass tile, so a single tile of sand corners already stops them, and the second tile
// keeps the beach arithmetic from ever leaving a grass corner between forest and clearing.
constexpr double kClearingMargin = 3.0;
constexpr int kSandRing = 2;
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
	std::vector<unsigned char> water, clearing, sand, forest, lake;
	std::vector<std::vector<ShapePoint>> pools; // each home's ring of pools
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
	dealStarts(context, L.homes); // which colony gets which site is a draw, not the order
	L.spacing = nearestSiteDistance(t, L.homes);
	L.homeRadius =
		std::min<double>(o.homeSize, std::floor(L.spacing / 3 - kClearingMargin - kSandRing));
	if (!homeHasRoom(L.homeRadius))
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	L.clearing.assign(n, 0);
	L.sand.assign(n, 0);
	for (const ShapePoint &home : L.homes)
		for (int i = 0; i < n; ++i)
		{
			const double d =
				std::hypot(t.offsetX(int(home.x), i % t.w), t.offsetY(int(home.y), i / t.w));
			if (d <= L.homeRadius + kClearingMargin)
				L.clearing[i] = 1;
			else if (d <= L.homeRadius + kClearingMargin + kSandRing)
				L.sand[i] = 1;
		}
	for (int i = 0; i < n; ++i)
		if (L.clearing[i])
			L.sand[i] = 0;
	L.homeOf.assign(n, -1);
	L.water.assign(n, 0);
	const RadialShape home(L.homeRadius, 0.15, context, "growth-home");
	const RadialShape pond(homePondRadius(L.homeRadius), 0.3, context, "growth-pond");
	L.kits = stampRoundHomes(t, L.homes, 0.0, home, L.homeRadius, 1, &pond, L.water, L.homeOf);
	// The ring of pools (FEEDBACK 2026-09-13), evenly spaced from a random phase per home.
	const RadialShape pool(kPoolRadius, 0.3, context, "growth-pools");
	for (const ShapePoint &home : L.homes)
	{
		std::vector<ShapePoint> ring;
		const double phase = context.bounded("growth-pools", 3600) / 3600.0 * 2 * kPi;
		for (int p = 0; p < o.homePools; ++p)
		{
			const ShapePoint at = polarPoint(home.x, home.y, kPoolRingShare * L.homeRadius,
											 phase + 2 * kPi * p / o.homePools);
			ring.push_back(at);
			fillShape(L.water, t, at.x, at.y, pool, 0.0);
		}
		L.pools.push_back(ring);
	}

	// Lakes: each at the tile farthest from every clearing and every lake so far (the exact
	// distance transform, Morphology.h), grown to `lakeSize` tiles by distance from its seed with a
	// little noise in the key so the outline is a lake's, not a disc's. `lakes` is a count per
	// 128x128 of map, so a 256 map gets four times as many as a 128.
	L.lake.assign(n, 0);
	const int lakes = int(std::lround(o.lakes * double(n) / (128.0 * 128.0)));
	std::vector<unsigned char> settled(n, 0);
	for (int i = 0; i < n; ++i)
		settled[i] = L.clearing[i] || L.sand[i];
	std::vector<unsigned char> keepClear = dilate(t, settled, kLakeGap);
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
		keepClear = dilate(t, settled, kLakeGap);
		const std::vector<unsigned char> shores = dilate(t, L.lake, kLakeGap);
		for (int i = 0; i < n; ++i)
			keepClear[i] = keepClear[i] || shores[i];
	}
	// The forest: everything that is not a clearing or water.
	L.forest.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.forest[i] = !L.clearing[i] && !L.sand[i] && !L.water[i];
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
		terrain[i] = L.water[i] ? WATER : L.sand[i] ? SAND : GRASS;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "old growth colonies";
	if (!settleRoundColonies(game, context, "growth-starts", L.homeOf, L.homes, L.homeRadius))
		return false;

	context.stage = "old growth resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	// The kit (FEEDBACK 2026-09-13: no wood in the home): two wheat patches beside the central pond
	// and a quarry beyond it, then wheat on the shore of every pool of the ring.
	for (int k = 0; k < teams; ++k)
	{
		const auto eligible = [&](int i)
		{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
		const KitFrame frame{int(std::lround(L.kits[k].x)), int(std::lround(L.kits[k].y)), 0.0};
		const double reach = homePondRadius(L.homeRadius) * 1.2 + 3;
		plantKit(map, t, context,
				 {frame.at(-0.4 * reach, -reach, 12), frame.at(-0.4 * reach, reach, 12),
				  frame.at(reach + 3, 0, 10), kHomeWheat, 0, kHomeQuarry},
				 eligible);
		// Wheat all round every pool (second play: "a ton more wheat around these pools"): grown
		// from kPoolSeeds seeds spaced round the shore, each with its share, so the ring is whole.
		for (const ShapePoint &pool : L.pools[k])
		{
			const int px = int(std::lround(pool.x)), py = int(std::lround(pool.y));
			const auto shore = [&](int i)
			{
				return eligible(i) &&
					   t.dist2(px, py, i % t.w, i / t.w) <= (kPoolRadius + 5) * (kPoolRadius + 5);
			};
			for (int q = 0; q < kPoolSeeds; ++q)
			{
				const double a = 2 * kPi * q / kPoolSeeds;
				const int seed =
					seedNear(t, int(std::lround(pool.x + (kPoolRadius + 2) * std::cos(a))),
							 int(std::lround(pool.y + (kPoolRadius + 2) * std::sin(a))), 2, shore);
				if (seed >= 0)
					growPatch(map, t, seed, CORN, kPoolWheat / kPoolSeeds, shore);
			}
		}
	}
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
	if (const std::string lost = homePondMissing(map, t, L.kits, teams, "clearing", "pond");
		!lost.empty())
		return lost;
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
	  homePools(r.option("home-pools")), hiddenGroves(r.option("hidden-groves") != 0),
	  trails(r.option("trails") != 0), wheat(r.option("wheat-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition oldGrowthDefinition()
{
	return {
		"old-growth",
		28,
		"Old growth",
		4,
		false,
		// 90% cover reads as unbroken forest with the odd glade; one lake per 128x128 of 90
		// tiles (four on a 256 map, each a few days' cutting from any home) keeps them rare enough
		// to be prizes; the forest carries no wood amount, since the forest is the map.
		{{"forest-density", "Forest density", 60, 100, 5, 90, ControlGroup::Terrain},
		 {"lakes", "Lakes", 0, 4, 1, 1, ControlGroup::Terrain},
		 {"lake-size", "Lake size", 40, 160, 10, 90, ControlGroup::Terrain},
		 // FEEDBACK 2026-09-13: homes of radius 24 with five pools (first 13 with one pond, then
		 // 20; the second play wanted "just a bit bigger"); the whole clearing is within the growth
		 // probe's reach of water.
		 {"home-size", "Home size", 12, 30, 1, 24, ControlGroup::Layout},
		 {"home-pools", "Home pools", 0, 8, 1, 5, ControlGroup::Layout},
		 GeneratorControl::toggle("hidden-groves", "Hidden groves", true, ControlGroup::Layout),
		 // On, a trail is cut from every colony to the first before the game starts.
		 GeneratorControl::toggle("trails", "Starting trails", false, ControlGroup::Layout),
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate,
		true,
		designFailure<design>,
		validateWorld};
}
