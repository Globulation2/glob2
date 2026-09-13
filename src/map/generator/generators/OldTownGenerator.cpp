// SPDX-License-Identifier: GPL-3.0-or-later
#include "OldTownGenerator.h"
#include "Farmland.h"
#include "FertilityField.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "Tessellation.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
using namespace MapGeneration;

// Old town: a city of stone blocks and narrow grass streets, with farmland all round it.
// The city is a warped grid of irregular blocks (Tessellation.h); every block is solid stone, every
// street between blocks is grass, so the streets form a dense grid with a flanking route round every
// corner, unlike a maze's single ways. Some blocks are plazas instead: open grass with a fountain
// pond, and every colony starts in a plaza of its own; the plaza at the city's centre is the
// cathedral square, with an orchard of all three fruits round its fountain. Outside the city the land
// is laid out in farm rows, with building plots dotted through them.
//
// Streets are buildable and buildings block walking, so every building a player puts up closes a
// street: the players build the city's fortifications and chokepoints themselves, and a tower on a
// street fires over the block into the next street. The fields outside are where the food is; the
// streets are where the fight is.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Stone blocks are permanent
// and unbuildable, so the city's shape is fixed and only the streets can change hands; crops regrow
// near water, and the only water inside the wall is the fountains, so a colony that wants to eat
// must farm the plazas or go out to the fields. A block stops walking but not shooting, so the
// blocks are also cover for towers.
//
// FEEDBACK 2026-09-13 (first play): "the biggest problem is the unfair accessibility for accessing
// the outlying farm lands. delete the stone ring that surrounds the entire main base so that there
// is just free access to farm lands no matter where you are located. and otherwise make sure that we
// stamp tons of those 10x4 little 'farm hubs' all over these farm lands, sporadically, maybe like
// 2.5x the number of players. and remove all the stone from the outer farm lands and reduce the
// amount of wood by like 2/3. The little 'pools' that occasionally replace the 'old city' grid in
// the center need to be like 2x bigger, and more of them need to contain fruits - not just the one
// at the center. the pools should really be big enough that they push up close against their
// surrounding grid cells' stone. same with the pools associated with each user's home base." So:
// no wall and no gates; farm plots (layFarm's 10x4 clearings, stampFarmPlot) spread over the fields
// at three per colony (the control is whole plots per colony; 2.5 was asked for); no outcrops in the
// fields and a third of the wood; every plaza's pool sized to its block, a tile short of the streets
// round it; a fruit grove beside every plaza's pool, the cathedral keeping the full orchard.
//
// FEEDBACK 2026-09-13 (second play): "close to good, but it might play nicer if the outer ring had
// little 'tendril' sand roads extending inwards towards the cities and settlements." A first reading
// ran a road from the ring along the streets to every plaza; the user meant something else (third
// play): "small roads but way more frequent, extending from the outer perimeter of the circle,
// inwards like 10 or 12 squares, maybe with a bit of meandering or waviness." So: from the fields'
// sand cap, every kTendrilSpacing tiles round it, a wandering line of sand ten to twelve tiles long
// runs straight in towards the city's middle (`tendrils`, on). Where one meets an outer block it
// carves a notch of sand into it (stone stands only on pure grass), so the city frays into the fields.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say: wheat and wood beside the fountain.
// No quarry: the blocks are stone, the nearest of them a few steps from every door.
constexpr int kHomeWheat = 14, kHomeWood = 12;
// Outside the city the farm rows keep this margin of grass from the outermost blocks and run along
// the map's axis; a farm plot's middle keeps this far inside the fields, so its ring of sand and the
// margin round it (10 wide, 4 high, a ring of 2 and a vertex more) never meet the city.
constexpr int kFarmMargin = 4, kFarmRim = 2, kFarmBridges = 16, kPlotMargin = 9;
// Tendrils (third play: "dozens of them, all the way around the perimeter, roughly evenly spaced ...
// a default width of 2 cells of sand"): one every this many tiles round the fields' sand cap, this
// long (plus up to kTendrilExtra), wandering sideways by up to this many tiles, stroked
// kTendrilHalfWidth either side of its line: three corners across, which is two whole tiles of sand.
// Eight tiles apart on a ring of about 600 tiles is some seventy tendrils on a 256 map.
constexpr int kTendrilSpacing = 8, kTendrilLength = 10, kTendrilExtra = 3;
constexpr double kTendrilWander = 2.0, kTendrilHalfWidth = 1.5;
// A fountain fills its plaza to this far short of the streets round it (FEEDBACK 2026-09-13: pools
// "big enough that they push up close against their surrounding grid cells' stone"): the block's
// half width less the street's, less a tile for the beach. 4.0 at the default block and street, an
// area two and a half times the old 2.5.
double fountainRadiusFor(int blockSize, int streetWidth)
{
	return std::max(2.5, blockSize / 2.0 - streetWidth / 2.0 - 1.0);
}

struct Layout
{
	Torus t{1, 1};
	Tessellation g;
	double cx = 0, cy = 0, cityRadius = 0, homeRadius = 0;
	std::vector<int> cell;
	std::vector<int> homeCell, annexCell,
		plazaCell;            // each colony's fountain block and swarm block, then the other plazas
	std::vector<double> axes; // each home's facing: from the fountain to the swarm
	int cathedral = -1;
	std::vector<int> homeOf;
	std::vector<unsigned char> city, block, street, water, road, farmRegion;
	Farm farm; // the fields' rows, sand and plots
	double fountainRadius = 0;
	std::vector<ShapePoint> homes, kits;
	std::string failure;
};

ShapePoint tilePoint(SubtilePoint p)
{
	return {p.x / 16.0, p.y / 16.0};
}

Layout design(const GenerationRequest &request, GenerationContext &context)
{
	const OldTownOptions o(request);
	Layout L;
	L.t = {1 << request.wDec, 1 << request.hDec};
	const Torus &t = L.t;
	const int n = t.size(), teams = std::max(1, request.nbTeams);
	L.cx = t.w / 2.0;
	L.cy = t.h / 2.0;
	// The city is a disc of `citySize` percent of the half side; the wall stands at its edge.
	L.cityRadius = o.citySize / 100.0 * std::min(t.w, t.h) / 2.0;

	// The blocks: a square tiling warped into irregular polygons. Every edge is a street, and the
	// warp keeps streets that share no corner a street's width and a house apart.
	L.g = squareTessellation(t.w, t.h, o.blockSize);
	const std::vector<unsigned char> walls(L.g.edges.size(), 1);
	warpCorners(L.g, warpLimit(L.g) * o.warp / 100, walls, o.streetWidth + 6, o.blockSize / 4,
				context, "town-warp");
	L.cell = labelTiles(L.g);
	if (L.cell.empty())
	{
		L.failure = "The blocks could not be labelled.";
		return L;
	}
	// The city's cells: those whose centre lies inside the wall's ring road.
	std::vector<unsigned char> inCity(L.g.cellCount(), 0);
	int cityCells = 0;
	for (int cell = 0; cell < L.g.cellCount(); ++cell)
	{
		const ShapePoint c = tilePoint(L.g.cells[cell].centre);
		const double d = std::hypot(t.offsetX(int(L.cx), int(c.x)), t.offsetY(int(L.cy), int(c.y)));
		inCity[cell] = d + o.blockSize / 2.0 < L.cityRadius;
		cityCells += inCity[cell];
	}
	// Plazas: the colonies' plazas as far apart among the city's cells as the city allows
	// (farthest-point spreading by centre distance, the cathedral square taken first so no colony
	// starts on it), then `plazas` per colony farthest from those.
	L.cathedral = L.cell[t.at(int(L.cx), int(L.cy))];
	if (cityCells < teams + 1)
	{
		L.failure =
			"The city has too few blocks for this many colonies; use a bigger city, smaller "
			"blocks or fewer colonies.";
		return L;
	}
	{
		// Farthest-point spreading over the city's cells, the cathedral square taken first so no
		// colony starts on it. A home plaza is two blocks, not one: a single block of 14 less its
		// streets is 10 tiles across, and with a fountain in the middle no 4x4 swarm fits beside it
		// (measured: every settlement failed); so each home takes a neighbouring block as well, the
		// fountain in the first and the swarm in the second. Extra plazas are given up first when the
		// city is small.
		std::vector<long long> nearest(L.g.cellCount(), 0);
		std::vector<unsigned char> taken(L.g.cellCount(), 0);
		taken[L.cathedral] = 1;
		for (int cell = 0; cell < L.g.cellCount(); ++cell)
			nearest[cell] = L.g.distance2(cell, L.cathedral);
		const int extra = std::min(o.plazas * teams, std::max(0, (cityCells - 1 - 2 * teams) / 2));
		for (int k = 0; k < teams + extra; ++k)
		{
			int best = -1;
			for (int cell = 0; cell < L.g.cellCount(); ++cell)
				if (inCity[cell] && !taken[cell] && (best < 0 || nearest[cell] > nearest[best]))
					best = cell;
			if (best < 0)
				break;
			taken[best] = 1;
			if (k < teams)
			{
				// The home's second block: its city neighbour nearest the cathedral, so the swarm
				// stands on the side towards the middle of town.
				int annex = -1;
				for (int edge : L.g.cells[best].edges)
				{
					const int next = L.g.other(edge, best);
					if (inCity[next] && !taken[next] &&
						(annex < 0 ||
						 L.g.distance2(next, L.cathedral) < L.g.distance2(annex, L.cathedral)))
						annex = next;
				}
				if (annex < 0)
					break;
				taken[annex] = 1;
				L.homeCell.push_back(best);
				L.annexCell.push_back(annex);
				for (int cell = 0; cell < L.g.cellCount(); ++cell)
					nearest[cell] = std::min(nearest[cell], L.g.distance2(cell, annex));
			}
			else
				L.plazaCell.push_back(best);
			for (int cell = 0; cell < L.g.cellCount(); ++cell)
				nearest[cell] = std::min(nearest[cell], L.g.distance2(cell, best));
		}
		if (int(L.homeCell.size()) != teams)
		{
			L.failure = "The city has too few blocks for this many colonies; use a bigger city, "
						"smaller blocks or fewer colonies.";
			return L;
		}
	}

	// Streets: the band along every cell border inside the city; blocks: the rest of the city's
	// cells, except the plazas. No wall (FEEDBACK 2026-09-13): the fields are open on every side.
	L.city.assign(n, 0);
	std::vector<unsigned char> border(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		const double d = std::hypot(t.offsetX(int(L.cx), x), t.offsetY(int(L.cy), y));
		L.city[i] = d < L.cityRadius;
		for (const auto &s : kCardinalSteps)
			if (L.cell[t.at(x + s[0], y + s[1])] != L.cell[i])
				border[i] = 1;
	}
	L.street = dilateRound(t, border, o.streetWidth / 2.0);
	std::vector<unsigned char> plaza(L.g.cellCount(), 0);
	plaza[L.cathedral] = 1;
	for (int cell : L.homeCell)
		plaza[cell] = 1;
	for (int cell : L.annexCell)
		plaza[cell] = 1;
	for (int cell : L.plazaCell)
		plaza[cell] = 1;
	L.block.assign(n, 0);
	for (int i = 0; i < n; ++i)
		L.block[i] = L.city[i] && inCity[L.cell[i]] && !L.street[i] && !plaza[L.cell[i]];
	// Fountains: a pond at the middle of every plaza; the colonies' kits round theirs.
	L.water.assign(n, 0);
	L.homeOf.assign(n, -1);
	L.homeRadius = std::max(6.0, o.blockSize / 2.0 - o.streetWidth / 2.0 - 1);
	L.fountainRadius = fountainRadiusFor(o.blockSize, o.streetWidth);
	const RadialShape fountain(L.fountainRadius, 0.25, context, "town-fountain");
	for (int k = 0; k < teams; ++k)
	{
		const ShapePoint fountainAt = tilePoint(L.g.cells[L.homeCell[k]].centre);
		const ShapePoint swarmAt = tilePoint(L.g.centreAcross(
			[&]
			{
				for (int edge : L.g.cells[L.homeCell[k]].edges)
					if (L.g.other(edge, L.homeCell[k]) == L.annexCell[k])
						return edge;
				return L.g.cells[L.homeCell[k]].edges[0];
			}(),
			L.homeCell[k]));
		L.homes.push_back(swarmAt);
		L.kits.push_back(fountainAt);
		L.axes.push_back(std::atan2(swarmAt.y - fountainAt.y, swarmAt.x - fountainAt.x) + kPi);
		fillShape(L.water, t, fountainAt.x, fountainAt.y, fountain, 0.0);
		for (int i = 0; i < n; ++i)
			if (L.cell[i] == L.homeCell[k] || L.cell[i] == L.annexCell[k])
				L.homeOf[i] = k;
	}
	for (int cell : L.plazaCell)
	{
		const ShapePoint c = tilePoint(L.g.cells[cell].centre);
		fillShape(L.water, t, c.x, c.y, fountain, 0.0);
	}
	{
		const ShapePoint c = tilePoint(L.g.cells[L.cathedral].centre);
		fillShape(L.water, t, c.x, c.y, fountain, 0.0);
	}
	// Outside the city: farm rows over everything beyond the outermost blocks' margin, along the map's
	// axis with the row through the centre a crop row, bridged every kFarmBridges tiles; then farm
	// plots (FEEDBACK 2026-09-13) spread through the fields, each the farthest a plot can stand from
	// the city and from every plot before it, so they are evenly dotted about.
	L.farmRegion.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const double d = std::hypot(t.offsetX(int(L.cx), i % t.w), t.offsetY(int(L.cy), i / t.w));
		L.farmRegion[i] = d > L.cityRadius + kFarmMargin;
	}
	TerrainSketch rows(n, GRASS);
	L.farm = layFarm(rows, t, L.farmRegion, 0.0, {L.cx, L.cy}, kFarmRim, bestFarmRows(0.0), nullptr,
					 kFarmBridges, true);
	const FarmPlot plot;
	const std::vector<unsigned char> roomy = erode(t, L.farmRegion, kPlotMargin);
	std::vector<unsigned char> keepClear(n, 0);
	for (int i = 0; i < n; ++i)
		keepClear[i] = !L.farmRegion[i];
	for (int p = 0; p < o.farmPlots * teams; ++p)
	{
		const std::vector<std::int64_t> far = distanceSquaredTo(t, keepClear);
		int site = -1;
		for (int i = 0; i < n; ++i)
			if (roomy[i] && (site < 0 || far[i] > far[site]))
				site = i;
		if (site < 0)
			break;
		const int x0 = site % t.w - plot.width / 2, y0 = site / t.w - plot.height / 2;
		stampFarmPlot(rows, t, L.farm, x0, y0, plot);
		for (int dy = -kPlotMargin; dy <= kPlotMargin; ++dy)
			for (int dx = -kPlotMargin; dx <= kPlotMargin; ++dx)
				keepClear[t.at(site % t.w + dx, site / t.w + dy)] = 1;
	}
	for (int i = 0; i < n; ++i)
		if (L.farm.water[i])
			L.water[i] = 1;
	// Tendrils (FEEDBACK 2026-09-13, third play): short wavy sand roads from the fields' cap ring in
	// towards the city, one every kTendrilSpacing tiles round the ring, kTendrilLength to
	// kTendrilLength + kTendrilExtra tiles long, each a wanderingPath stroked two tiles of sand wide.
	// They cross the margin and run into the outer streets and blocks; the pools are left alone.
	L.road.assign(n, 0);
	if (o.tendrils)
	{
		const double ring = L.cityRadius + kFarmMargin + 1;
		const int count = std::max(1, int(std::lround(2 * kPi * ring / kTendrilSpacing)));
		const double phase = context.bounded("town-tendrils", 3600) / 3600.0 * 2 * kPi;
		std::mt19937 &wobble = context.stream("town-tendrils");
		for (int k = 0; k < count; ++k)
		{
			const double a = phase + 2 * kPi * k / count;
			const double length =
				kTendrilLength + context.bounded("town-tendrils", kTendrilExtra + 1);
			const ShapePoint from = polarPoint(L.cx, L.cy, ring, a);
			const ShapePoint to = polarPoint(L.cx, L.cy, ring - length, a);
			strokePath(L.road, t,
					   wanderingPath(t, from, to, kTendrilHalfWidth, kTendrilWander, 0.0, wobble));
		}
		for (int i = 0; i < n; ++i)
			if (L.water[i])
				L.road[i] = 0;
	}
	return L;
}

bool generate(Game &game, GenerationContext &context)
{
	context.stage = "old town layout";
	const OldTownOptions o(context.request);
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

	context.stage = "old town terrain";
	TerrainSketch terrain(n, GRASS);
	for (int i = 0; i < n; ++i)
		terrain[i] = L.water[i] ? WATER : (L.farm.sand[i] || L.road[i]) ? SAND : GRASS;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	// The blocks are stone, wherever the beaches left pure grass.
	for (int i = 0; i < n; ++i)
		if (L.block[i] && map.isResourceAllowed(i % t.w, i / t.w, STONE))
			map.setResource(i % t.w, i / t.w, STONE, 1);

	context.stage = "old town colonies";
	const auto homeMask = [&](int team)
	{
		std::vector<unsigned char> ground(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			ground[i] = L.homeOf[i] == team && map.isGrass(i % t.w, i / t.w);
		return ground;
	};
	const auto anchor = [&](int team)
	{
		return MapGeneratorPoint(int(std::lround(L.homes[team].x)) - 2,
								 int(std::lround(L.homes[team].y)) - 2);
	};
	if (!settleColonies(game, context, "town-starts", homeMask, anchor))
		return false;

	context.stage = "old town resources";
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	for (int k = 0; k < teams; ++k)
		plantHomeKit(
			map, t, context, L.kits[k], L.axes[k], L.homeRadius, kHomeWheat, kHomeWood, [&](int i)
			{ return L.homeOf[i] == k && !reserved[i] && clearGround(map, i % t.w, i / t.w); });
	// The cathedral square's orchard round its fountain, and a grove of one fruit in turn beside every
	// other plaza's pool (FEEDBACK 2026-09-13: "more of them need to contain fruits"). A pool fills its
	// plaza to a tile short of the streets, so the fruit stands on the ground round it.
	const auto roundPlaza = [&](int cell)
	{
		const ShapePoint c = tilePoint(L.g.cells[cell].centre);
		return [&, c](int i)
		{
			return !L.block[i] && L.homeOf[i] < 0 &&
				   std::hypot(t.offsetX(int(c.x), i % t.w), t.offsetY(int(c.y), i / t.w)) <=
					   o.blockSize / 2.0 + 2 &&
				   clearGround(map, i % t.w, i / t.w);
		};
	};
	if (scaledCount(1, o.fruit) > 0)
	{
		const ShapePoint c = tilePoint(L.g.cells[L.cathedral].centre);
		const double spin = context.bounded("town-fruit", 3600) / 3600.0 * 2 * kPi;
		plantOrchard(map, t, context, c.x, c.y, L.fountainRadius + 2.5, {spin}, 4.5, 5, 1,
					 roundPlaza(L.cathedral));
		int fruit = 0;
		for (int cell : L.plazaCell)
		{
			const ShapePoint c = tilePoint(L.g.cells[cell].centre);
			const double at = context.bounded("town-fruit", 3600) / 3600.0 * 2 * kPi;
			plantRound(map, t, context, c.x, c.y, L.fountainRadius + 2.5, {at},
					   CHERRY + fruit++ % 3, 1, 5, roundPlaza(cell));
		}
	}
	// The fields outside: half the fertile row ground under wheat and a twentieth under wood (a third
	// of the sixth it was, FEEDBACK 2026-09-13), no outcrops, in patches, the farm plots kept clear;
	// inside the city only the plazas are fertile (their fountains), and get a light share so they
	// stay open to build on.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 8, context.stream("town-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 4, context.stream("town-split"));
	const auto fields = [&](int i)
	{
		return L.farmRegion[i] && !L.farm.sand[i] && !L.farm.plot[i] &&
			   clearGround(map, i % t.w, i / t.w);
	};
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += fields(i) && fertility.at(i % t.w, i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, fields, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(fertile * 50 / 100, o.wheat)),
								 int(scaledCount(fertile * 5 / 100, o.wood)), 0,
								 int(scaledCount(area / 3000, o.fruit))};
		},
		"town-stone", "town-fruit");
	const auto plazas = [&](int i)
	{
		return L.city[i] && !L.block[i] && !L.street[i] && L.homeOf[i] < 0 &&
			   clearGround(map, i % t.w, i / t.w);
	};
	int plazaFertile = 0;
	for (int i = 0; i < n; ++i)
		plazaFertile += plazas(i) && fertility.at(i % t.w, i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, plazas, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int)
		{
			return GroundAmounts{int(scaledCount(plazaFertile * 15 / 100, o.wheat)),
								 int(scaledCount(plazaFertile * 10 / 100, o.wood)), 0, 0};
		},
		"town-stone", "town-fruit");
	seedAlgae(map, context, t, "town-algae", o.algae, AlgaeBand::anyWater(50));
	const std::vector<unsigned char> &stone = L.block;
	secureStartingCrops(game, context, t, 24, 32, 0, &stone);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&stone);
	clearFarmPlots(map, t, {L.farm});
	// The streets join every plaza and open onto the fields on every side; only crops could close a
	// street, so only crops are cleared.
	context.stage = "old town routes";
	openColonyRoutes(map, context, t, StepCosts{1, 3, -1, -1, -1});
	return true;
}

std::string validateWorld(const Game &game, const GenerationContext &context)
{
	GenerationContext replay(context.request);
	const Layout L = design(context.request, replay);
	const Map &map = game.map;
	if (const std::string mismatch = designMismatch(L, map, "old town"); !mismatch.empty())
		return mismatch;
	const Torus &t = L.t;
	for (int k = 0; k < context.request.nbTeams; ++k)
	{
		bool pond = false;
		for (int dy = -2; dy <= 2 && !pond; ++dy)
			for (int dx = -2; dx <= 2 && !pond; ++dx)
				pond = map.isWater(t.x(int(L.kits[k].x) + dx), t.y(int(L.kits[k].y) + dy));
		if (!pond)
			return "Colony " + std::to_string(k) + "'s plaza has lost its fountain.";
	}
	return walkFromFirstColony(map, context.request.nbTeams, "the city", "through the streets")
		.error;
}
} // namespace

OldTownOptions::OldTownOptions(const GenerationRequest &r)
	: citySize(r.option("city-size")), blockSize(r.option("block-size")),
	  streetWidth(r.option("street-width")), warp(r.option("warp")), plazas(r.option("plazas")),
	  farmPlots(r.option("farm-plots")), tendrils(r.option("tendrils") != 0),
	  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition oldTownDefinition()
{
	return {"old-town",
			31,
			"Old town",
			4,
			false,
			// A city of 70% of the half side leaves a belt of fields round it; blocks of 14 with
			// streets of 4 give a 256 map about a hundred blocks and streets a column of units wide
			// that one building closes; three farm plots per colony through the fields (FEEDBACK
			// 2026-09-13 asked for about two and a half).
			{{"city-size", "City size", 40, 90, 5, 70, ControlGroup::Layout},
			 {"block-size", "Block size", 10, 20, 1, 14, ControlGroup::Layout},
			 {"street-width", "Street width", 3, 7, 1, 4, ControlGroup::Terrain},
			 {"warp", "Warp", 0, 100, 10, 50, ControlGroup::Terrain},
			 {"plazas", "Plazas", 0, 4, 1, 2, ControlGroup::Layout},
			 {"farm-plots", "Farm plots", 0, 6, 1, 3, ControlGroup::Layout},
			 GeneratorControl::toggle("tendrils", "Tendril roads", true, ControlGroup::Terrain),
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
