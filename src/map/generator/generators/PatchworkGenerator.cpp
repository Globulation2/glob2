// SPDX-License-Identifier: GPL-3.0-or-later
#include "PatchworkGenerator.h"
#include "Biomes.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
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
#include "Territories.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
using namespace MapGeneration;

// Patchwork: every colony starts on a different kind of land. The map is shared out into one
// territory per colony, and each territory is dealt a kit from Biomes.h in turn: a fertile plain
// (big ponds, wide fields, open ground), a stone fortress (a ring of stone with gates, outcrops,
// poor water), an orchard island (a lake with an island of all three fruits, little farmland) or a
// forest (most of the ground under wood, ponds to keep it growing). Every player plays a different
// opening: the plain builds wide, the fortress holds, the island pulls hungry enemies across with
// its fruit, the forest cuts.
//
// This is the one map in the catalog that gives up fairness by construction. What it does instead is
// share the ground out by worth: a territory dealt a poorer kit gets proportionally more tiles
// (growTerritories' worth, from biomeWorth's estimate), and the start scorer ranks the seeds the
// lobby rolls. biomeWorth is an estimate from the scorer's weights, not a measurement: the kits are
// to be tuned with the fairness tournament (docs/map-generators/FAIRNESS_TOURNAMENT.md).
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Crops regrow near water, so
// the plain out-produces everyone and must be attacked before it does; stone is a wall no one
// clears, so the fortress's gates are its whole defence; an inn stocked with all three fruits pulls
// hungry enemy units across, so the island's orchard is a weapon; wood is a wall until cut, so the
// forest colony chooses where its front is.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say, so every opening is at least
// viable: wheat and wood beside the home's own pond, and a quarry.
constexpr int kHomeWheat = 14, kHomeWood = 12, kHomeQuarry = 2;
// The home's clearing keeps this much beyond its rough disc free of the kit's cover, ponds and ring.
constexpr double kClearingMargin = 3.0;
// A fortress's gates: one towards every neighbouring territory, this wide.
constexpr int kGateRadius = 3;
// Territory borders wander with noise cells half a home spacing across, weighted so a border can
// bend by a few tiles but never wraps round a home.
constexpr int kBorderCostScale = 700;

struct Layout
{
	Torus t{1, 1};
	double homeRadius = 0;
	std::vector<ShapePoint> homes, kits;
	std::vector<int> kitOf; // which BiomeKit each colony was dealt
	std::vector<int> territory, homeOf;
	std::vector<std::vector<unsigned char>> regions, doors;
	std::vector<BiomeTerrain> biomes;
	std::vector<unsigned char> water, clearing;
	TerrainSketch sketch;
	std::string failure;
};

const std::vector<BiomeKit> &kits()
{
	static const std::vector<BiomeKit> all{fertilePlain(), stoneFortress(), orchardIsland(),
										   forest()};
	return all;
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const PatchworkOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// Homes on a lattice, and the kits dealt round from a random first.
	L.homes = latticeSites(t.w, t.h, teams, context.bounded("patch-layout", std::uint32_t(t.w)),
						   context.bounded("patch-layout", std::uint32_t(t.h)))
				  .sites;
	double spacing = std::min(t.w, t.h);
	for (size_t a = 0; a < L.homes.size(); ++a)
		for (size_t b = a + 1; b < L.homes.size(); ++b)
			spacing =
				std::min(spacing, std::hypot(t.offsetX(int(L.homes[a].x), int(L.homes[b].x)),
											 t.offsetY(int(L.homes[a].y), int(L.homes[b].y))));
	L.homeRadius = std::min<double>(o.homeSize, std::floor(spacing / 3 - kClearingMargin));
	if (!homeHasRoom(L.homeRadius))
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	const int first = int(context.bounded("patch-deal", std::uint32_t(kits().size())));
	std::vector<double> worth;
	for (int k = 0; k < teams; ++k)
	{
		L.kitOf.push_back((first + k) % int(kits().size()));
		worth.push_back(biomeWorth(kits()[L.kitOf[k]]));
	}

	// Territories by worth (growTerritories): the colony whose ground is worth least per tile takes
	// the next tile, so it ends with more of them; noise in the cost bends the borders, and a few
	// smoothing passes straighten them into curves.
	std::vector<std::vector<int>> seeds;
	for (const ShapePoint &home : L.homes)
		seeds.push_back({t.at(int(home.x), int(home.y))});
	const std::vector<int> noise =
		fractalNoise(t.w, t.h, std::max(4, int(spacing) / 2), 3, context.stream("patch-borders"));
	const int roughness = o.borderRoughness * kBorderCostScale / 100;
	L.territory = growTerritories(
					  t, std::vector<unsigned char>(n, 1), seeds, [&](int i)
					  { return int(std::int64_t(noise[i]) * roughness / 65536); }, &worth)
					  .labels;
	std::vector<unsigned char> keep(n, 0);
	for (const auto &seed : seeds)
		keep[seed[0]] = 1;
	if (o.smoothing > 0)
		smoothLabels(t, L.territory, o.smoothing, keep, 3);
	// Every tile must belong to someone: whatever smoothing or growth left unclaimed goes to the
	// nearest home.
	for (int i = 0; i < n; ++i)
		if (L.territory[i] < 0)
		{
			int best = 0;
			for (int k = 1; k < teams; ++k)
				if (t.dist2(i % t.w, i / t.w, int(L.homes[k].x), int(L.homes[k].y)) <
					t.dist2(i % t.w, i / t.w, int(L.homes[best].x), int(L.homes[best].y)))
					best = k;
			L.territory[i] = best;
		}
	L.regions.assign(teams, std::vector<unsigned char>(n, 0));
	for (int i = 0; i < n; ++i)
		L.regions[L.territory[i]][i] = 1;

	// Gates: for a fortress, a door in its ring towards every neighbouring territory, at the rim tile
	// nearest the line between the two homes. Computed for every kit, harmlessly.
	L.doors.assign(teams, std::vector<unsigned char>(n, 0));
	for (int k = 0; k < teams; ++k)
	{
		std::vector<unsigned char> neighbour(teams, 0);
		for (int i = 0; i < n; ++i)
			if (L.territory[i] == k)
				for (const auto &s : kCardinalSteps)
				{
					const int other = L.territory[t.at(i % t.w + s[0], i / t.w + s[1])];
					if (other != k)
						neighbour[other] = 1;
				}
		std::vector<int> gates;
		for (int j = 0; j < teams; ++j)
		{
			if (!neighbour[j])
				continue;
			const double mx = L.homes[k].x + t.offsetX(int(L.homes[k].x), int(L.homes[j].x)) / 2.0;
			const double my = L.homes[k].y + t.offsetY(int(L.homes[k].y), int(L.homes[j].y)) / 2.0;
			int best = -1;
			for (int i = 0; i < n; ++i)
			{
				if (L.territory[i] != k)
					continue;
				bool rim = false;
				for (const auto &s : kCardinalSteps)
					rim = rim || L.territory[t.at(i % t.w + s[0], i / t.w + s[1])] != k;
				if (rim && (best < 0 || t.dist2(int(mx), int(my), i % t.w, i / t.w) <
											t.dist2(int(mx), int(my), best % t.w, best / t.w)))
					best = i;
			}
			if (best >= 0)
				gates.push_back(best);
		}
		L.doors[k] = dilate(t, tileMask(t, gates), kGateRadius);
	}

	// The homes' clearings and ponds, then each kit's own terrain in its territory: a kit's ponds
	// and ring keep out of the clearing, whose pond is the home's.
	L.clearing.assign(n, 0);
	for (const ShapePoint &home : L.homes)
		for (int i = 0; i < n; ++i)
			if (std::hypot(t.offsetX(int(home.x), i % t.w), t.offsetY(int(home.y), i / t.w)) <=
				L.homeRadius + kClearingMargin)
				L.clearing[i] = 1;
	L.homeOf.assign(n, -1);
	L.water.assign(n, 0);
	const RadialShape home(L.homeRadius, 0.15, context, "patch-home");
	const RadialShape pond(homePondRadius(L.homeRadius), 0.3, context, "patch-pond");
	L.kits = stampRoundHomes(t, L.homes, 0.0, home, L.homeRadius, 1, &pond, L.water, L.homeOf);
	// Each kit's terrain over its whole territory (so a fortress's ring runs round the territory,
	// not round the home clearing); a kit's pond that landed in the clearing is drained, leaving the
	// home its own pond.
	L.sketch.assign(n, GRASS);
	const std::vector<unsigned char> homeWater = L.water;
	for (int k = 0; k < teams; ++k)
	{
		L.biomes.push_back(sketchBiome(L.sketch, t, L.regions[k], L.doors[k], kits()[L.kitOf[k]],
									   context, "patch-biome-" + std::to_string(k)));
		for (int i = 0; i < n; ++i)
		{
			if (L.clearing[i] && !homeWater[i])
			{
				L.biomes[k].water[i] = 0;
				L.sketch[i] = GRASS;
			}
			if (L.biomes[k].water[i])
				L.water[i] = 1;
		}
	}
	for (int i = 0; i < n; ++i)
		if (L.water[i])
			L.sketch[i] = WATER;
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "patchwork layout";
	const PatchworkOptions o(context.request);
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

	context.stage = "patchwork terrain";
	TerrainSketch terrain = L.sketch;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "patchwork colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.homeOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team) { return homeSwarmSite(L.homes[team], 0.0, L.homeRadius); };
	if (!settleColonies(game, context, "patch-starts", homeMask, anchor))
		return false;

	context.stage = "patchwork resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
		plantOpenHomeKit(
			map, t, context, L.kits[k], 0.0, L.homeRadius, kHomeWheat, kHomeWood, kHomeQuarry,
			[&](int i)
			{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); });
	// Each kit's deposits over its territory, the home's clearing kept open. The amounts scale each
	// kit's own shares.
	std::vector<unsigned char> stone(n, 0);
	for (int k = 0; k < teams; ++k)
	{
		BiomeKit kit = kits()[L.kitOf[k]];
		kit.farmPerMille = int(scaledCount(kit.farmPerMille, (o.wheat + o.wood) / 2));
		kit.outcropsPer1000 = int(scaledCount(kit.outcropsPer1000, o.stone));
		kit.grovesPer1000 = int(scaledCount(kit.grovesPer1000, o.fruit));
		kit.coverPercent = std::min(100, int(scaledCount(kit.coverPercent, o.wood)));
		std::vector<unsigned char> keepClear(n, 0), ground(n, 0);
		for (int i = 0; i < n; ++i)
		{
			keepClear[i] = L.clearing[i] || reserved[i];
			ground[i] = L.regions[k][i];
			stone[i] = stone[i] || L.biomes[k].wall[i];
		}
		furnishBiome(map, t, context, ground, L.biomes[k], kit, keepClear,
					 "patch-" + std::to_string(k));
	}
	seedAlgae(map, context, t, "patch-algae", o.algae, AlgaeBand::anyWater(30));
	secureStartingCrops(game, context, t, 24, 32, 0, &stone);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&stone);
	// The kits' cover and rings can close a colony in; the cheapest way through is opened, through
	// crops for preference, through a ring only when a gate failed.
	context.stage = "patchwork routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, 12, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "patchwork"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		bool pond = false;
		for (int dy = -2; dy <= 2 && !pond; ++dy)
			for (int dx = -2; dx <= 2 && !pond; ++dx)
				pond = map.isWater(t.x(int(L.kits[k].x) + dx), t.y(int(L.kits[k].y) + dy));
		if (!pond)
			return "Colony " + std::to_string(k) + "'s home has lost its pond.";
	}
	return walkFromFirstColony(map, context.request.nbTeams, "the patchwork", "across the borders")
		.error;
}
} // namespace

PatchworkOptions::PatchworkOptions(const GenerationRequest &r)
	: homeSize(r.option("home-size")), borderRoughness(r.option("border-roughness")),
	  smoothing(r.option("smoothing")), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition patchworkDefinition()
{
	return {"patchwork",
			33,
			"Patchwork",
			1,
			false,
			{{"home-size", "Home size", 10, 20, 1, 13, ControlGroup::Layout},
			 {"border-roughness", "Border roughness", 0, 100, 10, 40, ControlGroup::Terrain},
			 {"smoothing", "Smoothing", 0, 4, 1, 2, ControlGroup::Terrain},
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
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
