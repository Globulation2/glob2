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

// Old town: a walled city of stone blocks and narrow grass streets, with farmland outside the wall.
// The city is a warped grid of irregular blocks (Tessellation.h); every block is solid stone, every
// street between blocks is grass, so the streets form a dense grid with a flanking route round every
// corner, unlike a maze's single ways. Some blocks are plazas instead: open grass with a fountain
// pond, and every colony starts in a plaza of its own; the plaza at the city's centre is the
// cathedral square, with an orchard of all three fruits round its fountain. A wall of stone with a
// few gates rings the city, and outside it the land is laid out in farm rows.
//
// Streets are buildable and buildings block walking, so every building a player puts up closes a
// street: the players build the city's fortifications and chokepoints themselves, and a tower on a
// street fires over the block into the next street. The fields outside are where the food is; the
// streets are where the fight is.
//
// WHY IT PLAYS WELL (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md). Stone blocks are permanent
// and unbuildable, so the city's shape is fixed and only the streets can change hands; crops regrow
// near water, and the only water inside the wall is the fountains, so a colony that wants to eat
// must farm the plazas or go out through a gate. A wall stops walking but not shooting, so the
// blocks are also cover for towers.
namespace
{

// Every home's starting kit, unscaled whatever the amounts say: wheat and wood beside the fountain.
// No quarry: the blocks are stone, the nearest of them a few steps from every door.
constexpr int kHomeWheat = 14, kHomeWood = 12;
// The wall is this thick (stone, sealed against diagonal steps) and stands this far outside the
// outermost blocks, leaving a ring road inside it; gates are this wide.
constexpr int kWallThickness = 2, kRingRoad = 4, kGateHalfWidth = 3;
// Outside the wall the farm rows keep this margin of grass from it and run along the map's axis.
constexpr int kFarmMargin = 4, kFarmRim = 2, kFarmBridges = 16;
// A fountain's radius: a small pond with a beach, the width of a street to walk round.
constexpr double kFountainRadius = 2.5;

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
	std::vector<unsigned char> city, block, street, wall, water, farmSand, farmRegion;
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
		inCity[cell] = d + o.blockSize / 2.0 < L.cityRadius - kWallThickness - kRingRoad;
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
	// cells, except the plazas; the wall: a ring with gates; the ring road between them.
	L.city.assign(n, 0);
	L.wall.assign(n, 0);
	std::vector<unsigned char> border(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		const double d = std::hypot(t.offsetX(int(L.cx), x), t.offsetY(int(L.cy), y));
		L.city[i] = d < L.cityRadius - kWallThickness;
		for (const auto &s : kCardinalSteps)
			if (L.cell[t.at(x + s[0], y + s[1])] != L.cell[i])
				border[i] = 1;
	}
	L.street = dilateRound(t, border, o.streetWidth / 2.0);
	const double gateSpin = context.bounded("town-gates", 3600) / 3600.0 * 2 * kPi;
	std::vector<double> gates;
	for (int gate = 0; gate < o.gates; ++gate)
		gates.push_back(gateSpin + 2 * kPi * gate / o.gates);
	ringWithGates(t, L.cx, L.cy, L.cityRadius - kWallThickness / 2.0, kWallThickness / 2.0, gates,
				  kGateHalfWidth, [&](int i, int gate) { L.wall[i] = gate < 0; });
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
		L.block[i] =
			L.city[i] && inCity[L.cell[i]] && !L.street[i] && !plaza[L.cell[i]] && !L.wall[i];
	// Fountains: a pond at the middle of every plaza; the colonies' kits round theirs.
	L.water.assign(n, 0);
	L.homeOf.assign(n, -1);
	L.homeRadius = std::max(6.0, o.blockSize / 2.0 - o.streetWidth / 2.0 - 1);
	const RadialShape fountain(kFountainRadius, 0.3, context, "town-fountain");
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
	// Outside the wall: farm rows over everything beyond the wall's margin, along the map's axis
	// with the row through the centre a crop row, bridged every kFarmBridges tiles.
	L.farmRegion.assign(n, 0);
	for (int i = 0; i < n; ++i)
	{
		const double d = std::hypot(t.offsetX(int(L.cx), i % t.w), t.offsetY(int(L.cy), i / t.w));
		L.farmRegion[i] = d > L.cityRadius + kFarmMargin;
	}
	TerrainSketch rows(n, GRASS);
	const Farm farm = layFarm(rows, t, L.farmRegion, 0.0, {L.cx, L.cy}, kFarmRim, bestFarmRows(0.0),
							  nullptr, kFarmBridges, true);
	L.farmSand = farm.sand;
	for (int i = 0; i < n; ++i)
		if (farm.water[i])
			L.water[i] = 1;
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
		terrain[i] = L.water[i] ? WATER : L.farmSand[i] ? SAND : GRASS;
	layBeaches(terrain, t);
	writeUndermap(map, terrain);
	// The blocks and the wall are stone, wherever the beaches left pure grass.
	for (int i = 0; i < n; ++i)
		if ((L.block[i] || L.wall[i]) && map.isResourceAllowed(i % t.w, i / t.w, STONE))
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
	// The cathedral square's orchard, round its fountain.
	if (scaledCount(1, o.fruit) > 0)
	{
		const ShapePoint c = tilePoint(L.g.cells[L.cathedral].centre);
		const double spin = context.bounded("town-fruit", 3600) / 3600.0 * 2 * kPi;
		plantOrchard(map, t, context, c.x, c.y, kFountainRadius + 3.5, {spin}, 4.5, 4, 1,
					 [&](int i)
					 {
						 return L.cell[i] == L.cathedral && !L.street[i] &&
								clearGround(map, i % t.w, i / t.w);
					 });
	}
	// The fields outside: half the fertile row ground under wheat and a sixth under wood, in patches;
	// inside the city only the plazas are fertile (their fountains), and get a light share so they
	// stay open to build on.
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 8, context.stream("town-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 4, context.stream("town-split"));
	const auto fields = [&](int i)
	{ return L.farmRegion[i] && !L.farmSand[i] && clearGround(map, i % t.w, i / t.w); };
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += fields(i) && fertility.at(i % t.w, i / t.w) > 0;
	furnishGround(
		map, t, context, fertility, fields, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int area)
		{
			return GroundAmounts{int(scaledCount(fertile * 50 / 100, o.wheat)),
								 int(scaledCount(fertile * 16 / 100, o.wood)),
								 int(scaledCount(area / 2000, o.stone)),
								 int(scaledCount(area / 3000, o.fruit))};
		},
		"town-stone", "town-fruit");
	const auto plazas = [&](int i)
	{
		return L.city[i] && !L.block[i] && !L.wall[i] && !L.street[i] && L.homeOf[i] < 0 &&
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
	std::vector<unsigned char> stone(n, 0);
	for (int i = 0; i < n; ++i)
		stone[i] = L.block[i] || L.wall[i];
	secureStartingCrops(game, context, t, 24, 32, 0, &stone);
	reopenCrampedStarts(game, context, {o.wheat, o.wood, o.stone, o.algae, o.fruit}, 24, 32, 0,
						&stone);
	// The streets join every plaza and the gates join the city to the fields; only crops could close
	// a street, so only crops are cleared.
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
	for (int i = 0; i < t.size(); ++i)
		if (L.wall[i] && map.isGrass(i % t.w, i / t.w) &&
			map.getResource(i % t.w, i / t.w).type != STONE)
			return "The city wall has a gap at (" + std::to_string(i % t.w) + ", " +
				   std::to_string(i / t.w) + ").";
	return walkFromFirstColony(map, context.request.nbTeams, "the city", "through the streets")
		.error;
}
} // namespace

OldTownOptions::OldTownOptions(const GenerationRequest &r)
	: citySize(r.option("city-size")), blockSize(r.option("block-size")),
	  streetWidth(r.option("street-width")), warp(r.option("warp")), plazas(r.option("plazas")),
	  gates(r.option("gates")), wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
	  stone(r.option("stone-amount")), algae(r.option("algae-amount")),
	  fruit(r.option("fruit-amount"))
{
}

GeneratorDefinition oldTownDefinition()
{
	return {"old-town",
			31,
			"Old town",
			1,
			false,
			// A city of 70% of the half side leaves a belt of fields round it; blocks of 14 with
			// streets of 4 give a 256 map about a hundred blocks and streets a column of units wide
			// that one building closes; four gates, one to a side.
			{{"city-size", "City size", 40, 90, 5, 70, ControlGroup::Layout},
			 {"block-size", "Block size", 10, 20, 1, 14, ControlGroup::Layout},
			 {"street-width", "Street width", 3, 7, 1, 4, ControlGroup::Terrain},
			 {"warp", "Warp", 0, 100, 10, 50, ControlGroup::Terrain},
			 {"plazas", "Plazas", 0, 4, 1, 2, ControlGroup::Layout},
			 {"gates", "Gates", 2, 8, 1, 4, ControlGroup::Layout},
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
