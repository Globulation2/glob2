// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolderGenerator.h"
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
#include <string>
#include <vector>
using namespace MapGeneration;

// Polder: reclaimed land, all of it. The whole torus is laid out in long rows of crops with a
// ditch of water between every two, the way a real farm is (Farmland.h's row widths: crops wide
// enough to fill, water close enough that every crop regrows), and sand dykes cross the ditches at
// regular intervals. Every colony starts in a small grass village on the rows, and hamlets of grass
// lie between the villages for forward inns. Food is effectively unlimited: the game is logistics.
// Units walk only where there is no crop and no water - along the ditches' beaches, over the dykes,
// through the villages - so hungry units walk a long way unless the inns are placed well, fights
// happen on the dykes, and a colony that can swim ignores the dykes altogether.
//
// The rows and the dykes are stripe fields that wrap the torus exactly (Patterns.h), so the polder
// has no seam; the villages sit on a lattice (Orbits.h). Fairness is statistical: every village sees
// the same rows, but which dyke it stands beside is where it fell.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Crops block movement and
// building, so a field is a wall until it is harvested and a village is the only room to build;
// water within a few tiles of every crop keeps every field regrowing, so the map never runs short;
// sand beside a ditch is where everyone walks. Swimming turns every ditch into a road: the second
// half of the game is a different map.
namespace
{

// Every village's starting kit, unscaled whatever the amounts say: wheat and wood at the village's
// edge (they regrow there, the ditches being a few tiles away) and a quarry, the only stone the
// polder has besides the odd outcrop.
constexpr int kHomeWheat = 14, kHomeWood = 12, kHomeQuarry = 2;
// Row widths, across the rows in tiles. Straight rows: 10 of crops and 6 of water, a period of 16 so
// the pattern divides every map side and wraps without a seam (the yield fit's 10 and 8 would give
// 18, which does not, and the difference in yield is under 3%). Diagonal rows: 11 of crops and 6 of
// water on a perpendicular spacing set by the map, about 17 on a 256 map.
constexpr int kStraightCrops = 10, kStraightPeriod = 16;
constexpr int kDiagonalCrops = 11, kDiagonalStep = 24;
// A dyke is a line of sand this many corners wide across the rows: two corners make one whole tile
// of sand with a walkable half tile either side, a lane two or three units wide.
constexpr int kDykeCorners = 2;
// Hamlets are grass discs of this radius half way between neighbouring villages: room for an inn and
// a tower, not for a swarm.
constexpr double kHamletRadius = 5.0;

struct Layout
{
	Torus t{1, 1};
	StripeStyle rows, along;
	double spacing = 0, crops = 0, villageRadius = 0;
	std::vector<ShapePoint> homes, kits, hamlets;
	std::vector<int> homeOf;
	std::vector<unsigned char> water, dyke, village, hamlet;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const PolderOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// The rows: straight rows run across the map with `h / 16` of them from top to bottom; diagonal
	// rows climb as many times across the width as down the height. Both wrap exactly.
	L.rows.warpPercent = 0;
	if (o.rowAngle == 0)
	{
		L.rows.acrossX = 0;
		L.rows.acrossY = t.h / kStraightPeriod;
		L.crops = kStraightCrops;
	}
	else
	{
		L.rows.acrossX = std::max(1, t.w / kDiagonalStep);
		L.rows.acrossY = std::max(1, t.h / kDiagonalStep);
		L.crops = kDiagonalCrops;
	}
	L.spacing = stripeSpacing(t, L.rows);
	L.along = alongStripes(t, L.rows);
	const std::vector<int> phase = stripePhase(t, L.rows, context.stream("polder-rows"));
	const std::vector<int> along = stripePhase(t, L.along, context.stream("polder-rows"));

	// The villages: on a lattice, shrunk so a whole row of crops and ditch always lies between two.
	L.homes = latticeSites(t.w, t.h, teams, context.bounded("polder-layout", std::uint32_t(t.w)),
						   context.bounded("polder-layout", std::uint32_t(t.h)))
				  .sites;
	double nearest = std::min(t.w, t.h);
	for (size_t a = 0; a < L.homes.size(); ++a)
		for (size_t b = a + 1; b < L.homes.size(); ++b)
			nearest =
				std::min(nearest, std::hypot(t.offsetX(int(L.homes[a].x), int(L.homes[b].x)),
											 t.offsetY(int(L.homes[a].y), int(L.homes[b].y))));
	L.villageRadius = std::min<double>(o.villageSize, std::floor((nearest - L.spacing) / 2));
	if (L.villageRadius < 8)
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	L.village.assign(n, 0);
	L.homeOf.assign(n, -1);
	L.water.assign(n, 0);
	const RadialShape village(L.villageRadius, 0.15, context, "polder-village");
	L.kits =
		stampRoundHomes(t, L.homes, 0.0, village, L.villageRadius, 0, nullptr, L.water, L.homeOf);
	for (int i = 0; i < n; ++i)
		L.village[i] = L.homeOf[i] >= 0;
	// Hamlets: a small grass disc half way between every two villages that are nearest neighbours.
	L.hamlet.assign(n, 0);
	if (o.hamlets)
	{
		const RadialShape hamlet(kHamletRadius, 0.2, context, "polder-hamlet");
		for (size_t a = 0; a < L.homes.size(); ++a)
			for (size_t b = a + 1; b < L.homes.size(); ++b)
			{
				const double dx = t.offsetX(int(L.homes[a].x), int(L.homes[b].x));
				const double dy = t.offsetY(int(L.homes[a].y), int(L.homes[b].y));
				if (std::hypot(dx, dy) > nearest * 1.2)
					continue;
				const ShapePoint middle{std::fmod(L.homes[a].x + dx / 2 + t.w, t.w),
										std::fmod(L.homes[a].y + dy / 2 + t.h, t.h)};
				L.hamlets.push_back(middle);
				fillShape(L.hamlet, t, middle.x, middle.y, hamlet, 0.0);
			}
	}

	// Water in every ditch, except through the villages and hamlets, and the dykes across it: a line
	// of sand corners every `dykeSpacing` tiles along the rows.
	const int cropUnits = int(L.crops / L.spacing * 65536);
	const double alongLength = stripeSpacing(t, L.along);
	const int dykes = std::max(1, int(std::lround(alongLength / o.dykeSpacing)));
	const int dykePeriod = 65536 / dykes, dykeHalf = int(kDykeCorners / 2.0 / alongLength * 65536);
	L.dyke.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		if (L.village[i] || L.hamlet[i])
			continue;
		const int offset = along[i] % dykePeriod;
		const bool onDyke = offset < dykeHalf || dykePeriod - offset < dykeHalf;
		if (phase[i] >= cropUnits)
			(onDyke ? L.dyke[i] : L.water[i]) = 1;
	}
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "polder layout";
	const PolderOptions o(context.request);
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

	context.stage = "polder terrain";
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
		terrain[i] = L.water[i] ? WATER : L.dyke[i] ? SAND : GRASS;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);

	context.stage = "polder colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.homeOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team)
	{ return homeSwarmSite(L.homes[team], 0.0, L.villageRadius); };
	if (!settleColonies(game, context, "polder-starts", homeMask, anchor))
		return false;

	context.stage = "polder resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
		plantOpenHomeKit(
			map, t, context, L.kits[k], 0.0, L.villageRadius, kHomeWheat, kHomeWood, kHomeQuarry,
			[&](int i)
			{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); });
	// The fields: crops on the rows' grass, nearly all of it. Wheat on 55% of the fertile row ground
	// and wood on 15%, in patches, so the rows are mostly under crop (this is the polder) while the
	// gaps between patches leave room to build a mill or a tower on a row; the villages and hamlets
	// stay open. Every row tile is within a few tiles of water, so it all regrows.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 8, context.stream("polder-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 4, context.stream("polder-split"));
	const auto rowGround = [&](int i)
	{ return !L.village[i] && !L.hamlet[i] && !reserved[i] && clearGround(map, i % t.w, i / t.w); };
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += rowGround(i) && fertility.at(i % t.w, i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, rowGround, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(fertile * 55 / 100, o.wheat)),
								 int(scaledCount(fertile * 15 / 100, o.wood)),
								 int(scaledCount(area / 2500, o.stone)), 0};
		},
		"polder-stone", "polder-fruit");
	// A grove of one fruit in every hamlet, so a forward inn there has something to stock.
	if (scaledCount(1, o.fruit) > 0)
		for (const ShapePoint &hamlet : L.hamlets)
			if (const int seed =
					seedNear(t, int(hamlet.x), int(hamlet.y), 3, [&](int i)
							 { return L.hamlet[i] && clearGround(map, i % t.w, i / t.w); });
				seed >= 0)
				placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w),
								   CHERRY + int(context.bounded("polder-fruit", 3)), 1);
	seedAlgae(map, context, t, "polder-algae", o.algae, AlgaeBand::anyWater(60));
	secureStartingCrops(game, context, t);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit});
	// The beaches along the ditches and the dykes across them join everything; the crops could still
	// close a lane, so the cheapest way through crops is opened, never through water.
	context.stage = "polder routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "polder"); !mismatch.empty())
		return mismatch;
	return walkFromFirstColony(map, context.request.nbTeams, "the polder", "along the dykes").error;
}
} // namespace

PolderOptions::PolderOptions(const GenerationRequest &r)
	: rowAngle(r.option("row-angle")), dykeSpacing(r.option("dyke-spacing")),
	  villageSize(r.option("village-size")), hamlets(r.option("hamlets") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition polderDefinition()
{
	return {
		"polder",
		30,
		"Polder",
		1,
		false,
		// A dyke every 24 tiles is a lane every one and a half rows' walk; villages of radius 11
		// hold a swarm, its kit and a few more buildings and no more.
		{GeneratorControl::choice("row-angle", "Row angle", {"Straight", "Diagonal"}, 0,
								  ControlGroup::Terrain),
		 {"dyke-spacing", "Dyke spacing", 12, 48, 4, 24, ControlGroup::Terrain},
		 {"village-size", "Village size", 8, 16, 1, 11, ControlGroup::Layout},
		 GeneratorControl::toggle("hamlets", "Hamlets", true, ControlGroup::Layout),
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
