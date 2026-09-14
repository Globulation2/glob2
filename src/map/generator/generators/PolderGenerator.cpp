// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolderGenerator.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Farmland.h"
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
//
// FEEDBACK 2026-09-13 (first play): "can we randomize the angle that the farms run at each round?
// also, the bases are wayyy so small and the aggressive growth quickly crowds out the base, so the
// bases need to be surrounded by a sand ring to hold back the growth. change the Row Angle parameter
// to have options Random, Vertical, Horizontal, Diagonal; Random ideally chooses any random angle,
// not just from a couple of options. I also want the farm lands stamped with the 10x4 sand plots
// similar to other map gens using farm lands: 2.5x the number of players, spread out evenly across
// the map (except some distance away from player bases), so the player is really encouraged to
// spread into these zones to extract food rapidly." So: `row-angle` defaults to Random, an angle
// drawn per map and rounded to the whole turn counts a stripe field needs (about a tenth of a turn's
// steps on a 256 map, so it is any angle to the eye); villages are radius 14 (was 11) inside a
// two-tile ring of sand the crops cannot cross; and two and a half farm plots per colony are stamped
// through the rows, each the farthest a plot can stand from every village and every plot before it.
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
// A ring of sand this wide round every village (first play: growth "quickly crowds out the base"):
// crops spread only onto grass, so one tile of sand corners stops them and the second keeps a grass
// corner from ever bridging the ring.
constexpr int kVillageSand = 2;
// Farm plots (first play): two and a half per colony (rounded up), each at least this far from every
// village and hamlet and from the plots before it, so they are dotted evenly through the rows.
constexpr int kPlotsPerTwoColonies = 5, kPlotClear = 18, kPlotMargin = 9;

struct Layout
{
	Torus t{1, 1};
	StripeStyle rows, along;
	double spacing = 0, crops = 0, villageRadius = 0;
	std::vector<ShapePoint> homes, kits, hamlets;
	std::vector<int> homeOf;
	std::vector<unsigned char> water, dyke, village, ring, hamlet;
	Farm farm;            // the rows' water and sand as a farm, so plots can be stamped into them
	TerrainSketch sketch; // the terrain as designed, plots and all
	std::string failure;
};

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const PolderOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);

	// The rows: stripes that wrap exactly, so their direction is a pair of whole turn counts across the
	// width and down the height. Horizontal rows cross the map `h / 16` times from top to bottom,
	// vertical ones `w / 16` times across; diagonal rows climb as many times across as down; and a
	// random angle (first play; the default) is drawn as any angle and rounded to the nearest whole
	// counts on a circle of `s / 16` (s the shorter side: 16 on a 256 map), so the rows stay about 16
	// apart at any angle and the angle is one of some fifty on a 256 map. Crops take 10 of every 16
	// along an axis and 11 on the diagonal (Farmland.h's fit), by the sine of twice the angle.
	L.rows.warpPercent = 0;
	{
		const int s = std::min(t.w, t.h), turns = std::max(1, s / kStraightPeriod);
		const double draw = context.bounded("polder-angle", 3600) / 3600.0 * kPi;
		const double theta = o.rowAngle == 0   ? draw
							 : o.rowAngle == 1 ? 0.0
							 : o.rowAngle == 2 ? kPi / 2
											   : kPi / 4;
		L.rows.acrossX = int(std::lround(turns * std::cos(theta) * t.w / s));
		L.rows.acrossY = int(std::lround(turns * std::sin(theta) * t.h / s));
		if (o.rowAngle == 3)
		{
			L.rows.acrossX = std::max(1, t.w / kDiagonalStep);
			L.rows.acrossY = std::max(1, t.h / kDiagonalStep);
		}
		if (L.rows.acrossX == 0 && L.rows.acrossY == 0)
			L.rows.acrossY = turns;
		const double angle = std::atan2(double(L.rows.acrossY) / t.h, double(L.rows.acrossX) / t.w);
		L.crops = kStraightCrops +
				  std::lround((kDiagonalCrops - kStraightCrops) * std::fabs(std::sin(2 * angle)));
	}
	L.spacing = stripeSpacing(t, L.rows);
	L.along = alongStripes(t, L.rows);
	const std::vector<int> phase = stripePhase(t, L.rows, context.stream("polder-rows"));
	const std::vector<int> along = stripePhase(t, L.along, context.stream("polder-rows"));

	// The villages: on a lattice, shrunk so a whole row of crops and ditch always lies between two.
	L.homes = latticeSites(t.w, t.h, teams, context.bounded("polder-layout", std::uint32_t(t.w)),
						   context.bounded("polder-layout", std::uint32_t(t.h)))
				  .sites;
	dealStarts(context, L.homes); // which colony gets which site is a draw, not the order
	double nearest = std::min(t.w, t.h);
	for (size_t a = 0; a < L.homes.size(); ++a)
		for (size_t b = a + 1; b < L.homes.size(); ++b)
			nearest =
				std::min(nearest, std::hypot(t.offsetX(int(L.homes[a].x), int(L.homes[b].x)),
											 t.offsetY(int(L.homes[a].y), int(L.homes[b].y))));
	L.villageRadius =
		std::min<double>(o.villageSize, std::floor((nearest - L.spacing) / 2 - kVillageSand));
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
	// The ring of sand round every village (first play), outside its rough edge.
	L.ring = dilate(t, L.village, kVillageSand);
	for (int i = 0; i < n; ++i)
		L.ring[i] = L.ring[i] && !L.village[i];
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
		if (L.village[i] || L.ring[i] || L.hamlet[i])
			continue;
		const int offset = along[i] % dykePeriod;
		const bool onDyke = offset < dykeHalf || dykePeriod - offset < dykeHalf;
		if (phase[i] >= cropUnits)
			(onDyke ? L.dyke[i] : L.water[i]) = 1;
	}
	// The terrain as designed, and the rows as a farm so plots can be stamped into them
	// (stampFarmPlot): two and a half plots per colony (first play), each the farthest a plot can
	// stand from every village, hamlet and earlier plot, with the whole plot and its ring at least
	// kPlotMargin inside that room.
	L.sketch.assign(n, GRASS);
	for (int i = 0; i < n; ++i)
		L.sketch[i] = L.water[i] ? WATER : (L.dyke[i] || L.ring[i]) ? SAND : GRASS;
	L.farm.water = L.water;
	L.farm.sand = L.dyke;
	L.farm.plot.assign(n, 0);
	L.farm.row.assign(n, -1);
	const FarmPlot plot;
	std::vector<unsigned char> keepClear(n, 0);
	for (int i = 0; i < n; ++i)
		keepClear[i] = L.village[i] || L.ring[i] || L.hamlet[i];
	keepClear = dilate(t, keepClear, kPlotClear);
	const int plots = (teams * kPlotsPerTwoColonies + 1) / 2;
	for (int p = 0; p < plots; ++p)
	{
		const std::vector<std::int64_t> far = distanceSquaredTo(t, keepClear);
		int site = -1;
		for (int i = 0; i < n; ++i)
			if (!keepClear[i] && (site < 0 || far[i] > far[site]))
				site = i;
		if (site < 0 || far[site] < std::int64_t(kPlotMargin) * kPlotMargin)
			break;
		const int x0 = site % t.w - plot.width / 2, y0 = site / t.w - plot.height / 2;
		stampFarmPlot(L.sketch, t, L.farm, x0, y0, plot);
		for (int dy = -kPlotMargin; dy <= kPlotMargin; ++dy)
			for (int dx = -kPlotMargin; dx <= kPlotMargin; ++dx)
				keepClear[t.at(site % t.w + dx, site / t.w + dy)] = 1;
	}
	L.water = L.farm.water;
	L.dyke = L.farm.sand;
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
	TerrainSketch terrain = L.sketch;
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
	{
		return !L.village[i] && !L.hamlet[i] && !L.farm.plot[i] && !reserved[i] &&
			   clearGround(map, i % t.w, i / t.w);
	};
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
	clearFarmPlots(map, t, {L.farm});
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
		3,
		false,
		// A dyke every 24 tiles is a lane every one and a half rows' walk; villages of radius 11
		// hold a swarm, its kit and a few more buildings and no more.
		// FEEDBACK 2026-09-13: a random angle by default; villages of 14 (was 11) in a sand ring.
		{GeneratorControl::choice("row-angle", "Row angle",
								  {"Random", "Vertical", "Horizontal", "Diagonal"}, 0,
								  ControlGroup::Terrain),
		 {"dyke-spacing", "Dyke spacing", 12, 48, 4, 24, ControlGroup::Terrain},
		 {"village-size", "Village size", 8, 20, 1, 14, ControlGroup::Layout},
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
