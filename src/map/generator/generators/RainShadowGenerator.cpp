// SPDX-License-Identifier: GPL-3.0-or-later
#include "RainShadowGenerator.h"
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
#include "Points.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>
using namespace MapGeneration;

// Rain shadow: a map with a grain. Long stone ridges run right across the torus, parallel and a
// valley apart, and the wind blows across them from one side. The windward foot of every ridge
// catches the rain: a chain of pools runs along it and the farmland grows there. The lee side is
// dry: a band of sand behind every ridge where nothing grows and nothing can be built. Every ridge
// is cut by passes at regular intervals, staggered from ridge to ridge, so moving along a valley is
// easy and crossing the ridges is slow.
//
// A colony's home sits in the middle of a valley: its lush side faces the windward foot of the next
// ridge, and behind it lies the dry back of the ridge it shelters under. An attacker crossing a pass
// arrives on barren sand facing a defender standing on fields; a defender who sallies out crosses
// the same sand. Nothing here is turned round a centre: the ridges are stripes that wrap the torus
// exactly at their slant (Patterns.h), the homes sit on a lattice (Orbits.h) snapped to the middle
// of their valleys, and fairness comes from every valley being the same valley.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Stone is a wall no unit
// crosses and nobody clears, so the passes are doors worth holding, and being staggered no straight
// road runs across the map. Crops regrow only near water and never beside sand, so the windward
// pools decide where the fields are and the lee sand is a desert even though it is walkable: an
// army on it is far from any inn. Sand also carries no building, so a forward base has to wait for
// the next valley.
//
// FEEDBACK 2026-09-13 (first play): "default amount of water is not enough. the ponds need to be
// deeper and more connected to each other. also, for the passes that connect between the ridges, we
// need sand roadways leading inland to prevent those chasms from being overgrown because they get
// clogged up too easily. also in the valleys between the mountains it just feels too empty; toss in
// a few rivers connected to some inland lakes connecting from the rain shadows, and some inland
// desert or sporadic sand patches to break up the texture." So: the windward foot is a chain of
// streams 7 deep and 20 long every 24 (was 3 deep, 12 long: nearly continuous water now, with a
// four-tile beach gap between streams); a sand road runs through every pass and kPassRoad tiles into
// the valley on both sides; inland lakes lie scattered along the middle of every valley, each joined
// by a wandering river to the nearest windward stream; and sand patches are sprinkled over the
// valleys' grass (`sand-patches` percent).
//
// FEEDBACK 2026-09-14 (second play): "make the default lakes/ponds on the rain side of the mountain a
// couple tiles larger. still too small right now. Also the sand roads that go between the ridges need
// to extend further into either side of the base and should connect more smoothly to the beaches
// around the water instead of being distinct from them." So the streams are 7 deep (was 5), the pass
// roads run 18 tiles into the valley on both sides (was 12), and on the windward side each road ends
// in a lane of sand along the middle of the foot, out to the streams either side of the pass, so the
// road runs into their beaches instead of stopping short of them in the grass.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say: wheat and wood beside the home's own
// pond, and a quarry, though stone is everywhere on this map.
constexpr int kHomeWheat = 14, kHomeWood = 12, kHomeQuarry = 2;
// The ground a home clearing keeps from the ridges, the sand and the pools on either side: its own
// pond's beach (2 tiles) and a lane to walk round the home (2 more).
constexpr int kHomeMargin = 4;
// The ridge's own wobble, as a share of the valley spacing: enough that the ridges read as
// mountains rather than ruled lines, little enough that a valley never pinches shut (the pools and
// homes are placed by the warped phase, so they follow the wobble).
constexpr int kRidgeWarpPercent = 12;
// Ridge wobble noise cells, in tiles: features a few times longer than a pass, so a ridge sways
// rather than shivers.
constexpr int kRidgeWarpPeriod = 40;
// The windward foot: pools start this many tiles out from the ridge's stone and run this many tiles
// deep into the valley. Stone stands only on pure grass, a pool's beach is a ring of sand corners
// one out from its water, and a corner spoils the four tiles round it, so a pool three tiles out
// would take the ridge's windward row with it (measured: the ridge came out one row thick); four
// tiles leaves all three rows standing.
constexpr int kFootGap = 4, kFootDepth = 7;
// Streams along the foot: each this long along the ridge, every this many tiles (first play: 20 every
// 24, from 12 every 24; second play: 7 deep, from 5). A stream 20 long and 7 deep is about 140 corners
// of water, which the engine's growth probe finds from most of the valley; the four-tile gaps between
// streams, once their beaches meet, are where the valley's beach lets units walk along the foot.
constexpr int kPoolLength = 20, kPoolSpacing = 24;
// A sand road through every pass, running this far into the valley on either side of the stone
// (first play: the passes "get clogged up too easily"; second play: 18, from 12, to reach well into
// the bases): a line of single sand corners, which nothing grows onto and nothing is built across. No
// stream lies within kPassClear tiles along the ridge of a pass, so the road never runs into water; on
// the windward side it ends in a lane along the foot that joins the streams' beaches (second play).
constexpr int kPassRoad = 18, kPassClear = 4;
// Inland lakes (first play: "a few rivers connected to some inland lakes"): sites at least this far
// apart along the middle of each valley (the band within kLakeBand of the valley's middle phase), each
// a rough disc of this radius, joined to the nearest windward stream by a river this many corners
// wide (one tile of water, with the fords a wandering line leaves) wandering by up to kRiverWander.
constexpr int kLakeSpacing = 56;
constexpr double kLakeBand = 0.18, kLakeRadius = 5.0, kRiverHalfWidth = 1.0, kRiverWander = 4.0;

struct Layout
{
	Torus t{1, 1};
	StripeStyle across, along; // phase across the ridges (0 on a ridge) and along them
	int ridges = 0;            // crossings of the map, after any taken away to fit
	double spacing = 0;        // between ridges, in tiles
	double homeRadius = 0;
	int windX = 0, windY = 0; // the wind's direction in whole steps
	std::vector<ShapePoint> homes, kits;
	std::vector<int> homeOf;
	std::vector<unsigned char> stone, water, sand, road, clearing;
	std::vector<Site> lakes;
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const RainShadowOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// The ridges: `ridges` crossings of the map from top to bottom, tilted so that following one
	// across the width climbs `slant` ridges. With a slant every ridge is one long spiral round the
	// torus, which is what keeps the passes staggered from one crossing to the next. The valley
	// between two ridges must hold a home with its margins, the windward pools and the lee sand: on a
	// small map the ridges are taken away one at a time until the valleys are wide enough (four
	// ridges suit a 256 map; a 128 map gets two), then the homes shrink to what is left, and only then
	// is the map refused.
	L.across.acrossX = o.slant;
	L.across.warpPeriod = kRidgeWarpPeriod;
	L.across.warpPercent = kRidgeWarpPercent;
	const double taken = kFootGap + kFootDepth + o.leeWidth;
	const double smallest = homePondRadius(o.homeSize) + kHomePondGap + kHomeSwarmRoom;
	for (L.ridges = o.ridges; L.ridges > 2; --L.ridges)
	{
		L.across.acrossY = L.ridges;
		if ((stripeSpacing(t, L.across) - o.ridgeThickness - taken) / 2 - kHomeMargin >= smallest)
			break;
	}
	L.across.acrossY = L.ridges;
	L.along = alongStripes(t, L.across);
	L.spacing = stripeSpacing(t, L.across);
	const double alongLength = stripeSpacing(t, L.along);
	// Phase units: 65536 is one spacing across, or one turn along.
	const auto acrossUnits = [&](double tiles) { return int(tiles / L.spacing * 65536); };
	const auto alongUnits = [&](double tiles) { return int(tiles / alongLength * 65536); };
	const double valley = L.spacing - o.ridgeThickness;
	L.homeRadius = std::min<double>(o.homeSize, std::floor((valley - taken) / 2 - kHomeMargin));
	context.telemetry.measure("rain-shadow.ridges.actual", L.ridges);
	context.telemetry.measure("rain-shadow.valleys.spacing", L.spacing);
	context.telemetry.measure("rain-shadow.homes.actual-radius", L.homeRadius);
	if (L.ridges < o.ridges)
		context.telemetry.fallback("rain-shadow.ridges.reduced",
								   "Fewer ridges needed for viable valleys");
	if (L.homeRadius < o.homeSize)
		context.telemetry.fallback("rain-shadow.homes.shrunk", "Homes shrank to the valley budget");
	if (!homeHasRoom(L.homeRadius))
	{
		L.failure =
			"The valleys are too narrow for the homes; use fewer ridges, thinner ridges or a "
			"bigger map.";
		return L;
	}

	const std::vector<int> phase = stripePhase(t, L.across, context.stream("rain-ridges"));
	const std::vector<int> along = stripePhase(t, L.along, context.stream("rain-ridges"));
	// The wind blows towards rising phase: its direction is the across field's normal, as whole steps.
	{
		const int g = std::gcd(std::max(1, o.slant * t.h), L.ridges * t.w);
		L.windX = o.slant * t.h / g;
		L.windY = L.ridges * t.w / g;
	}

	// Stone on the ridge's band of phase, except at the passes. Passes are cut every `passSpacing`
	// tiles along the ridge (as many whole passes as fit in one turn along it, never fewer than one),
	// and on straight ridges every other ridge's passes are shifted by half a spacing, so no pass
	// lines up with the one on the next ridge; a slanted ridge staggers its own passes, since each
	// crossing of the map shifts the along phase by a fraction of a turn.
	const int ridgeHalf = acrossUnits(o.ridgeThickness / 2.0);
	const int passes = std::max(1, int(std::lround(alongLength / o.passSpacing)));
	const int passPeriod = 65536 / passes, passHalf = alongUnits(o.passWidth / 2.0);
	L.stone.assign(n, 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = y * t.w + x;
			if (stripeDistance(phase[i]) >= ridgeHalf)
				continue;
			int shift = 0;
			if (o.slant == 0)
				shift = ((L.ridges * y / t.h) % 2) * passPeriod / 2;
			const int offset = ((along[i] + shift) % passPeriod + passPeriod) % passPeriod;
			const bool pass = offset < passHalf || passPeriod - offset < passHalf;
			L.stone[i] = !pass;
		}

	// Windward pools: a chain along the foot of every ridge on the side the wind comes from, which
	// is the side of falling phase (the wind blows towards rising phase, so it arrives at the ridge
	// from below its phase). Each pool runs kPoolLength along the ridge every kPoolSpacing, and
	// kFootGap to kFootGap + kFootDepth out from the stone. Lee sand: the band on the other side, as
	// far as leeWidth, measured downwind from the stone itself (upwindSteps against the wind) so the
	// sand stops where a pass lets the rain through.
	L.water.assign(n, 0);
	L.sand.assign(n, 0);
	L.road.assign(n, 0);
	const int footNear = ridgeHalf + acrossUnits(kFootGap),
			  footFar = footNear + acrossUnits(kFootDepth);
	const int poolPeriod = alongUnits(kPoolSpacing), poolHalf = alongUnits(kPoolLength / 2.0);
	const int passClear = passHalf + alongUnits(kPassClear);
	// Where a pass's line runs along the ridge, given the along phase and the row's pass shift (the
	// same shift the stone uses).
	const auto passOffset = [&](int i)
	{
		const int y = i / t.w;
		const int shift = o.slant == 0 ? ((L.ridges * y / t.h) % 2) * passPeriod / 2 : 0;
		const int offset = ((along[i] + shift) % passPeriod + passPeriod) % passPeriod;
		return std::min(offset, passPeriod - offset);
	};
	const int footMiddle = (footNear + footFar) / 2;
	for (int i = 0; i < n; ++i)
	{
		const int below = 65536 - phase[i]; // how far below the next ridge's crest
		if (below >= footNear && below < footFar && passOffset(i) > passClear)
		{
			const int offset = along[i] % std::max(1, poolPeriod);
			if (offset < poolHalf || poolPeriod - offset < poolHalf)
				L.water[i] = 1;
		}
		// The pass road: the pass's centre line, from kPassRoad tiles before the ridge to as far past.
		if (passOffset(i) <= alongUnits(0.5) &&
			stripeDistance(phase[i]) < ridgeHalf + acrossUnits(kPassRoad))
			L.road[i] = 1;
		// The road's junction with the foot (second play): a lane of sand two tiles across along the
		// middle of the foot band, from the pass out to the streams on either side of it, so the road
		// meets their beaches rather than stopping in the grass a few tiles short. It runs one tile
		// past kPassClear so it touches the first tile of each stream's beach.
		if (passOffset(i) <= passClear + alongUnits(1) && below >= footMiddle - acrossUnits(1) &&
			below < footMiddle + acrossUnits(1) && !L.water[i])
			L.road[i] = 1;
	}
	// The sand starts two tiles behind the stone, not one: a sand corner spoils the tiles round it
	// and stone stands only on pure grass, so sand touching the ridge would strip its lee row.
	const std::vector<int> shadow = upwindSteps(t, L.stone, L.windX, L.windY, o.leeWidth + 1);
	for (int i = 0; i < n; ++i)
		L.sand[i] = shadow[i] >= 2;
	// Inland lakes along the middle of every valley (first play: the valleys "feel too empty"): sites
	// thrown at least kLakeSpacing apart over the map and kept where the across phase is within
	// kLakeBand of the valley's middle, each a rough disc, joined to the nearest stream tile by a
	// wandering river. Rivers leave the lee sand alone but cross the valley's grass.
	if (o.inlandLakes)
	{
		const RadialShape lake(kLakeRadius, 0.35, context, "rain-lakes");
		std::mt19937 &rivers = context.stream("rain-rivers");
		std::vector<unsigned char> streams = L.water;
		for (const Site &site : spreadPoints(t, kLakeSpacing, context, "rain-lakes"))
		{
			const int at = t.at(site.x, site.y);
			if (std::abs(phase[at] - 32768) > kLakeBand * 65536)
				continue;
			L.lakes.push_back(site);
			fillShape(L.water, t, site.x + 0.5, site.y + 0.5, lake, 0.0);
			int nearest = -1;
			for (int i = 0; i < n; ++i)
				if (streams[i] &&
					(nearest < 0 || t.dist2(site.x, site.y, i % t.w, i / t.w) <
										t.dist2(site.x, site.y, nearest % t.w, nearest / t.w)))
					nearest = i;
			if (nearest < 0)
				continue;
			const ShapePoint mouth{site.x + 0.5 + t.offsetX(site.x, nearest % t.w),
								   site.y + 0.5 + t.offsetY(site.y, nearest / t.w)};
			strokePath(L.water, t,
					   wanderingPath(t, {site.x + 0.5, site.y + 0.5}, mouth, kRiverHalfWidth,
									 kRiverWander, 0.0, rivers));
		}
	}
	for (int i = 0; i < n; ++i)
		if (L.water[i])
			L.road[i] = L.sand[i] = 0;

	// Homes: on a lattice, each slid across its valley to the middle of it (phase half a turn), so
	// every home has the same ridge behind it and the same foot before it. The clearing round a home
	// has no pools, no sand and no stone.
	const LatticeSites lattice =
		latticeSites(t.w, t.h, teams, context.bounded("rain-layout", std::uint32_t(t.w)),
					 context.bounded("rain-layout", std::uint32_t(t.h)));
	const double nx = L.windX / std::hypot(L.windX, L.windY),
				 ny = L.windY / std::hypot(L.windX, L.windY);
	L.clearing.assign(n, 0);
	for (ShapePoint site : lattice.sites)
	{
		const int at = t.at(int(site.x), int(site.y));
		// Slide along the wind (the normal) by the phase's shortfall from half a turn.
		const double shortfall = (32768 - phase[at]) / 65536.0 * L.spacing;
		site.x = std::fmod(site.x + shortfall * nx + t.w, t.w);
		site.y = std::fmod(site.y + shortfall * ny + t.h, t.h);
		L.homes.push_back(site);
		for (int i = 0; i < n; ++i)
			if (std::hypot(t.offsetX(int(site.x), i % t.w), t.offsetY(int(site.y), i / t.w)) <=
				L.homeRadius + kHomeMargin)
				L.clearing[i] = 1;
	}
	dealStarts(context, L.homes); // which colony gets which valley site is a draw, not the order
	const double nearest = nearestSiteDistance(t, L.homes);
	if (nearest < 2 * (L.homeRadius + kHomeMargin))
	{
		L.failure = "Too many colonies for this map; use a bigger map or fewer colonies.";
		return L;
	}
	for (int i = 0; i < n; ++i)
		if (L.clearing[i])
			L.water[i] = L.sand[i] = L.stone[i] = L.road[i] = 0;
	L.homeOf.assign(n, -1);
	const RadialShape home(L.homeRadius, 0.15, context, "rain-home");
	const RadialShape pond(homePondRadius(L.homeRadius), 0.3, context, "rain-pond");
	L.kits = stampRoundHomes(t, L.homes, 0.0, home, L.homeRadius, 1, &pond, L.water, L.homeOf);
	context.telemetry.measure("rain-shadow.passes.per-turn", passes);
	context.telemetry.measure("rain-shadow.lakes.actual", L.lakes.size());
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "rain shadow layout";
	const RainShadowOptions o(context.request);
	const Layout L = design(context.request, context);
	if (!L.failure.empty())
	{
		context.telemetry.fallback("rain-shadow.layout.failure", L.failure);
		context.detail = L.failure;
		return false;
	}
	Map &map = game.map;
	const Torus &t = L.t;
	const int n = t.size(), teams = context.request.nbTeams;
	for (int k = 0; k < teams; ++k)
		game.addTeam();

	context.stage = "rain shadow terrain";
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
		terrain[i] = L.water[i] ? WATER : (L.sand[i] || L.road[i]) ? SAND : GRASS;
	// Sand patches over the valleys' grass (first play: "some inland desert or sporadic sand patches
	// to break up the texture"): `sand-patches` percent of the grass at least three tiles from any
	// water, in patches from a noise field, kept off the homes, the stone and the roads.
	{
		std::vector<unsigned char> valley(n, 0);
		for (int i = 0; i < n; ++i)
			valley[i] = !L.stone[i] && !L.road[i] && !L.sand[i] && L.homeOf[i] < 0;
		const std::vector<int> patches =
			periodicNoise(t.w, t.h, 14, context.stream("rain-patches"));
		sprinkleSand(terrain, t, valley, o.sandPatches / 100.0, 3,
					 [&](int i) { return patches[i]; });
	}
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	// Stone stands only on pure grass; a ridge tile the beaches or the lee sand spoiled is left out,
	// which is why the pools keep kFootGap from the stone and the sand lies on the far side.
	for (int i = 0; i < n; ++i)
		if (L.stone[i] && map.isResourceAllowed(i % t.w, i / t.w, STONE))
			map.setResource(i % t.w, i / t.w, STONE, 1);

	context.stage = "rain shadow colonies";
	if (!settleRoundColonies(game, context, "rain-starts", L.homeOf, L.homes, L.homeRadius))
		return false;

	context.stage = "rain shadow resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
		plantOpenHomeKit(
			map, t, context, L.kits[k], 0.0, L.homeRadius, kHomeWheat, kHomeWood, kHomeQuarry,
			[&](int i)
			{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); });
	// The fields: crops on the fertile ground, which the pools make the windward side of every valley
	// (the growth probe reaches 15 tiles from water and refuses beside sand, so the lee band grows
	// nothing). A third of the fertile ground under wheat and a fifth under wood is a working
	// countryside, in patches so it can be walked and built on; outcrops are few, the ridges being
	// stone enough.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 10, context.stream("rain-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 5, context.stream("rain-split"));
	const auto eligible = [&](int i)
	{
		return L.homeOf[i] < 0 && !reserved[i] && !L.stone[i] && clearGround(map, i % t.w, i / t.w);
	};
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += eligible(i) && fertility.at(i % t.w, i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, eligible, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(fertile * 30 / 100, o.wheat)),
								 int(scaledCount(fertile * 20 / 100, o.wood)),
								 int(scaledCount(area / 3000, o.stone)),
								 int(scaledCount(area / 2000, o.fruit))};
		},
		"rain-stone", "rain-fruit");
	seedAlgae(map, context, t, "rain-algae", o.algae, AlgaeBand::anyWater(30));
	secureStartingCrops(game, context, t, 24, 32, 0, &L.stone);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&L.stone);
	// The passes join every valley, but the fields can close one; the cheapest cut through crops is
	// opened, never through stone or water.
	context.stage = "rain shadow routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "rain shadow"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	if (const std::string lost =
			homePondMissing(map, t, L.kits, context.request.nbTeams, "home", "pond");
		!lost.empty())
		return lost;
	// Every designed ridge tile that could hold stone does: the ridges are the map's walls.
	for (int i = 0; i < t.size(); ++i)
		if (L.stone[i] && map.isGrass(i % t.w, i / t.w) &&
			map.getResource(i % t.w, i / t.w).type != STONE)
			return "A ridge has lost its stone at (" + std::to_string(i % t.w) + ", " +
				   std::to_string(i / t.w) + ").";
	return walkFromFirstColony(map, context.request.nbTeams, "the valleys", "through the passes")
		.error;
}
} // namespace

RainShadowOptions::RainShadowOptions(const GenerationRequest &r)
	: ridges(r.option("ridges")), slant(r.option("slant")),
	  ridgeThickness(r.option("ridge-thickness")), passSpacing(r.option("pass-spacing")),
	  passWidth(r.option("pass-width")), leeWidth(r.option("lee-width")),
	  homeSize(r.option("home-size")), sandPatches(r.option("sand-patches")),
	  inlandLakes(r.option("inland-lakes") != 0), wheat(r.option("wheat-amount")),
	  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
	  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition rainShadowDefinition()
{
	return {
		"rain-shadow",
		27,
		"Rain shadow",
		4,
		false,
		// Four ridges on a 256 map give 64-tile valleys: a 12-tile home, a pass every 48 tiles
		// and a lee band of 6 leave a valley wide enough to farm and to fight in. Ridges three
		// thick are sealed against diagonal steps and thin enough for a level-1 tower to shoot
		// across (Walls.h's towerReach); passes five wide take a column of units and can be
		// walled shut by whoever holds them.
		{{"ridges", "Ridges", 2, 8, 1, 4, ControlGroup::Terrain},
		 {"slant", "Slant", 0, 3, 1, 1, ControlGroup::Terrain},
		 {"ridge-thickness", "Ridge thickness", 2, 6, 1, 3, ControlGroup::Terrain},
		 {"pass-spacing", "Pass spacing", 24, 96, 8, 48, ControlGroup::Terrain},
		 {"pass-width", "Pass width", 3, 9, 1, 5, ControlGroup::Terrain},
		 {"lee-width", "Lee width", 2, 12, 1, 6, ControlGroup::Terrain},
		 {"home-size", "Home size", 10, 20, 1, 12, ControlGroup::Layout},
		 // FEEDBACK 2026-09-13: lakes with rivers in every valley, and 6% of the valleys' grass in
		 // sand patches.
		 GeneratorControl::toggle("inland-lakes", "Inland lakes", true, ControlGroup::Terrain),
		 {"sand-patches", "Sand patches", 0, 20, 2, 6, ControlGroup::Terrain},
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
