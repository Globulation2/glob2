// SPDX-License-Identifier: GPL-3.0-or-later
#include "VulturesGenerator.h"
#include "ClearingLandscape.h"
#include "Contact.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Sketch.h"
#include "StartQuality.h"
#include <algorithm>
using namespace MapGeneration;

// Old Growth's clearings and lakes, with finite wheat replacing the dry forest. Every wheat
// tile has zero growth probability and one harvest left. Wood alone occupies the shores.
// Homes have room to build and small dry rations; trails allow raids immediately. Harvesting
// opens the fields, but spending the finite food on population leaves less time for an army.
namespace
{
// Opening budgets are deliberately fixed even at zero abundance. Forty-eight single-harvest
// wheat tiles provide a small reserve while workers reach the exterior fields; they are not
// a sustainable farm or a guarantee of any particular survival time. Twenty-four timber tiles
// keep the first buildings from depending on a lucky shore scatter. Timber retains normal
// engine stock amounts, so "tiles" is not the same unit as pieces of wood.
constexpr int kStarterWheat = 48, kStarterWood = 24;
// A small permanent quarry gives mining frontage for upgrades/towers without filling the home.
// Stone is eternal in this engine, so five tiles is an access budget, not a depletion budget.
constexpr int kQuarryTiles = 5;
// At normal abundance, correlated wheat occupies about 35% of eligible dry exterior ground.
// This leaves several harvesting edges and initial approaches rather than one solid crop wall.
// A small 128x128 AI pilot found early Nicowar contact at this setting; raising the player control
// to 125% changed winners and hunger in both directions across paired seeds, so it did not justify
// raising the opening default. Quantile ties and route clearing mean final cover is not exact.
constexpr int kWheatCover = 35;
// A 20% shore scatter leaves worker/building gaps; at 200% it reaches 40%, not a solid wood wall.
// Eight tiles is a visibly local shoreline band, well inside the engine's 15-tile growth probe.
// Wood can subsequently spread under ordinary engine rules; only its INITIAL position is capped.
constexpr int kWoodCover = 20, kShoreReach = 8;
// Noise octave count: five produces coherent, harvestable patches instead of independent dots.
constexpr int kFieldOctaves = 5;
// These are the framework's usual starting-access/room targets, made explicit because generic
// crop repair is forbidden here. The 16 origins overlap; they do not promise 16 separate buildings.
constexpr int kWheatReach = 24, kWoodReach = 32, kRoomRange = 24, kRoomSites = 16;
// Search around the pond rather than its water tile. Twelve spans the beach and nearby grass;
// putting the quarry nine tiles beyond its centre separates it from the swarm and dry rations.
constexpr int kPondSearch = 12, kQuarryOffset = 9;
constexpr int kFoodPerTile = 1;
using Layout = ClearingLandscape;
Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const VulturesOptions o(request);
	// No ring of home pools: those would irrigate the dry rations and undermine finite food.
	// The central pond is retained for wood; lake omissions and home shrinkage use shared rules.
	return clearingLandscape(request, context,
							 {o.homeSize, 0, o.lakes, o.lakeSize, "vultures", true});
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "vultures layout";
	const VulturesOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	context.stage = "vultures terrain";
	TerrainSketch terrain(t.size(), GRASS);
	for (int i = 0; i < t.size(); ++i)
		terrain[i] = L.water[i] ? WATER : L.sand[i] ? SAND : GRASS;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	for (int k = 0; k < context.request.nbTeams; ++k)
		game.addTeam();
	context.stage = "vultures colonies";
	if (!settleRoundColonies(game, context, "vultures-starts", L.homeOf, L.homes, L.homeRadius))
		return false;
	const auto reserved = swarmSurroundings(t, context);
	// Ungated fertility asks whether ANY wheat placed here could grow. A deposit-gated field
	// would incorrectly call unplanted but irrigated ground dry.
	const auto fertility = Fertility::forMap(map, false);
	const auto shore = dilate(t, pureTiles(map, WATER), kShoreReach);
	const auto free = [&](int i) { return !reserved[i] && clearGround(map, i % t.w, i / t.w); };
	context.stage = "vultures starting rations";
	context.telemetry.measure("vultures.starter.requested-wheat-tiles", kStarterWheat);
	context.telemetry.measure("vultures.starter.requested-wood-tiles", kStarterWood);
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		// Resource eligibility is a hard constraint, not a score: never repair a failed food kit
		// by watering it. Search can cross the home boundary when a compact clearing is mostly wet;
		// final walking validation rejects a geometrically close but inaccessible patch.
		const auto dry = [&](int i) { return free(i) && fertility.at(i % t.w, i / t.w) == 0; };
		const auto wet = [&](int i) { return free(i) && L.homeOf[i] == k && shore[i]; };
		// A compact home can have a tiny dry sliver and a larger exterior field separated by
		// sand. Spend the budget across both instead of failing after the first tiny component.
		const auto rationPatches = growPatchesNear(map, t, context.bootX[k], context.bootY[k],
												   kWheatReach, WHEAT, kStarterWheat, dry);
		const int wheat = rationPatches.tiles;
		context.telemetry.measure("vultures.starter.wheat-patches", rationPatches.patches, k);
		if (rationPatches.patches > 1)
			context.telemetry.fallback("vultures.starter.split-rations",
									   "Starting rations span disconnected dry pockets", k);
		// Apply the same disconnected-pocket budget to shoreline wood; a beach can split
		// eligible grass into several components just as a sand ring splits the dry food ground.
		const auto timberPatches = growPatchesNear(map, t, int(L.kits[k].x), int(L.kits[k].y),
												   kPondSearch, WOOD, kStarterWood, wet);
		const int wood = timberPatches.tiles;
		context.telemetry.measure("vultures.starter.wood-patches", timberPatches.patches, k);
		if (timberPatches.patches > 1)
			context.telemetry.fallback("vultures.starter.split-wood",
									   "Starting wood spans disconnected shoreline pockets", k);
		context.telemetry.measure("vultures.starter.wheat-tiles", wheat, k);
		context.telemetry.measure("vultures.starter.wood-tiles", wood, k);
		// A partial kit is a failed candidate. The service discards this world and the lobby may
		// try another recorded seed; this function never changes the user's settings or retries
		// with a time-dependent budget.
		if (wheat < kStarterWheat || wood < kStarterWood)
		{
			context.detail = "No room for dry starting rations and shoreline wood; use a bigger "
							 "map or fewer colonies.";
			return false;
		}
		const auto quarry = [&](int i) { return free(i) && L.homeOf[i] == k; };
		const auto quarryPatches =
			growPatchesNear(map, t, int(L.kits[k].x) + kQuarryOffset, int(L.kits[k].y), kPondSearch,
							STONE, kQuarryTiles, quarry);
		const int quarryTiles = quarryPatches.tiles;
		context.telemetry.measure("vultures.starter.quarry-patches", quarryPatches.patches, k);
		if (quarryPatches.patches > 1)
			context.telemetry.fallback("vultures.starter.split-quarry",
									   "Starting quarry spans disconnected building-free pockets",
									   k);
		context.telemetry.measure("vultures.starter.quarry-tiles", quarryTiles, k);
		if (quarryTiles < kQuarryTiles)
		{
			context.detail = "No room for the starting quarry; use a bigger map or fewer colonies.";
			return false;
		}
	}
	context.stage = "vultures fields";
	const auto noise = periodicNoise(t.w, t.h, kFieldOctaves, context.stream("vultures-fields"));
	std::vector<int> levels;
	for (int i = 0; i < t.size(); ++i)
		if (L.forest[i] && fertility.at(i % t.w, i / t.w) == 0 && free(i))
			levels.push_back(noise[i]);
	const int cover = std::min(100, int(scaledCount(kWheatCover, o.wheat)));
	const int threshold = percentile(levels, 100 - cover);
	const int wheat = plantCover(map, t, L.forest, WHEAT,
								 [&](int i)
								 {
									 return cover > 0 && noise[i] >= threshold && free(i) &&
											fertility.at(i % t.w, i / t.w) == 0;
								 });
	const int wood = plantCover(map, t, shore, WOOD,
								[&](int i)
								{
									return free(i) && context.bounded("vultures-wood", 100) <
														  scaledCount(kWoodCover, o.wood);
								});
	context.telemetry.measure("vultures.fields.cover-percent", cover);
	context.telemetry.measure("vultures.fields.eligible-tiles", levels.size());
	context.telemetry.measure("vultures.fields.wheat-tiles", wheat);
	context.telemetry.measure("vultures.shores.wood-tiles", wood);
	// Repairs may only remove deposits: the generic crop guarantee could plant renewable wheat.
	context.stage = "vultures trails";
	// One open step costs 1, a harvest/clear step 3: favor existing gaps without forcing a
	// huge detour. Water and permanent deposits remain impassable. This is route selection,
	// not a claim that clearing takes three simulation ticks. The final walk check is decisive.
	const bool trails = openColonyRoutes(map, context, t, StepCosts::chopping(3));
	context.telemetry.measure("vultures.trails.cleared", trails);
	openCrampedStarts(game, context, kRoomSites, kRoomRange);
	// Capping after every repair makes the final total honest. Ordinary setResource varies
	// stock/sprites through engine RNG; the cap adds no draws and never replenishes a deposit.
	const ResourceStock food = capResourceStock(map, WHEAT, kFoodPerTile);
	context.telemetry.measure("vultures.food.total-rations", food.amount);
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const auto error = designMismatch(L, map, "vultures"); !error.empty())
		return error;
	const Torus &t = L.t;
	// Ungated fertility asks whether ANY wheat placed here could grow. A deposit-gated field
	// would incorrectly call unplanted but irrigated ground dry.
	const auto fertility = Fertility::forMap(map, false);
	const auto shore = dilate(t, pureTiles(map, WATER), kShoreReach);
	for (int i = 0; i < t.size(); ++i)
	{
		const auto &r = map.getResource(i);
		if (r.type == WHEAT && (fertility.at(i % t.w, i / t.w) != 0 || r.amount != kFoodPerTile))
			return "Wheat must be dry and hold exactly one ration.";
		if (r.type == WOOD && !shore[i])
			return "Wood must stand within eight tiles of water.";
	}
	// Evaluate the completed world, including any trail and room repairs.
	if (const auto error = startingAccessFailure(
			map, context.request.nbTeams,
			{{WHEAT, kWheatReach, "dry wheat"}, {WOOD, kWoodReach, "shoreline wood"}}, kRoomSites,
			kRoomRange);
		!error.empty())
		return error + " Use a bigger map or fewer colonies.";
	return walkFromFirstColony(map, context.request.nbTeams, "the fields", "along the trails")
		.error;
}
} // namespace
VulturesOptions::VulturesOptions(const GenerationRequest &r)
	: homeSize(r.option("home-size")), lakes(r.option("lakes")), lakeSize(r.option("lake-size")),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount"))
{
}
GeneratorDefinition vulturesDefinition()
{
	// The same 16..30 home-radius range retains the Old Growth scale while leaving room for
	// a central pond, swarm and dry-side stock. Lakes are requested per 128x128 area, in 40..160
	// corner patches; defaults are one 90-corner lake. The shared layout may omit a lake, but
	// never shrink its exclusion zone to force one in. Resource percentages are 0..200.
	return {"vultures",
			47,
			"Vultures",
			2,
			false,
			{{"home-size", "Home size", 16, 30, 1, 24, ControlGroup::Layout},
			 {"lakes", "Lakes", 0, 4, 1, 1, ControlGroup::Terrain},
			 {"lake-size", "Lake size", 40, 160, 10, 90, ControlGroup::Terrain},
			 GeneratorControl::percentage("wheat-amount", "Wheat amount", 200),
			 GeneratorControl::percentage("wood-amount", "Wood amount", 200)},
			generate,
			true,
			designFailure<design>,
			validateWorld,
			// Wheat/wood access dominate (30%/25%); stock 20%, room 15%, separation 10%.
			// Fertility gets zero weight: rewarding renewable food would contradict the concept.
			{0.30, 0.25, 0.0, 0.20, 0.15, 0.10}};
}
