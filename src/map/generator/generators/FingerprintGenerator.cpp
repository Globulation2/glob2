// SPDX-License-Identifier: GPL-3.0-or-later
#include "FingerprintGenerator.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Orbits.h"
#include "Patterns.h"
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

// Fingerprint: an organic labyrinth. A Turing pattern grown over the whole torus (Patterns.h) draws
// long curving bands that fork, merge and dead-end the way the ridges of a fingerprint or the
// stripes of a zebra do, and the bands become the barriers: water, so every corridor of land runs
// beside water and all of it is farmland; or stone, permanent walls with pools in the corridors.
// The wavelength sets how wide the corridors are, the pattern how open the map is (an even
// labyrinth, islands of land in water, or channels of water through land), and the grain stretches
// the bands along one axis so they run rather than wander.
//
// Nothing is symmetric: the pattern is one field over the map and the homes are clearings on a
// lattice through it, so fairness is statistical and the lobby keeps the best-scoring of several
// seeds. Where the bands close a colony off, the cheapest way through is opened as a ford (or a
// gap cut in the stone), so every colony can be walked to from the first.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Wheat and wood regrow only
// near water, so a water fingerprint is fertile everywhere and the fight is over the corridors, not
// the fields; a stone fingerprint is the opposite, its pools the only fertile ground. Corridors that
// fork and dead-end give ambushes and cul-de-sacs no grid has, and reads as country rather than as
// a maze.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say: wheat and wood beside the pond,
// and a quarry of radius 2, which on a water map is the colony's only stone until it finds an
// outcrop.
constexpr int kHomeWheat = 14, kHomeWood = 12, kHomeQuarry = 2;
// The pattern is cleared this far beyond a home's rough disc, so its beach never reaches the
// swarm's ground and a band of the pattern always runs between neighbouring clearings.
constexpr double kClearingMargin = 3.0;
// How much of the map the barrier covers, in percent, for each pattern: Labyrinth, Islands, Channels.
// The field is symmetric about its mean, so 50% would give bands and corridors of equal width; a
// water band spoils a further tile of grass on each side with its beach (Channels.h), so 42% water
// leaves corridors about as wide as the bands read. 60% turns the corridors into strings of islands
// in water; 28% leaves channels of water through wide land.
constexpr int kBarrierShare[3] = {42, 60, 28};
// On a stone map the pools lie at the bottoms of the field's troughs, which run along the middles
// of the corridors: every local minimum with nothing lower within two wavelengths becomes a pool
// (windowMinimum, Morphology.h), grown to kPoolTiles by the field's value so it fills its trough.
// Two wavelengths apart gives about a dozen pools on a 256 map at the default wavelength, each
// watering the corridor round it; a percentile of the field instead gave a hundred tiny pools whose
// beaches cut the stone into blobs.
constexpr int kPoolTiles = 40;

struct Layout
{
	Torus t{1, 1};
	double homeRadius = 0;
	std::vector<ShapePoint> homes, kits;
	std::vector<int> homeOf;
	std::vector<unsigned char> water, stone, clearing;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const FingerprintOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	L.homeRadius = o.homeSize;
	L.homes =
		latticeSites(t.w, t.h, teams, context.bounded("fingerprint-layout", std::uint32_t(t.w)),
					 context.bounded("fingerprint-layout", std::uint32_t(t.h)))
			.sites;
	double spacing = std::min(t.w, t.h);
	for (size_t a = 0; a < L.homes.size(); ++a)
		for (size_t b = a + 1; b < L.homes.size(); ++b)
			spacing =
				std::min(spacing, std::hypot(t.offsetX(int(L.homes[a].x), int(L.homes[b].x)),
											 t.offsetY(int(L.homes[a].y), int(L.homes[b].y))));
	// Clearings shrink to keep a band's width of pattern between neighbours; the smallest home that
	// holds a pond and a kit is the floor.
	L.homeRadius =
		std::min(L.homeRadius, std::floor(spacing / 2 - kClearingMargin - o.wavelength / 2.0));
	if (!homeHasRoom(L.homeRadius))
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}

	// The pattern, and the barrier as the share of it above a level.
	TuringStyle style;
	style.wavelength = o.wavelength;
	style.stretchX = 100 + o.grain * 2;
	std::vector<int> field = turingPattern(t, style, context.stream("fingerprint-pattern"));
	const int level = percentile(field, 100 - kBarrierShare[std::clamp(o.pattern, 0, 2)]);
	std::vector<unsigned char> barrier(n, 0);
	for (int i = 0; i < n; ++i)
		barrier[i] = field[i] >= level;
	// Every home is a clearing in the pattern, with its pond.
	L.clearing.assign(n, 0);
	for (const ShapePoint &home : L.homes)
		for (int i = 0; i < n; ++i)
			if (std::hypot(t.offsetX(int(home.x), i % t.w), t.offsetY(int(home.y), i / t.w)) <=
				L.homeRadius + kClearingMargin)
				L.clearing[i] = 1;
	for (int i = 0; i < n; ++i)
		if (L.clearing[i])
			barrier[i] = 0;
	L.water.assign(n, 0);
	L.stone.assign(n, 0);
	if (o.barrier == 0)
		L.water = barrier;
	else
	{
		L.stone = barrier;
		// Pools in the corridors: one at every trough bottom with nothing lower within two
		// wavelengths, grown along the trough. The only water, so the only fertile ground.
		const std::vector<int> lowest = windowMinimum(t, field, 2 * o.wavelength);
		std::vector<int> queued(n, 0);
		int pools = 0;
		for (int i = 0; i < n; ++i)
			if (field[i] == lowest[i] && !L.clearing[i] && !L.water[i])
				growWater(
					t, L.water, i, kPoolTiles, [&](int j) { return !L.clearing[j]; },
					[&](int j)
					{
						return std::int64_t(field[j]) +
							   t.dist2(i % t.w, i / t.w, j % t.w, j / t.w) * 50;
					},
					queued, ++pools);
		// A pool's beach takes the stone off its shore, so no stone stands within two of a pool.
		const std::vector<unsigned char> shore = dilate(t, L.water, 2);
		for (int i = 0; i < n; ++i)
			if (shore[i])
				L.stone[i] = 0;
	}
	L.homeOf.assign(n, -1);
	const RadialShape home(L.homeRadius, 0.15, context, "fingerprint-home");
	const RadialShape pond(homePondRadius(L.homeRadius), 0.3, context, "fingerprint-pond");
	L.kits = stampRoundHomes(t, L.homes, 0.0, home, L.homeRadius, 1, &pond, L.water, L.homeOf);
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "fingerprint layout";
	const FingerprintOptions o(context.request);
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

	context.stage = "fingerprint terrain";
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
		if (L.water[i])
			terrain[i] = WATER;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	for (int i = 0; i < n; ++i)
		if (L.stone[i] && map.isResourceAllowed(i % t.w, i / t.w, STONE))
			map.setResource(i % t.w, i / t.w, STONE, 1);

	context.stage = "fingerprint colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.homeOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team) { return homeSwarmSite(L.homes[team], 0.0, L.homeRadius); };
	if (!settleColonies(game, context, "fingerprint-starts", homeMask, anchor))
		return false;

	context.stage = "fingerprint resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
		plantOpenHomeKit(
			map, t, context, L.kits[k], 0.0, L.homeRadius, kHomeWheat, kHomeWood, kHomeQuarry,
			[&](int i)
			{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); });
	// The ambient layer over all the land: a share of the fertile ground under crops, in patches, with
	// outcrops and groves; on a water map that is nearly everything.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 10, context.stream("fingerprint-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 5, context.stream("fingerprint-split"));
	const auto eligible = [&](int i)
	{ return L.homeOf[i] < 0 && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += eligible(i) && fertility.at(i % t.w, i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, eligible, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(fertile * 9 / 100, o.wheat)),
								 int(scaledCount(fertile * 6 / 100, o.wood)),
								 int(scaledCount(area / 1000, o.stone)),
								 int(scaledCount(area / 1800, o.fruit))};
		},
		"fingerprint-stone", "fingerprint-fruit");
	seedAlgae(map, context, t, "fingerprint-algae", o.algae, AlgaeBand::shallows(1, 6, 60));
	secureStartingCrops(game, context, t, 24, 32, 0, &L.stone);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&L.stone);
	// The pattern owes nobody a way through: where it closes a colony off, the cheapest way is opened,
	// a ford across water or a gap cut through the stone.
	context.stage = "fingerprint routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, 8, 25, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "fingerprint"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		bool pond = false;
		for (int dy = -2; dy <= 2 && !pond; ++dy)
			for (int dx = -2; dx <= 2 && !pond; ++dx)
				pond = map.isWater(t.x(int(L.kits[k].x) + dx), t.y(int(L.kits[k].y) + dy));
		if (!pond)
			return "Colony " + std::to_string(k) + "'s clearing has lost its pond.";
	}
	return walkFromFirstColony(map, context.request.nbTeams, "the fingerprint",
							   "through the pattern")
		.error;
}
} // namespace

FingerprintOptions::FingerprintOptions(const GenerationRequest &r)
	: wavelength(r.option("wavelength")), pattern(r.option("pattern")),
	  barrier(r.option("barrier")), grain(r.option("grain")), homeSize(r.option("home-size")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition fingerprintDefinition()
{
	return {"fingerprint",
			26,
			"Fingerprint",
			1,
			false,
			{{"wavelength", "Wavelength", 12, 40, 2, 20, ControlGroup::Terrain},
			 GeneratorControl::choice("pattern", "Pattern", {"Labyrinth", "Islands", "Channels"}, 0,
									  ControlGroup::Terrain),
			 GeneratorControl::choice("barrier", "Barrier", {"Water", "Stone"}, 0,
									  ControlGroup::Terrain),
			 {"grain", "Grain", 0, 100, 10, 0, ControlGroup::Terrain},
			 {"home-size", "Home size", 10, 20, 1, 12, ControlGroup::Layout},
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
