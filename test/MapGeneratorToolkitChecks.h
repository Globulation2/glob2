// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Contracts of the shared generator toolkit under src/map/generator/shared, each checked on a
// small map built by hand rather than through a generator, so a change to one module fails
// here before it shows up as a changed golden fingerprint somewhere downstream. Needs the
// globals loaded: building and resource types.
#include "BalancedStarts.h"
#include "Bases.h"
#include "Building.h"
#include "BuildingType.h"
#include "Compounds.h"
#include "Drawing.h"
#include "GenerationValidation.h"
#include "GeneratorDefinition.h"
#include "IntBuildingType.h"
#include "Lots.h"
#include "Routes.h"
#include "Team.h"
#include "Unit.h"
#include "UnitConsts.h"
#include "GraphMaze.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "StartQuality.h"
#include "Tessellation.h"
#include "Territories.h"
#include "Walls.h"
#include "Homes.h"
#include "Farmland.h"
#include "Towers.h"
#include "Wedge.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace ToolkitChecks
{
using namespace MapGeneration;

inline void grassMap(Game &game, int wDec, int hDec)
{
	game.map.setSize(wDec, hDec);
	game.map.setGame(&game);
	game.map.makeHomogenMap(GRASS);
}

inline int countResource(const Map &map, int type)
{
	int count = 0;
	for (int y = 0; y < map.getH(); ++y)
		for (int x = 0; x < map.getW(); ++x)
			count += map.getResource(x, y).type == type;
	return count;
}

// floodFrom: steps match the Chebyshev oracle on an open torus, a limit stops the flood there,
// tiles are visited in nondecreasing order, and an obstacle ring holds it.
inline void floodChecks()
{
	const Torus t(16, 8);
	std::vector<unsigned char> source(t.size(), 0), open(t.size(), 1);
	source[t.at(3, 2)] = 1;
	const Flood limited = floodFrom(t, source, open, 3);
	assert(limited.steps[t.at(3, 2)] == 0 && limited.steps[t.at(6, 2)] == 3);
	assert(limited.steps[t.at(7, 2)] == -1 && limited.steps[t.at(0, 7)] == 3);
	assert(limited.visited.front() == t.at(3, 2));
	for (size_t i = 1; i < limited.visited.size(); ++i)
		assert(limited.steps[limited.visited[i - 1]] <= limited.steps[limited.visited[i]]);
	const std::vector<int> steps = stepsFrom(t, source, open);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			assert(steps[t.at(x, y)] == t.chebyshev(3, 2, x, y));
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx)
			if (dx || dy)
				open[t.at(3 + dx, 2 + dy)] = 0;
	const std::vector<int> held = stepsFrom(t, source, open);
	assert(held[t.at(3, 2)] == 0 && held[t.at(4, 2)] == -1 && held[t.at(9, 5)] == -1);
	assert(std::count(held.begin(), held.end(), 0) == 1);
}

// Sketch: every land tile touching water becomes beach and nothing else changes; islands raise
// out at sea, clear of the coast and of each other, and the same seed raises the same islands.
inline void sketchChecks()
{
	const Torus t(64, 64);
	TerrainSketch sketch(t.size(), GRASS);
	for (int y = 10; y < 14; ++y)
		for (int x = 10; x < 14; ++x)
			sketch[t.at(x, y)] = WATER;
	const TerrainSketch before = sketch;
	layBeaches(sketch, t);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = t.at(x, y);
			if (before[i] == WATER)
			{
				assert(sketch[i] == WATER);
				continue;
			}
			bool shore = false;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					shore = shore || before[t.at(x + dx, y + dy)] == WATER;
			assert(sketch[i] == (shore ? SAND : GRASS));
		}
	assert(countTiles(sketch, WATER) == 16 && countTiles(sketch, SAND) == 20);

	TerrainSketch sea(t.size(), WATER);
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 4; ++x)
			sea[t.at(x, y)] = GRASS;
	GenerationRequest request;
	request.seed = 11;
	GenerationContext context(request), again(request);
	const IslandPlacement placement{"islands", 2, 40, 7.0};
	TerrainSketch sameSea = sea;
	const std::vector<Island> islands = raiseIslands(sea, t, context, placement);
	const std::vector<Island> same = raiseIslands(sameSea, t, again, placement);
	assert(!islands.empty() && islands.size() <= 2 && islands.size() == same.size());
	assert(sea == sameSea);
	int raised = 0;
	for (const Island &island : islands)
	{
		assert(!island.tiles.empty() && island.reach > 0);
		for (int i : island.tiles)
			assert(sea[i] == GRASS);
		assert(std::find(island.tiles.begin(), island.tiles.end(), t.at(island.x, island.y)) !=
			   island.tiles.end());
		// Clear of the corner's land: more than the moat plus the outline's reach away.
		assert(t.chebyshev(island.x, island.y, 2, 2) > island.reach + 7 - 2);
		raised += int(island.tiles.size());
	}
	for (size_t a = 0; a < islands.size(); ++a)
		for (size_t b = a + 1; b < islands.size(); ++b)
		{
			const double gap = islands[a].reach + islands[b].reach + 7;
			assert(t.dist2(islands[a].x, islands[a].y, islands[b].x, islands[b].y) >= gap * gap);
		}
	assert(countTiles(sea, GRASS) == 16 + raised);
	TerrainSketch none(t.size(), WATER);
	assert(raiseIslands(none, t, context, placement).empty()); // no coast to keep clear of
}

// Planting: patches grow to size and stay four-connected, seedNear finds the nearest eligible
// tile, algae lands on water only and in the band asked for, and the swarm clearance is exact.
inline void plantingChecks()
{
	Game game(nullptr);
	grassMap(game, 6, 6);
	Map &map = game.map;
	const Torus t(map);
	assert(clearGround(map, 5, 5));
	const auto anywhere = [](int) { return true; };
	assert(growPatch(map, t, t.at(20, 20), WHEAT, 12, anywhere) == 12);
	assert(countResource(map, WHEAT) == 12 && !clearGround(map, 20, 20));
	std::vector<unsigned char> wheat(t.size(), 0), seed(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		wheat[i] = map.getResource(i % t.w, i / t.w).type == WHEAT;
	seed[t.at(20, 20)] = 1;
	// Four-connected: a cardinal flood over the patch from its seed reaches every tile of it.
	std::vector<int> reached{t.at(20, 20)};
	std::vector<unsigned char> seen(t.size(), 0);
	seen[t.at(20, 20)] = 1;
	for (size_t head = 0; head < reached.size(); ++head)
		for (const auto &step : kCardinalSteps)
		{
			const int n = t.at(reached[head] % t.w + step[0], reached[head] / t.w + step[1]);
			if (wheat[n] && !seen[n])
			{
				seen[n] = 1;
				reached.push_back(n);
			}
		}
	assert(reached.size() == 12);
	const auto clear = [&](int i) { return clearGround(map, i % t.w, i / t.w); };
	const int nearby = seedNear(t, 20, 20, 6, clear);
	assert(nearby >= 0 && clear(nearby));
	for (int dy = -6; dy <= 6; ++dy)
		for (int dx = -6; dx <= 6; ++dx)
			if (clear(t.at(20 + dx, 20 + dy)))
				assert(dx * dx + dy * dy >= t.dist2(20, 20, nearby % t.w, nearby / t.w));
	assert(seedNear(t, 20, 20, 2, [](int) { return false; }) == -1);
	// Only eligible tiles are planted, and the count stops where eligibility does: a 2 by 4 box.
	const auto box = [&](int i)
	{ return i % t.w >= 40 && i % t.w < 42 && i / t.w >= 40 && i / t.w < 44; };
	assert(growPatch(map, t, t.at(40, 40), WOOD, 50, box) == 8);
	assert(countResource(map, WOOD) == 8);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			if (map.getResource(x, y).type == WOOD)
				assert(box(t.at(x, y)));

	GenerationRequest request;
	request.seed = 5;
	request.nbTeams = 1;
	GenerationContext context(request);
	// A pond: algae anywhere on its water, then only in its shallows.
	for (int y = 40; y < 52; ++y)
		for (int x = 8; x < 20; ++x)
			map.setUMTerrain(x, y, WATER);
	map.controlSand();
	map.rebuildTerrain();
	seedAlgae(map, context, t, "algae", 100, AlgaeBand::anyWater(1));
	const int anywhereAlgae = countResource(map, ALGA);
	assert(anywhereAlgae > 0);
	std::vector<unsigned char> wet(t.size()), dry(t.size());
	for (int i = 0; i < t.size(); ++i)
	{
		wet[i] = map.isWater(i % t.w, i / t.w);
		dry[i] = !wet[i];
		if (map.getResource(i % t.w, i / t.w).type == ALGA)
		{
			assert(wet[i]);
			map.setNoResource(i % t.w, i / t.w, 1);
		}
	}
	const std::vector<int> offshore = stepsFrom(t, dry, wet);
	// The band constrains the clumps' centres, so single-tile clumps show it exactly.
	seedAlgae(map, context, t, "algae", 100, AlgaeBand{2, 3, 1, 0});
	assert(countResource(map, ALGA) > 0);
	for (int i = 0; i < t.size(); ++i)
		if (map.getResource(i % t.w, i / t.w).type == ALGA)
			assert(offshore[i] >= 2 && offshore[i] <= 3);
	assert(scaledCount(40, 0) == 0);
	map.setNoResource(8, 40, 1);
	seedAlgae(map, context, t, "algae", 0, AlgaeBand::anyWater(1));

	// The swarm clearance: the 4x4 footprint and kSwarmClearance tiles round it.
	context.bootX[0] = 30;
	context.bootY[0] = 10;
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	assert(std::count(reserved.begin(), reserved.end(), 1) == 8 * 8);
	assert(reserved[t.at(28, 8)] && reserved[t.at(35, 15)] && !reserved[t.at(27, 8)] &&
		   !reserved[t.at(36, 15)]);
	for (int dy = -3; dy < 7; ++dy)
		for (int dx = -3; dx < 7; ++dx)
			map.setResource(30 + dx, 10 + dy, STONE, 1);
	std::vector<unsigned char> keep(t.size(), 0);
	keep[t.at(30, 10)] = 1;
	clearAroundSwarms(map, context, t, &keep);
	for (int dy = -3; dy < 7; ++dy)
		for (int dx = -3; dx < 7; ++dx)
		{
			const bool inside = dx >= -2 && dx < 6 && dy >= -2 && dy < 6;
			const bool kept = dx == 0 && dy == 0;
			assert((map.getResource(30 + dx, 10 + dy).type == STONE) == (!inside || kept));
		}
}

// Roads: the cheapest route crosses the fewest costly tiles, never a blocked one, and is empty
// when there is no way; openRoad clears exactly the deposits on it.
inline void roadChecks()
{
	// Column 15 is blocked, so the way round the wrap is shut; column 7 costs a deposit to cross and
	// column 9 is shut but for one gate at (9, 8).
	const Torus t(16, 16);
	std::vector<unsigned char> goal(t.size(), 0), blocked(t.size(), 0), costly(t.size(), 0);
	goal[t.at(12, 8)] = 1;
	for (int y = 0; y < t.h; ++y)
	{
		costly[t.at(7, y)] = 1;
		blocked[t.at(15, y)] = 1;
		if (y != 8)
			blocked[t.at(9, y)] = 1;
	}
	const std::vector<int> route = cheapestRoute(t, {t.at(2, 8)}, goal, blocked, costly);
	assert(!route.empty() && route.front() == t.at(12, 8) && route.back() == t.at(2, 8));
	int crossings = 0;
	bool gate = false;
	for (int i : route)
	{
		assert(!blocked[i]);
		crossings += costly[i];
		gate = gate || i == t.at(9, 8);
	}
	assert(crossings == 1 && gate);
	blocked[t.at(9, 8)] = 1;
	assert(cheapestRoute(t, {t.at(2, 8)}, goal, blocked, costly).empty());

	// On the map, stone columns either side of the source: any walk to the goal crosses one of
	// them, and openRoad clears exactly one tile of it.
	Game game(nullptr);
	grassMap(game, 4, 4);
	Map &map = game.map;
	for (int y = 0; y < map.getH(); ++y)
	{
		map.setResource(7, y, STONE, 1);
		map.setResource(15, y, STONE, 1);
	}
	assert(countResource(map, STONE) == 32);
	assert(openRoad(map, Torus(map), {t.at(2, 8)}, goal));
	assert(countResource(map, STONE) == 31);
	std::vector<unsigned char> wall(t.size(), 0);
	for (int y = 0; y < t.h; ++y)
		wall[t.at(9, y)] = wall[t.at(15, y)] = 1;
	assert(!openRoad(map, Torus(map), {t.at(2, 8)}, goal, &wall));
	assert(countResource(map, STONE) == 31);
}

// Settlements and the pipeline stages round them: settleColonies puts every colony down,
// walkFromFirstColony reports a cut-off colony by number, the guarantee reaches wheat and wood
// through a resource wall but never through a protected one, and openCrampedStarts gives a
// buried colony its building room back.
inline void settlementChecks()
{
	{
		Game game(nullptr);
		grassMap(game, 6, 6);
		Map &map = game.map;
		const Torus t(map);
		GenerationRequest request;
		request.seed = 3;
		request.nbTeams = 2;
		request.nbWorkers = 3;
		GenerationContext context(request);
		game.addTeam();
		game.addTeam();
		const std::vector<unsigned char> everywhere(t.size(), 1);
		assert(settleColonies(
			game, context, "starts", [&](int) { return everywhere; },
			[](int team) { return MapGeneratorPoint(team ? 44 : 12, 20); }));
		assert(context.bootX[0] == 12 && context.bootY[0] == 20 && context.bootX[1] == 44);
		ColonyWalk walk = walkFromFirstColony(map, 2, "the map", "");
		assert(walk.error.empty() && walk.workers[1].size() == 3 && walk.steps[t.at(12, 19)] >= 0);
		for (int i : walk.workers[1])
			assert(walk.steps[i] > 0);
		assert(!reopenCrampedStarts(game, context, {}));
		// Scoring the finished colonies.
		growPatch(map, t, t.at(12, 26), WHEAT, 10, [](int) { return true; });
		growPatch(map, t, t.at(44, 26), WHEAT, 10, [](int) { return true; });
		growPatch(map, t, t.at(8, 14), WOOD, 10, [](int) { return true; });
		growPatch(map, t, t.at(40, 14), WOOD, 10, [](int) { return true; });
		const StartQualityReport report = scoreStarts(game, 2);
		assert(report.measured && report.colonies.size() == 2);
		for (const ColonyQuality &colony : report.colonies)
			assert(colony.wheatDistance > 0 && colony.woodDistance > 0 && colony.total > 0 &&
				   colony.total <= 1 && colony.buildSites > 0);
		assert(report.worst <= report.best && report.score <= report.worst + 1e-12 &&
			   report.fairness > 0 && report.fairness <= 1);
		// Two water walls, one across the wrap, cut colony 1 off from colony 0.
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
				if ((x >= 28 && x < 32) || x < 4)
					map.setUMTerrain(x, y, WATER);
		map.rebuildTerrain();
		walk = walkFromFirstColony(map, 2, "the map", "over the wall");
		assert(walk.error == "Colony 1 cannot walk to colony 0 over the wall.");
		assert(walkFromFirstColony(map, 0, "the map", "").error ==
			   "Colony 0 has no workers to walk the map.");
	}
	{
		// A colony on bare grass: the guarantee gives it wheat and wood within range.
		Game game(nullptr);
		grassMap(game, 6, 6);
		Map &map = game.map;
		const Torus t(map);
		GenerationRequest request;
		request.seed = 9;
		request.nbTeams = 1;
		GenerationContext context(request);
		context.bootX[0] = 20;
		context.bootY[0] = 20;
		guaranteeStartingResources(game, context, 24, 32);
		const auto nearest = [&](int type)
		{
			const std::vector<int> steps =
				stepsFrom(t, tileMask(t, {t.at(20, 20)}), groundUnitTiles(map));
			int best = -1;
			for (int i = 0; i < t.size(); ++i)
				if (map.getResource(i % t.w, i / t.w).type == type)
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							const int d = steps[t.at(i % t.w + dx, i / t.w + dy)];
							if (d >= 0 && (best < 0 || d + 1 < best))
								best = d + 1;
						}
			return best;
		};
		assert(nearest(WHEAT) > 0 && nearest(WHEAT) <= 24 && nearest(WOOD) > 0 &&
			   nearest(WOOD) <= 32);
		// Walled into a pocket by stone with crops outside: the wall is cleared, not added to.
		Game walled(nullptr);
		grassMap(walled, 6, 6);
		Map &pocket = walled.map;
		std::vector<unsigned char> ring(t.size(), 0);
		for (int dy = -3; dy <= 3; ++dy)
			for (int dx = -3; dx <= 3; ++dx)
				if (std::max(std::abs(dx), std::abs(dy)) == 3)
				{
					pocket.setResource(20 + dx, 20 + dy, STONE, 1);
					ring[t.at(20 + dx, 20 + dy)] = 1;
				}
		pocket.setResource(28, 20, WHEAT, 1);
		pocket.setResource(12, 20, WOOD, 1);
		guaranteeStartingResources(walled, context, 24, 32);
		assert(countResource(pocket, STONE) < 24 && countResource(pocket, WHEAT) == 1 &&
			   countResource(pocket, WOOD) == 1);
		// The same pocket with the ring protected: the stone stays and crops are placed inside.
		Game keep(nullptr);
		grassMap(keep, 6, 6);
		Map &designed = keep.map;
		for (int i = 0; i < t.size(); ++i)
			if (ring[i])
				designed.setResource(i % t.w, i / t.w, STONE, 1);
		designed.setResource(28, 20, WHEAT, 1);
		designed.setResource(12, 20, WOOD, 1);
		guaranteeStartingResources(keep, context, 24, 32, 0, &ring);
		assert(countResource(designed, STONE) == 24 && countResource(designed, WHEAT) > 1 &&
			   countResource(designed, WOOD) > 1);
	}
	{
		// A colony buried in wood: openCrampedStarts clears rings until it can build again.
		Game game(nullptr);
		grassMap(game, 6, 6);
		Map &map = game.map;
		const Torus t(map);
		GenerationRequest request;
		request.seed = 21;
		request.nbTeams = 1;
		request.nbWorkers = 2;
		GenerationContext context(request);
		game.addTeam();
		const std::vector<unsigned char> everywhere(t.size(), 1);
		assert(placeSettlement(game, context, 0, everywhere, {30, 30}, "starts"));
		for (int dy = -12; dy <= 12; ++dy)
			for (int dx = -12; dx <= 12; ++dx)
				if (clearGround(map, t.x(30 + dx), t.y(30 + dy)))
					map.setResource(t.x(30 + dx), t.y(30 + dy), WOOD, 1);
		const auto sites = [&]
		{
			const Flood flood =
				floodFrom(t, tileMask(t, unitTilesByTeam(map, 1)[0]), groundUnitTiles(map), 24);
			int count = 0;
			for (int i : flood.visited)
				count += map.isFreeForBuilding(i % t.w, i / t.w, 4, 4);
			return count;
		};
		assert(sites() < 16);
		const int buried = countResource(map, WOOD);
		openCrampedStarts(game, context);
		assert(sites() >= 16 && countResource(map, WOOD) < buried && countResource(map, WOOD) > 0);
		assert(reopenCrampedStarts(game, context, {100, 150, 100, 100, 100}));
	}
}

// chooseBalancedStarts picks legal, mutually distant sites that can reach both crops, the same
// ones every time for the same map.
inline void balancedStartChecks()
{
	Game game(nullptr);
	grassMap(game, 6, 6);
	Map &map = game.map;
	const Torus t(map);
	const auto anywhere = [](int) { return true; };
	growPatch(map, t, t.at(16, 16), WHEAT, 12, anywhere);
	growPatch(map, t, t.at(22, 16), WOOD, 12, anywhere);
	growPatch(map, t, t.at(48, 48), WHEAT, 12, anywhere);
	growPatch(map, t, t.at(42, 48), WOOD, 12, anywhere);
	GenerationRequest request;
	request.seed = 4;
	request.nbTeams = 2;
	request.nbWorkers = 4;
	GenerationContext context(request), again(request);
	assert(chooseBalancedStarts(game, context, 20 * 20));
	assert(chooseBalancedStarts(game, again, 20 * 20));
	for (int team = 0; team < 2; ++team)
	{
		assert(context.bootX[team] == again.bootX[team] &&
			   context.bootY[team] == again.bootY[team]);
		assert(map.isFreeForBuilding(context.bootX[team], context.bootY[team], 4, 4));
	}
	assert(t.dist2(context.bootX[0], context.bootY[0], context.bootX[1], context.bootY[1]) >=
		   20 * 20);
	// No wood anywhere: no balanced set exists, and the boot tiles are left alone.
	Game bare(nullptr);
	grassMap(bare, 6, 6);
	growPatch(bare.map, t, t.at(16, 16), WHEAT, 12, anywhere);
	GenerationContext untouched(request);
	untouched.bootX[0] = untouched.bootY[0] = 7;
	assert(!chooseBalancedStarts(bare, untouched, 20 * 20));
	assert(untouched.bootX[0] == 7 && untouched.bootY[0] == 7);
}

// scatterResources shares each band out between landmasses: two islands both get farmland and
// stone, and nothing lands on water.
inline void scatterChecks()
{
	Game game(nullptr);
	game.map.setSize(6, 6);
	game.map.setGame(&game);
	game.map.makeHomogenMap(WATER);
	for (int y = 20; y < 40; ++y)
		for (int x = 0; x < 64; ++x)
			if ((x >= 4 && x < 24) || (x >= 36 && x < 56))
				game.map.setUMTerrain(x, y, GRASS);
	game.map.controlSand();
	game.map.rebuildTerrain();
	GenerationRequest request;
	request.seed = 13;
	GenerationContext context(request);
	scatterResources(game, context, {40, 20, 30, 0, 0});
	int wheat[2] = {0, 0}, wood[2] = {0, 0}, stone[2] = {0, 0};
	for (int y = 0; y < game.map.getH(); ++y)
		for (int x = 0; x < game.map.getW(); ++x)
		{
			const int type = game.map.getResource(x, y).type;
			if (type == NO_RES_TYPE)
				continue;
			assert(!game.map.isWater(x, y));
			const int island = x < 30 ? 0 : 1;
			wheat[island] += type == WHEAT;
			wood[island] += type == WOOD;
			stone[island] += type == STONE;
		}
	for (int island = 0; island < 2; ++island)
		assert(wheat[island] > 0 && wood[island] > 0 && stone[island] > 0);
	assert(wheat[0] + wheat[1] > wood[0] + wood[1]);
}

// Lattice noise tiles the torus, stays in range and is deterministic; percentile is what it says.
inline void noiseChecks()
{
	std::mt19937 rng(77), same(77);
	const PeriodicNoise noise(64, 32, 8, rng), twin(64, 32, 8, same);
	for (int y = 0; y < 32; y += 3)
		for (int x = 0; x < 64; x += 5)
		{
			const double v = noise.at(x + 0.5, y + 0.5);
			assert(v >= 0 && v < 1 && v == twin.at(x + 0.5, y + 0.5));
			assert(std::abs(noise.at(x + 64.5, y + 0.5) - v) < 1e-9);
			assert(std::abs(noise.at(x + 0.5, y - 31.5) - v) < 1e-9);
		}
	const std::vector<int> field = periodicNoise(64, 32, 16, rng);
	const std::vector<int> fractal = fractalNoise(64, 32, 16, 3, rng);
	assert(field.size() == 64 * 32 && fractal.size() == field.size());
	for (int v : field)
		assert(v >= 0 && v <= 65535);
	for (int v : fractal)
		assert(v >= 0 && v <= 65535);
	const std::vector<float> torus = torusNoise(64, 32, rng);
	float peak = 0;
	for (float v : torus)
	{
		assert(v >= -1 && v <= 1);
		peak = std::max(peak, std::abs(v));
	}
	assert(peak == 1.0f);
	assert(percentile({5, 1, 4, 2, 3}, 40) == 3 && percentile({5, 1, 4, 2, 3}, 0) == 1);
	assert(percentile({5, 1, 4, 2, 3}, 100) == 5 && percentile({}, 50) == 0);
}

// The wedge frame divides the map into equal wedges round the centre, with the arc offset zero
// on each wedge's middle; a blob holds what lies inside its outline.
inline void wedgeChecks()
{
	const Torus t(64, 64);
	const WedgeFrame frame(t, 0.0, 4);
	assert(frame.cx == 32 && frame.cy == 32 && std::abs(frame.wedge - kPi / 2) < 1e-12);
	int perWedge[4] = {0, 0, 0, 0};
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const WedgeFrame::Cell cell = frame.cell(x, y);
			assert(cell.k >= 0 && cell.k < 4 && cell.u >= 0 && cell.u < 1);
			assert(std::abs(cell.s - cell.d * (cell.u - 0.5) * frame.wedge) < 1e-9);
			assert(std::abs(cell.d - std::hypot(double(cell.dx), double(cell.dy))) < 1e-12);
			++perWedge[cell.k];
		}
	for (int k = 0; k < 4; ++k)
		assert(perWedge[k] > 64 * 64 / 5);
	const WedgeFrame::Cell middle = frame.cell(32 + 7, 32 + 7); // straight down wedge 0's middle
	assert(middle.k == 0 && std::abs(middle.s) < 1e-9 && std::abs(middle.u - 0.5) < 1e-12);
	WedgeFrame::Cell bent = middle;
	frame.bend(bent, 2.0);
	assert(bent.s < middle.s - 1.5 && bent.d == middle.d);
	GenerationRequest request;
	request.seed = 2;
	GenerationContext context(request);
	const Blob round{0, 10, 1.0, 0.0, RadialShape(3, 0, context, "blob")};
	assert(round.holds(0, 0) && round.holds(2.5, 0) && !round.holds(0, 3.5) && round.reach() == 3);
	const Blob stretched{0, 10, 2.0, 0.0, RadialShape(3, 0, context, "blob")};
	assert(stretched.holds(5, 0) && !stretched.holds(0, 2) && stretched.reach() == 6);
}

// GenerationContext::shuffle is a permutation drawn from its stream: deterministic per seed and
// name, independent of the other streams.
inline void shuffleChecks()
{
	GenerationRequest request;
	request.seed = 6;
	GenerationContext context(request), again(request);
	std::vector<int> order(20), same(20);
	for (int i = 0; i < 20; ++i)
		order[i] = same[i] = i;
	context.bounded("other", 1000);
	context.shuffle(order.begin(), order.end(), "deal");
	again.shuffle(same.begin(), same.end(), "deal");
	assert(order == same && order != std::vector<int>({0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
													   10, 11, 12, 13, 14, 15, 16, 17, 18, 19}));
	std::vector<int> sorted = order;
	std::sort(sorted.begin(), sorted.end());
	for (int i = 0; i < 20; ++i)
		assert(sorted[i] == i);
	struct Layout
	{
		Torus t{1, 1};
		std::string failure;
	};
	Game game(nullptr);
	game.map.setSize(5, 5);
	Layout rebuilt;
	rebuilt.t = {32, 32};
	assert(designMismatch(rebuilt, game.map, "test").empty());
	rebuilt.t = {16, 32};
	assert(designMismatch(rebuilt, game.map, "test") ==
		   "The test design does not match the map size.");
	rebuilt.failure = "no room";
	assert(designMismatch(rebuilt, game.map, "test") ==
		   "The test design could not be rebuilt: no room");
}

// strokePath: a thick line covers exactly the tiles within its half width, tapers between points,
// wraps across the seam and draws a lone point as a disc; bezierPath runs end to end through the
// pull of its control; fillShape turns an outline without changing its area.
inline void drawingChecks()
{
	const Torus t(32, 16);
	std::vector<unsigned char> mask(t.size(), 0);
	strokePath(mask, t, {{2, 8, 2.5}, {12, 8, 2.5}});
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const double dx = x < 2 ? 2 - x : x > 12 ? x - 12 : 0, dy = y - 8;
			assert(mask[t.at(x, y)] == (dx * dx + dy * dy < 2.5 * 2.5));
		}
	std::fill(mask.begin(), mask.end(), 0);
	strokePath(mask, t, {{-3, 4, 1.5}, {3, 4, 1.5}}, 7);
	assert(mask[t.at(31, 4)] == 7 && mask[t.at(29, 4)] == 7 && mask[t.at(2, 4)] == 7);
	assert(mask[t.at(27, 4)] == 0 && mask[t.at(31, 6)] == 0);
	std::fill(mask.begin(), mask.end(), 0);
	strokePath(mask, t, {{16, 8, 4}, {26, 8, 0.5}});
	assert(mask[t.at(16, 11)] && !mask[t.at(25, 9)] && mask[t.at(25, 8)]);
	std::fill(mask.begin(), mask.end(), 0);
	strokePath(mask, t, {{10, 10, 1.1}});
	assert(std::count(mask.begin(), mask.end(), 1) == 5);
	const std::vector<StrokePoint> curve = bezierPath({0, 0}, {10, 10}, {20, 0}, 1, 3, 10);
	assert(curve.size() == 11 && curve.front().x == 0 && curve.back().x == 20);
	assert(std::abs(curve[5].y - 5) < 1e-9 && std::abs(curve[5].halfWidth - 2) < 1e-9);

	GenerationRequest request;
	request.seed = 11;
	GenerationContext context(request);
	const RadialShape shape(5, 0.4, context, "shape");
	std::vector<unsigned char> upright(t.size(), 0), turned(t.size(), 0);
	fillShape(upright, t, 16, 8, shape);
	fillShape(turned, t, 16, 8, shape, kPi / 2);
	const long area = std::count(upright.begin(), upright.end(), 1);
	assert(area > 40 && std::abs(area - std::count(turned.begin(), turned.end(), 1)) < area / 8);
	assert(upright != turned);
}

// bentPath runs its length along its heading and bows to the left; tracePath is one tile thick and
// unbroken; pathClearance measures the water
// between two strokes and skips a branch's root; growBranches forks level by level to its depth,
// offers every branch to accept, retries a refused one at half length and grows the same tree from
// the same rolls.
inline void branchChecks()
{
	const std::vector<StrokePoint> straight = bentPath({0, 0}, 0, 20, 0, 3, 1, 10);
	assert(straight.size() == 11 && std::abs(straight.back().x - 20) < 1e-9 &&
		   std::abs(straight.back().y) < 1e-9 && std::abs(straight.back().halfWidth - 1) < 1e-9);
	const std::vector<StrokePoint> bowed = bentPath({0, 0}, 0, 20, 0.1, 1, 1, 10);
	assert(std::abs(bowed[5].y - 2) < 1e-9 && std::abs(bowed.back().x - 20) < 1e-9);

	const std::vector<StrokePoint> a{{0, 0, 1}, {20, 0, 1}}, b{{0, 6, 1}, {20, 6, 1}};
	assert(std::abs(pathClearance(a, b) - 4) < 1e-9 && std::abs(pathClearance(b, a) - 4) < 1e-9);
	const std::vector<StrokePoint> crossing{{10, -5, 1}, {10, 5, 1}};
	assert(pathClearance(a, crossing) < 0);
	const std::vector<StrokePoint> leaving{{10, 0, 1}, {10, 20, 1}};
	assert(pathClearance(leaving, a) < 0 && pathClearance(leaving, a, 4) > 1.9);
	const std::vector<StrokePoint> dot{{10, 4, 1}};
	assert(std::abs(pathClearance(dot, a) - 2) < 1e-9);
	// tracePath: one tile thick and unbroken, even along a shallow diagonal and across the wrap.
	{
		const Torus small(32, 16);
		std::vector<unsigned char> line(small.size(), 0);
		tracePath(line, small, {{2, 3, 5}, {29, 11, 5}, {35, 12, 5}});
		for (int x = 2; x <= 29; ++x)
		{
			int column = 0;
			for (int y = 0; y < small.h; ++y)
				column += line[small.at(x, y)];
			assert(column >= 1 && column <= 2);
		}
		assert(line[small.at(2, 3)] && line[small.at(29, 11)] && line[small.at(3, 12)]);
		assert(!line[small.at(15, 14)] && !line[small.at(10, 0)]);
	}
	const PathBounds bounds = pathBounds(a);
	assert(std::abs(bounds.x - 10) < 1e-9 && std::abs(bounds.radius - 11) < 1e-9);

	ForkStyle style;
	style.spreadJitter = 0;
	style.lengthJitter = 0;
	style.bend = 0;
	style.minimumLength = 1;
	const auto grow = [&](std::uint32_t seed, int forks, bool refuseSecond)
	{
		std::mt19937 random(seed);
		const auto roll = [&] { return random() / 4294967296.0; };
		int offered = 0;
		const auto accept = [&](const std::vector<StrokePoint> &, int)
		{ return !(refuseSecond && ++offered == 2); };
		std::vector<Branch> tree;
		growBranches(tree, -1, {0, 0}, 0, 16, 2, forks, style, roll, accept);
		return tree;
	};
	const std::vector<Branch> full = grow(3, 2, false);
	// Level by level: the root, its two children, then their four.
	assert(full.size() == 7 && full[0].parent < 0 && !full[0].leaf && full[2].depth == 1 &&
		   !full[2].leaf && full[3].depth == 2 && full[6].parent == 2 && full[6].leaf);
	const std::vector<Branch> again = grow(3, 2, false);
	for (size_t i = 0; i < full.size(); ++i)
		assert(full[i].path.back().x == again[i].path.back().x &&
			   full[i].path.back().y == again[i].path.back().y);
	// The first child is refused once and kept at half length, so its subtree is shorter too.
	const std::vector<Branch> retried = grow(3, 2, true);
	assert(retried.size() == 7);
	const auto length = [](const Branch &branch)
	{
		return std::hypot(branch.path.back().x - branch.path.front().x,
						  branch.path.back().y - branch.path.front().y);
	};
	assert(std::abs(length(retried[1]) - length(full[1]) / 2) < 1e-6);
}

// Stretch is exactly the identity on a square map and fills a rectangle as an ellipse; a stretched
// WedgeFrame measures cells in the round design frame; a stretched shape fill becomes an oval.
inline void stretchChecks()
{
	const Stretch square = Stretch::toFill(64, 64);
	assert(square.sx == 1 && square.sy == 1 && square.heading(1.234) == 1.234);
	const ShapePoint same = square.apply(32, 32, {10.1, 3.7});
	assert(same.x == 10.1 && same.y == 3.7);
	const Stretch wide = Stretch::toFill(64, 16);
	assert(wide.sx == 4 && wide.sy == 1 && wide.longest() == 4);
	const ShapePoint distant = wide.apply(32, 8, {40, 8});
	assert(distant.x == 64 && distant.y == 8);
	assert(std::abs(wide.heading(kPi / 4) - std::atan2(1.0, 4.0)) < 1e-12);

	const Torus t(64, 16);
	const WedgeFrame round(t, 0, 4, wide);
	assert(std::abs(round.cell(32 + 20, 8).d - 5) < 1e-9);
	assert(std::abs(round.cell(32, 8 + 5).d - 5) < 1e-9);

	GenerationRequest request;
	request.seed = 3;
	GenerationContext context(request);
	const RadialShape disc(4, 0, context, "stretch");
	std::vector<unsigned char> plain(t.size(), 0), oval(t.size(), 0);
	fillShape(plain, t, 32, 8, disc);
	fillShape(oval, t, 32, 8, disc, 0, 1, wide);
	assert(plain[t.at(35, 8)] && !plain[t.at(40, 8)] && oval[t.at(45, 8)] && !oval[t.at(32, 12)]);
	assert(std::count(oval.begin(), oval.end(), 1) > 3 * std::count(plain.begin(), plain.end(), 1));
}

// sprinkleSand turns only eligible inland grass, the noisiest share of it, and keeps a strip of
// grass by the shore; algaeGrowthChance is zero on land and far from sand, higher near sand, and
// matches the engine's offset weights exactly.
inline void dressingChecks()
{
	const Torus t(32, 32);
	TerrainSketch sketch(t.size(), WATER);
	for (int y = 4; y < 28; ++y)
		for (int x = 4; x < 28; ++x)
			sketch[t.at(x, y)] = GRASS;
	std::vector<unsigned char> eligible(t.size(), 1);
	eligible[t.at(16, 16)] = 0;
	sprinkleSand(sketch, t, eligible, 0.25, 3, [&](int i) { return double(i % t.w); });
	int sand = 0;
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			if (sketch[t.at(x, y)] == SAND)
			{
				++sand;
				assert(x >= 21 && x <= 25 && y >= 6 && y <= 25 && !(x == 16 && y == 16));
			}
	// Steps are Chebyshev: 20 by 20 tiles lie three or more from the water, less the one refused,
	// and the quarter of them turned are the five highest columns.
	assert(sand == int(std::lround((20 * 20 - 1) * 0.25)) && sketch[t.at(24, 12)] == SAND &&
		   sketch[t.at(7, 12)] == GRASS);

	Game game(nullptr);
	grassMap(game, 6, 6);
	Map &map = game.map;
	const Torus m(map);
	for (int y = 0; y < m.h; ++y)
		for (int x = 0; x < m.w; ++x)
			map.setUMTerrain(x, y, x >= 40 && x < 48 ? SAND : WATER);
	map.rebuildTerrain();
	const std::vector<double> chance = algaeGrowthChance(map, m);
	assert(chance[m.at(44, 10)] == 0);                   // sand, not water
	assert(chance[m.at(36, 10)] > chance[m.at(20, 10)]); // nearer the sand grows faster
	// A probability: the weights of all offsets sum to 1.
	assert(chance[m.at(36, 10)] > 0 && chance[m.at(36, 10)] <= 1);
	// Every tile of the sea sees the strip within 30 tiles, but no offset can reach sand from a
	// map that has none.
	for (int x = 0; x < m.w; ++x)
		map.setUMTerrain(x, 5, WATER);
	for (int y = 0; y < m.h; ++y)
		for (int x = 40; x < 48; ++x)
			map.setUMTerrain(x, y, WATER);
	map.rebuildTerrain();
	const std::vector<double> dry = algaeGrowthChance(map, m);
	assert(std::count(dry.begin(), dry.end(), 0.0) == long(dry.size()));
}

// plantFields deals the preferred tiles, wheat first by the split key, in proportion; KitFrame
// turns its offsets with its facing; scatterClumps stops after its attempts on ineligible ground.
inline void layerChecks()
{
	Game game(nullptr);
	grassMap(game, 6, 6);
	Map &map = game.map;
	const Torus t(map);
	std::vector<int> tiles;
	for (int x = 0; x < 20; ++x)
		tiles.push_back(t.at(x, 5));
	plantFields(map, t, tiles, 6, 3, [](int i) { return -i; });
	assert(countResource(map, WHEAT) == 6 && countResource(map, WOOD) == 3);
	assert(map.getResource(8, 5).type == WHEAT && map.getResource(0, 5).type == WOOD);
	assert(map.getResource(9, 5).type == NO_RES_TYPE);

	const KitFrame east{10, 10, 0}, south{10, 10, kPi / 2};
	assert(east.at(4, 1, 3).x == 14 && east.at(4, 1, 3).y == 11 && east.at(4, 1, 3).within == 3);
	assert(south.at(4, 1, 3).x == 9 && south.at(4, 1, 3).y == 14);

	GenerationRequest request;
	request.seed = 3;
	GenerationContext context(request);
	std::vector<int> ground{t.at(40, 40), t.at(50, 50)};
	int calls = 0;
	assert(scatterClumps(
			   context, t, ground, 4, "clumps", [&](int i) { return i == ground[1]; },
			   [&](MapGeneratorPoint p)
			   {
				   assert(p.x == 50 && p.y == 50);
				   ++calls;
			   }) == 4 &&
		   calls == 4);
	assert(scatterClumps(
			   context, t, ground, 2, "clumps", [](int) { return false; },
			   [](MapGeneratorPoint) { assert(false); }) == 0);
	assert(scatterClumps(
			   context, t, {}, 2, "clumps", [](int) { return true; },
			   [](MapGeneratorPoint) { assert(false); }) == 0);
}

// Walls: a sealed coast keeps anything landing on the beach out; labelBorders leaves no diagonal
// step between regions and ignores unlabelled ground; designedStone reports tiles that cannot hold
// stone; towerReach measures from a footprint's top-left tile; growWater stops at its target or
// its ground; arcPath runs round its circle.
inline void wallChecks()
{
	Game game(nullptr);
	grassMap(game, 5, 5);
	Map &map = game.map;
	const Torus t(map);
	TerrainSketch sketch(t.size(), WATER);
	for (int y = 6; y < 26; ++y)
		for (int x = 6; x < 26; ++x)
			sketch[t.at(x, y)] = GRASS;
	layBeaches(sketch, t);
	writeUndermap(map, sketch);
	std::vector<unsigned char> sea(t.size(), 0), none(t.size(), 0), all(t.size(), 1);
	for (int i = 0; i < t.size(); ++i)
		sea[i] = map.getUMTerrain(i % t.w, i / t.w) == WATER;
	const std::vector<unsigned char> margin = seaMargin(map, t, sea, none);
	const std::vector<unsigned char> stone = sealCoasts(map, t, margin, all);
	assert(std::count(stone.begin(), stone.end(), 1) > 40 && !margin[t.at(16, 16)]);
	for (int i = 0; i < t.size(); ++i)
		if (stone[i])
		{
			assert(!margin[i] && map.getTerrainType(i % t.w, i / t.w) == GRASS);
			map.setResource(i % t.w, i / t.w, STONE, 1);
		}
	std::vector<unsigned char> beach(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		beach[i] = margin[i] && !map.isWater(i % t.w, i / t.w);
	assert(std::count(beach.begin(), beach.end(), 1) > 0);
	const std::vector<int> landed = reachesWithShut(map, t, beach, none);
	assert(landed[t.at(16, 16)] < 0);
	std::vector<unsigned char> middle(t.size(), 0);
	middle[t.at(16, 16)] = 1;
	assert(reachesWithShut(map, t, middle, none)[t.at(12, 12)] >= 0);
	std::vector<unsigned char> row(t.size(), 0);
	for (int x = 0; x < t.w; ++x)
		row[t.at(x, 14)] = 1;
	assert(reachesWithShut(map, t, middle, row)[t.at(12, 12)] < 0);
	std::vector<unsigned char> onBeach(t.size(), 0);
	onBeach[t.at(6, 6)] = onBeach[t.at(16, 16)] = 1;
	const DesignedStone designed = designedStone(map, t, onBeach);
	assert(designed.gaps == 1 && designed.firstGap == t.at(6, 6) && designed.stone[t.at(16, 16)]);

	const Torus square(16, 16);
	std::vector<int> labels(square.size(), 0);
	for (int y = 0; y < square.h; ++y)
		for (int x = 8; x < 16; ++x)
			labels[square.at(x, y)] = x == 12 ? -1 : 1;
	const std::vector<unsigned char> border = labelBorders(square, labels);
	for (int y = 0; y < square.h; ++y)
		for (int x = 0; x < square.w; ++x)
		{
			const int i = square.at(x, y);
			assert(border[i] == (x == 8 || x == 15));
			if (labels[i] != 0 || border[i])
				continue;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = square.at(x + dx, y + dy);
					assert(labels[j] != 1 || border[j]);
				}
		}

	const Torus strip(32, 8);
	std::vector<unsigned char> buildable(strip.size(), 0), target(strip.size(), 0);
	for (int y = 0; y < strip.h; ++y)
	{
		for (int x = 0; x < 4; ++x)
			buildable[strip.at(x, y)] = 1;
		target[strip.at(9, y)] = 1;
	}
	assert(towerReach(strip, buildable, target) == 6);
	std::fill(buildable.begin(), buildable.end(), 0);
	for (int y = 0; y < strip.h; ++y)
		buildable[strip.at(2, y)] = 1;
	assert(towerReach(strip, buildable, target) == INT_MAX);

	std::vector<unsigned char> water(square.size(), 0);
	std::vector<int> queued(square.size(), 0);
	const auto flat = [](int tile) { return tile; };
	assert(growWater(
			   square, water, square.at(8, 8), 20, [](int) { return true; }, flat, queued, 1) ==
			   20 &&
		   std::count(water.begin(), water.end(), 1) == 20);
	std::fill(water.begin(), water.end(), 0);
	const auto smallRoom = [&](int tile) { return tile % square.w < 3 && tile / square.w < 2; };
	assert(growWater(square, water, 0, 20, smallRoom, flat, queued, 2) == 6);

	const std::vector<StrokePoint> arc = arcPath(10, 10, 10, 0, kPi / 2, 2, 3);
	assert(std::abs(arc.front().x - 20) < 1e-9 && std::abs(arc.back().y - 20) < 1e-9);
	for (size_t k = 1; k < arc.size(); ++k)
	{
		assert(std::abs(std::hypot(arc[k].x - 10, arc[k].y - 10) - 10) < 1e-9);
		assert(std::hypot(arc[k].x - arc[k - 1].x, arc[k].y - arc[k - 1].y) <= 3);
	}
}

// growTerritories: equal areas to the tile on an open torus, each territory in one piece, the same
// result grown twice, and a boxed-in territory that stops while the rest share out the remainder.
inline void territoryChecks()
{
	const Torus t(30, 30);
	std::vector<unsigned char> eligible(t.size(), 1);
	const std::vector<std::vector<int>> seeds = {{t.at(5, 5)}, {t.at(20, 8)}, {t.at(12, 22)}};
	const auto noise = [&](int tile) { return (tile * 2654435761u) % 700; };
	const Territories grown = growTerritories(t, eligible, seeds, noise);
	assert(grown.areas == std::vector<int>({300, 300, 300}));
	assert(grown.labels == growTerritories(t, eligible, seeds, noise).labels);
	for (int k = 0; k < 3; ++k)
	{
		std::vector<unsigned char> mine(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			mine[i] = grown.labels[i] == k;
		// Four-connected inside its own ground: flood cardinally by hand.
		std::vector<int> stack{seeds[k][0]};
		std::vector<unsigned char> seen(t.size(), 0);
		seen[seeds[k][0]] = 1;
		int reached = 0;
		while (!stack.empty())
		{
			const int i = stack.back();
			stack.pop_back();
			++reached;
			for (const auto &s : kCardinalSteps)
			{
				const int j = t.at(i % t.w + s[0], i / t.w + s[1]);
				if (mine[j] && !seen[j])
				{
					seen[j] = 1;
					stack.push_back(j);
				}
			}
		}
		assert(reached == 300);
	}
	// A 3x3 room walled off round colony 0's seed.
	for (int y = 3; y <= 7; ++y)
		for (int x = 3; x <= 7; ++x)
			if (x == 3 || x == 7 || y == 3 || y == 7)
				eligible[t.at(x, y)] = 0;
	const Territories boxed = growTerritories(t, eligible, seeds, noise);
	assert(boxed.areas[0] == 9 && std::abs(boxed.areas[1] - boxed.areas[2]) <= 1 &&
		   boxed.areas[1] + boxed.areas[2] == 900 - 16 - 9);
	assert(boxed.labels[t.at(3, 3)] == -1 && boxed.smallest() == 9);
}

// The arena primitives: ringWithGates leaves exact gaps; zigzagPath alternates sides and keeps its
// legs short of their turns; AxisFrame projects back what it placed; keepRoadInland and roadTiles;
// labelBorders thickens; seaEntry finds a hole in a sealed coast; plantRound and plantOrchard place
// the same at every angle; growFarLake grows a lake of the target at the far end of a region;
// siteAtDepth, smoothLabels and strandedGround; placeTower stocks a tower on allowed ground.
inline void arenaChecks()
{
	const Torus t(64, 64);
	std::vector<int> gateOf(t.size(), -2);
	ringWithGates(t, 32, 32, 20, 1.2, {0.0, kPi}, 3, [&](int i, int gate) { gateOf[i] = gate; });
	assert(gateOf[t.at(52, 32)] == 0 && gateOf[t.at(12, 32)] == 1 && gateOf[t.at(32, 52)] == -1);
	assert(gateOf[t.at(32, 32)] == -2 && gateOf[t.at(52, 36)] == -1);

	const AxisFrame frame{32, 32, kPi / 2};
	const ShapePoint p = frame.at(10, 4);
	assert(std::abs(p.x - 28) < 1e-9 && std::abs(p.y - 42) < 1e-9);
	const ShapePoint back = frame.project(p.x - 32, p.y - 32);
	assert(std::abs(back.x - 10) < 1e-9 && std::abs(back.y - 4) < 1e-9);
	const Zigzag zigzag = zigzagPath(AxisFrame{0, 0, 0}, 40, 30, 8, 3, 6, 5, 2);
	assert(zigzag.legs.size() == 3 && zigzag.path.size() == 8 && zigzag.finishAcross == -6);
	assert(zigzag.path.front().x == 40 && zigzag.path.front().y == 6 && zigzag.path.back().x == 5);
	assert(zigzag.legs[1].front().x == 22 && zigzag.legs[1].front().y == -4 &&
		   zigzag.legs[1].back().y == 4);

	std::vector<unsigned char> road(t.size(), 0), water(t.size(), 0);
	for (int x = 0; x < t.w; ++x)
		road[t.at(x, 10)] = 1;
	for (int y = 0; y < t.h; ++y)
		water[t.at(0, y)] = 1;
	keepRoadInland(road, t, water, 3);
	assert(!road[t.at(2, 10)] && road[t.at(3, 10)] && !road[t.at(62, 10)]);
	const std::vector<unsigned char> spoiled = roadTiles(t, road);
	assert(spoiled[t.at(3, 9)] && spoiled[t.at(2, 10)] && !spoiled[t.at(3, 11)]);

	std::vector<int> labels(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		labels[i] = i % t.w < 32 ? 0 : 1;
	const std::vector<unsigned char> thin = labelBorders(t, labels),
									 thick = labelBorders(t, labels, 2);
	assert(std::count(thick.begin(), thick.end(), 1) ==
		   2 * std::count(thin.begin(), thin.end(), 1));
	assert(thick[t.at(31, 5)] && thick[t.at(32, 5)] && !thin[t.at(31, 5)]);

	// Stranded ground and depth sites.
	std::vector<unsigned char> ground(t.size(), 1);
	for (int y = 0; y < t.h; ++y)
		ground[t.at(20, y)] = 0;
	const std::vector<unsigned char> stranded = strandedGround(t, {t.at(5, 5)}, ground);
	assert(!stranded[t.at(19, 5)] && !stranded[t.at(21, 5)] && !stranded[t.at(20, 5)]);
	for (int y = 0; y < t.h; ++y)
		ground[t.at(40, y)] = 0;
	const std::vector<unsigned char> cut = strandedGround(t, {t.at(5, 5)}, ground);
	assert(cut[t.at(30, 5)] && !cut[t.at(50, 5)]);
	std::vector<int> depth(t.size(), -1), room(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		depth[i] = i % t.w;
		room[i] = std::min(i / t.w, t.h - 1 - i / t.w);
	}
	const int site = siteAtDepth(depth, room, 12, 5, 0);
	assert(site % t.w == 12 && room[site] == 31);
	assert(siteAtDepth(depth, room, 12, 40, 3) == -1);

	// A lake at the far end of a strip: the same size whatever the seed, and never within the gap.
	std::vector<unsigned char> lake(t.size(), 0);
	std::vector<int> queued(t.size(), 0);
	std::vector<int> stripDepth(t.size(), -1);
	for (int y = 10; y < 30; ++y)
		for (int x = 0; x < 60; ++x)
			stripDepth[t.at(x, y)] = x;
	std::vector<unsigned char> edge(t.size(), 1);
	for (int y = 10; y < 30; ++y)
		for (int x = 0; x < 60; ++x)
			edge[t.at(x, y)] = 0;
	const std::vector<int> stripRoom = stepsFrom(t, edge);
	assert(growFarLake(
			   t, lake, stripDepth, stripRoom, 3, 60, [](int) { return 0.0; }, queued, 1) == 60);
	int distant = 0, nearby = 0;
	for (int i = 0; i < t.size(); ++i)
		if (lake[i])
		{
			assert(stripRoom[i] >= 3);
			(i % t.w > 30 ? distant : nearby)++;
		}
	assert(distant == 60 && nearby == 0);
	assert(growFarLake(
			   t, lake, stripDepth, stripRoom, 12, 60, [](int) { return 0.0; }, queued, 2) == 0);

	std::vector<int> jagged(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		jagged[i] = i % t.w < 32 ? 0 : 1;
	jagged[t.at(20, 20)] = 1;
	std::vector<unsigned char> keep(t.size(), 0);
	smoothLabels(t, jagged, 1, keep);
	assert(jagged[t.at(20, 20)] == 0 && jagged[t.at(40, 20)] == 1);

	// Clumps round a circle and a stocked tower, on a real map with a sealed island.
	Game game(nullptr);
	grassMap(game, 6, 6);
	Map &map = game.map;
	GenerationRequest request;
	request.seed = 5;
	GenerationContext context(request);
	const auto anywhere = [&](int i) { return clearGround(map, i % t.w, i / t.w); };
	assert(plantRound(map, t, context, 32, 32, 10, {0.0, kPi / 2, kPi}, STONE, 0, 2, anywhere) ==
		   3);
	assert(map.getResource(42, 32).type == STONE && map.getResource(22, 32).type == STONE);
	assert(plantOrchard(map, t, context, 32, 32, 20, {0.0}, 4, 2, 0, anywhere) == 3);
	assert(map.getResource(52, 32).type == CHERRY + 1);

	game.addTeam();
	std::vector<unsigned char> allowed(t.size(), 0);
	for (int y = 4; y < 10; ++y)
		for (int x = 4; x < 10; ++x)
			allowed[t.at(x, y)] = 1;
	const int tower = placeTower(game, 0, 1, 3, 3, 4, allowed);
	assert(tower == t.at(4, 4));
	assert(map.getBuilding(4, 4) != NOGBID && map.getBuilding(5, 5) != NOGBID);
	assert(game.teams[0]->turrets.size() == 1 && game.teams[0]->turrets.front()->bullets > 0);
	std::vector<unsigned char> none(t.size(), 0);
	assert(placeTower(game, 0, 1, 30, 30, 4, none) == -1);

	// seaEntry finds the one grass gap in a walled coast.
	TerrainSketch sketch(t.size(), WATER);
	for (int y = 20; y < 44; ++y)
		for (int x = 20; x < 44; ++x)
			sketch[t.at(x, y)] = GRASS;
	layBeaches(sketch, t);
	writeUndermap(map, sketch);
	const std::vector<unsigned char> lakes(t.size(), 0), inside(t.size(), 1);
	const std::vector<unsigned char> margin = seaMargin(map, t, seaVertices(map, t, lakes), lakes);
	std::vector<unsigned char> island(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		island[i] = !map.isWater(i % t.w, i / t.w);
	std::vector<unsigned char> wall = sealCoasts(map, t, margin, island);
	for (int i = 0; i < t.size(); ++i)
		map.setNoResource(i % t.w, i / t.w, 1);
	int gap = -1;
	for (int i = 0; i < t.size(); ++i)
		if (wall[i])
		{
			if (gap < 0 && i % t.w == 32)
				gap = i;
			else
				map.setResource(i % t.w, i / t.w, STONE, 1);
		}
	assert(gap >= 0 && seaEntry(map, t, margin, island) >= 0);
	map.setResource(gap % t.w, gap / t.w, STONE, 1);
	assert(seaEntry(map, t, margin, island) == -1);
	(void)inside;
}

// sealedSegmentTiles steps one side at a time from end tile to end tile, taking both tiles at an
// exact corner; forEachTileInPolygon takes the tiles whose centres lie inside, and polygons sharing
// edges - across the wrap too - split the tiles between them exactly.
inline void subtileRasterChecks()
{
	const auto sealed = [](SubtilePoint a, SubtilePoint b)
	{
		const auto tiles = sealedSegmentTiles(a, b);
		assert(tiles.front().first == subtileTile(a.x) && tiles.front().second == subtileTile(a.y));
		assert(tiles.back().first == subtileTile(b.x) && tiles.back().second == subtileTile(b.y));
		const std::set<std::pair<long long, long long>> line(tiles.begin(), tiles.end());
		for (size_t k = 1; k < tiles.size(); ++k)
		{
			const long long dx = tiles[k].first - tiles[k - 1].first,
							dy = tiles[k].second - tiles[k - 1].second;
			assert(std::llabs(dx) <= 1 && std::llabs(dy) <= 1 && (dx || dy));
			if (dx && dy)
				assert(line.count({tiles[k - 1].first + dx, tiles[k - 1].second}) &&
					   line.count({tiles[k - 1].first, tiles[k - 1].second + dy}));
		}
		return tiles;
	};
	assert(sealed(subtileCentre(2, 5), subtileCentre(9, 5)).size() == 8);
	assert(sealed(subtileCentre(3, 3), subtileCentre(3, -4)).size() == 8);
	// A diagonal through tile corners exactly: every step takes both tiles beside the corner.
	assert(sealed({0, 0}, {5 * kSubtile, 5 * kSubtile}).size() == 16);
	for (int k = 0; k < 40; ++k)
		sealed({k * 7 - 90, k * 13 - 50}, {300 - k * 11, k * 5 + 17});

	const Torus t(24, 16);
	std::vector<unsigned char> mask(t.size(), 0);
	fillPolygon(mask, t,
				{{2 * kSubtile, 3 * kSubtile},
				 {5 * kSubtile, 3 * kSubtile},
				 {5 * kSubtile, 7 * kSubtile},
				 {2 * kSubtile, 7 * kSubtile}});
	assert(std::count(mask.begin(), mask.end(), 1) == 12 && mask[t.at(2, 3)] && mask[t.at(4, 6)] &&
		   !mask[t.at(5, 3)] && !mask[t.at(2, 7)]);
	// Two triangles either side of a slanted cut through a strip that wraps the seam.
	std::vector<int> cover(t.size(), 0);
	const SubtilePoint a{-70, 2 * kSubtile + 3}, b{19 * kSubtile + 5, 2 * kSubtile + 3},
		c{19 * kSubtile + 5, 12 * kSubtile - 9}, d{-70, 12 * kSubtile - 9};
	forEachTileInPolygon(t, {a, b, c}, [&](int tile) { ++cover[tile]; });
	forEachTileInPolygon(t, {a, c, d}, [&](int tile) { ++cover[tile]; });
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const long long cx = x * kSubtile + kSubtile / 2, cy = y * kSubtile + kSubtile / 2;
			const bool inside = cy >= a.y && cy < d.y &&
								((cx >= a.x && cx < b.x) ||
								 (cx - t.w * kSubtile >= a.x && cx - t.w * kSubtile < b.x));
			assert(cover[t.at(x, y)] == (inside ? 1 : 0));
		}
}

// A tiling's cells cover every tile exactly once, edges join their cells from both sides with the
// same two corners, the counts obey Euler's formula for a torus, lattice symmetries keep neighbours
// neighbours, and warping keeps the tiling whole and the walls' passages open, the same way twice.
inline void tessellationChecks()
{
	const auto contract = [](const Tessellation &g, int degree)
	{
		assert(int(g.corners.size()) - int(g.edges.size()) + g.cellCount() == 0);
		const std::vector<int> labels = labelTiles(g);
		assert(int(labels.size()) == g.t.size());
		std::vector<int> area(g.cells.size(), 0);
		for (int label : labels)
			++area[label];
		for (int cell = 0; cell < g.cellCount(); ++cell)
		{
			assert(area[cell] > 0 && int(g.cells[cell].edges.size()) == degree);
			assert(labels[g.t.at(g.centreTileX(cell), g.centreTileY(cell))] == cell);
			for (int edge : g.cells[cell].edges)
			{
				const int next = g.other(edge, cell);
				const auto &back = g.cells[next].edges;
				assert(std::find(back.begin(), back.end(), edge) != back.end());
				// Seen from the other side the edge runs backwards, one whole map shift away.
				const auto here = g.edgeEnds(edge, cell), there = g.edgeEnds(edge, next);
				const long long sx = here.first.x - there.second.x,
								sy = here.first.y - there.second.y;
				assert(sx % (g.t.w * kSubtile) == 0 && sy % (g.t.h * kSubtile) == 0);
				assert(here.second.x - there.first.x == sx && here.second.y - there.first.y == sy);
				const SubtilePoint across = g.centreAcross(edge, cell);
				assert((across.x - g.cells[next].centre.x) == sx &&
					   (across.y - g.cells[next].centre.y) == sy);
			}
		}
		const RegionGraph graph = g.neighbours();
		for (int s = 0; s < 8; ++s)
			for (int cell = 0; cell < g.cellCount(); ++cell)
			{
				const auto move = [&](int c)
				{ return g.transform(c, s * 3 % g.columns, s % g.rows, s & 1, (s >> 1) & 1); };
				const int image = move(cell);
				for (int next : graph[cell])
					assert(std::count(graph[image].begin(), graph[image].end(), move(next)) > 0);
			}
		return labels;
	};
	const Tessellation squares = squareTessellation(64, 48, 16);
	assert(squares.columns == 4 && squares.rows == 3);
	const std::vector<int> squareLabels = contract(squares, 4);
	assert(squareLabels[squares.t.at(15, 0)] == 0 && squareLabels[squares.t.at(16, 0)] == 1);
	contract(squareTessellation(64, 64, 20), 4); // uneven pitches, 3 cells of 21 or 22 tiles
	contract(squareTessellation(32, 32, 16), 4); // two cells across: every neighbour twice
	const Tessellation hexes = hexTessellation(128, 128, 30);
	assert(hexes.columns == 4 && hexes.rows % 2 == 0);
	contract(hexes, 6);
	contract(hexTessellation(64, 64, 32), 6);
	contract(hexTessellation(256, 128, 48), 6);

	GenerationRequest request;
	request.seed = 9;
	for (const Tessellation &lattice : {squares, hexes})
	{
		Tessellation first = lattice, second = lattice;
		std::vector<unsigned char> walls(lattice.edges.size(), 0);
		for (size_t edge = 0; edge < walls.size(); ++edge)
			walls[edge] = edge % 3 != 0;
		const int limit = warpLimit(lattice);
		assert(limit > 0);
		GenerationContext one(request), two(request), untouched(request);
		warpCorners(first, limit, walls, 6, 3, one, "warp");
		warpCorners(second, limit, walls, 6, 3, two, "warp");
		assert(first.corners.size() == lattice.corners.size());
		bool moved = false;
		for (size_t k = 0; k < first.corners.size(); ++k)
		{
			assert(first.corners[k] == second.corners[k]);
			moved = moved || !(first.corners[k] == lattice.corners[k]);
		}
		assert(moved);
		contract(first, lattice.shape == Tessellation::Shape::Square ? 4 : 6);
		// No move at all draws nothing.
		Tessellation still = lattice;
		warpCorners(still, 0, walls, 6, 3, untouched, "warp");
		GenerationContext fresh(request);
		assert(untouched.bounded("warp", 1000) == fresh.bounded("warp", 1000));
		// Walls that share no corner keep their unwarped gap or 6 steps, whichever is less.
		for (size_t e = 0; e < walls.size(); ++e)
			for (size_t f = e + 1; f < walls.size(); ++f)
			{
				if (!walls[e] || !walls[f])
					continue;
				const auto &ce = lattice.edges[e].corners, &cf = lattice.edges[f].corners;
				if (ce[0] == cf[0] || ce[0] == cf[1] || ce[1] == cf[0] || ce[1] == cf[1])
					continue;
				const auto steps = [&](const Tessellation &g)
				{
					const auto p = g.edgeEnds(int(e), g.edges[e].cells[0]);
					const auto q = g.edgeEnds(int(f), g.edges[f].cells[0]);
					int least = INT_MAX;
					for (const auto &u : sealedSegmentTiles(p.first, p.second))
						for (const auto &v : sealedSegmentTiles(q.first, q.second))
							least = std::min(least, g.t.chebyshev(int(u.first), int(u.second),
																  int(v.first), int(v.second)));
					return least;
				};
				assert(steps(first) >= std::min(steps(lattice), 6));
			}
	}
}

// Arbitrary protected tiles need exact thick-wall clearance, including wrapping.
// These fixtures exercise safe/no-op, repair, bounded fallback and impossible
// reservations without depending on any generator's farm policy or random seed.
inline void boundaryExclusionChecks()
{
	const auto reference = squareTessellation(64, 48, 16);
	std::vector<unsigned char> edges(reference.edges.size(), 1);
	const auto thin = rasterizeBoundaries(reference, edges);
	const auto thick = rasterizeBoundaries(reference, edges, 2);
	assert(thick == dilate(reference.t, thin, 2));
	assert(thick[reference.t.at(-1, 8)]); // thick boundary wraps through x=0
	std::vector<unsigned char> none(edges.size(), 0);
	const auto absent = rasterizeBoundaries(reference, none, 2);
	assert(std::count(absent.begin(), absent.end(), 1) == 0);
	std::vector<unsigned char> excluded(reference.t.size(), 0);
	for (int cell = 0; cell < reference.cellCount(); ++cell)
		excluded[reference.t.at(reference.centreTileX(cell), reference.centreTileY(cell))] = 1;
	auto safe = reference;
	assert(relaxWarpOutside(safe, reference.corners, edges, excluded, 2) == 0);
	assert(safe.corners == reference.corners);
	auto shifted = reference;
	for (auto &corner : shifted.corners)
		corner.x += 8 * 16; // a wall now passes through every protected centre
	const auto collided = rasterizeBoundaries(shifted, edges, 2);
	assert(collided[reference.t.at(8, 8)]);
	auto repaired = shifted, repeated = shifted;
	const int reductions = relaxWarpOutside(repaired, reference.corners, edges, excluded, 2);
	assert(reductions > 0);
	assert(relaxWarpOutside(repeated, reference.corners, edges, excluded, 2) == reductions);
	assert(repaired.corners == repeated.corners);
	const auto cleared = rasterizeBoundaries(repaired, edges, 2);
	for (int tile = 0; tile < reference.t.size(); ++tile)
		assert(!(cleared[tile] && excluded[tile]));
	assert(!labelTiles(repaired).empty());
	// One allowed contraction uses the exact reference instead of an unbounded retry.
	assert(relaxWarpOutside(shifted, reference.corners, edges, excluded, 2, 1) == 1);
	assert(shifted.corners == reference.corners);
	std::fill(excluded.begin(), excluded.end(), 1);
	assert(relaxWarpOutside(shifted, reference.corners, edges, excluded, 2, 2) == -1);
	assert(shifted.corners == reference.corners);
}

// The maze toolkit on a small tiling: pockets that fit, a spanning tree through the rest, one door
// per pocket, loops that never touch a pocket, dead ends with their one exit; and firstRegionLeak
// finds ground shared by regions that aren't allowed to share it.
inline void graphMazeChecks()
{
	for (const Tessellation &g : {squareTessellation(96, 96, 16), hexTessellation(128, 128, 30)})
	{
		const int n = g.cellCount();
		std::vector<unsigned char> surrounded(g.cells.size(), 0);
		for (int edge : g.cells[0].edges)
			surrounded[g.other(edge, 0)] = 1;
		surrounded[0] = 1;
		assert(!pocketsFit(g, surrounded));
		const std::vector<int> pockets = spreadPockets(g, 3);
		assert(pockets.size() == 3 && spreadPockets(g, 3) == pockets);
		std::vector<unsigned char> isPocket(g.cells.size(), 0);
		for (int pocket : pockets)
			isPocket[pocket] = 1;
		assert(pocketsFit(g, isPocket));

		GenerationRequest request;
		request.seed = 4;
		GenerationContext context(request);
		std::vector<unsigned char> open(g.edges.size(), 0);
		assert(carveSpanningTree(g, context, "maze", isPocket, open));
		assert(std::count(open.begin(), open.end(), 1) == n - 3 - 1);
		const std::vector<int> doors = openPocketDoors(g, context, "maze", pockets, isPocket, open);
		for (size_t k = 0; k < pockets.size(); ++k)
		{
			int degree = 0;
			for (int edge : g.cells[pockets[k]].edges)
				degree += open[edge];
			assert(degree == 1 && open[doors[k]] && !isPocket[g.other(doors[k], pockets[k])]);
		}
		openLoops(g, context, "maze", isPocket, 50, open);
		assert(std::count(open.begin(), open.end(), 1) == n - 1 + (n - 3) * 50 / 100);
		std::vector<int> exits;
		for (int cell : deadEnds(g, open, isPocket, exits))
		{
			int degree = 0;
			for (int edge : g.cells[cell].edges)
				degree += open[edge];
			assert(!isPocket[cell] && degree == 1 && open[exits[cell]]);
		}
	}

	const Torus t(8, 8);
	std::vector<int> labels(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		labels[i] = i % t.w < 4 ? 0 : i % t.w == 4 ? -1 : 1;
	std::vector<unsigned char> reached(t.size(), 1);
	const RegionLeak leak = firstRegionLeak(t, reached, labels, [](int, int) { return false; });
	assert(leak.tile >= 0 && labels[leak.tile] != labels[leak.neighbour]);
	assert(firstRegionLeak(t, reached, labels, [](int, int) { return true; }).tile < 0);
	for (int i = 0; i < t.size(); ++i)
		reached[i] = labels[i] == 1;
	assert(firstRegionLeak(t, reached, labels, [](int, int) { return false; }).tile < 0);
}

// Farms and towers: bestFarmRows follows the fit; layFarm lays alternating rows clear of the rim and
// plantFarm fills crop rows with wheat nearest the water and keeps its wood to one row; growFarmFields shares open water equally and parts the
// fields; separateTerritories opens a gap, fillToNearest closes it and labelBorders walls it but for a door;
// growLakeBeside keeps to its side; chooseTowerSites covers
// the other colony, starts towers facing each other empty and gives every colony its count.
inline void farmAndTowerChecks()
{
	const FarmRows straight = bestFarmRows(0), diagonal = bestFarmRows(kPi / 4),
				   turned = bestFarmRows(kPi / 2);
	assert(straight.crops == 10 && straight.water == 8 && diagonal.crops == 12 &&
		   diagonal.water == 9);
	assert(turned.crops == straight.crops && bestFarmRows(-kPi / 4).crops == diagonal.crops);
	assert(std::abs(farmYield(0) - 0.1489) < 1e-9 && std::abs(farmYield(kPi / 4) - 0.1126) < 1e-9);
	assert(std::abs(farmYield(kPi / 8) - 0.1225) < 1e-9 && farmYield(kPi / 2) == farmYield(0));
	assert(farmYield(0.1) < farmYield(0) && farmYield(0.1) > farmYield(kPi / 16));
	{
		// Shared out by worth: a claimant worth half per tile ends with about twice the area.
		const Torus open(40, 40);
		const std::vector<unsigned char> all(open.size(), 1);
		const std::vector<double> worth = {1.0, 0.5};
		const Territories byWorth = growTerritories(
			open, all, {{open.at(5, 20)}, {open.at(25, 20)}}, [](int) { return 0; }, &worth);
		assert(std::abs(byWorth.areas[1] - 2 * byWorth.areas[0]) <= 2);
	}

	Game game(nullptr);
	grassMap(game, 6, 6);
	Map &map = game.map;
	const Torus t(map);
	std::vector<unsigned char> region(t.size(), 0);
	for (int y = 10; y < 54; ++y)
		for (int x = 10; x < 54; ++x)
			region[t.at(x, y)] = 1;
	TerrainSketch sketch(t.size(), GRASS);
	const Farm farm = layFarm(sketch, t, region, 0, {32, 32}, 4, {6, 4});
	assert(farm.rows >= 3 && !farm.water[t.at(32, 32)] && farm.row[t.at(32, 32)] % 2 == 0);
	for (int i = 0; i < t.size(); ++i)
	{
		if (!region[i])
			assert(farm.row[i] == -1 && !farm.water[i]);
		if (farm.water[i])
		{
			assert(farm.row[i] % 2 == 1 && sketch[i] == WATER);
			const int x = i % t.w, y = i / t.w;
			assert(x >= 13 && x < 51 && y >= 13 && y < 51);
		}
	}
	assert(farm.water[t.at(32, 36)] && !farm.water[t.at(32, 34)]);
	{
		// Bridges: every 8 tiles along the rows from the origin, a line of sand clean across the
		// farm, through the water rows and the crop rows alike (FEEDBACK 2026-09-14), so a colony
		// crosses the whole field on one road. Only the rim keeps its crops.
		TerrainSketch bridged(t.size(), GRASS);
		const Farm crossed = layFarm(bridged, t, region, 0, {32, 32}, 4, {6, 4}, nullptr, 8);
		assert(!crossed.water[t.at(32, 36)] && crossed.sand[t.at(32, 36)] &&
			   bridged[t.at(32, 36)] == SAND);
		assert(crossed.sand[t.at(24, 36)] && crossed.water[t.at(33, 36)] &&
			   crossed.water[t.at(28, 36)]);
		assert(crossed.sand[t.at(32, 32)] && bridged[t.at(32, 32)] == SAND &&
			   crossed.rows == farm.rows);
		assert(!crossed.sand[t.at(33, 32)] && bridged[t.at(33, 32)] == GRASS);
		assert(!crossed.sand[t.at(32, 10)] && !crossed.sand[t.at(32, 53)]); // the rim stays grass
		// A building plot's grass trumps a bridge through it.
		TerrainSketch plotted(t.size(), GRASS);
		const FarmPlot clearing;
		const Farm both = layFarm(plotted, t, region, 0, {32, 32}, 4, {6, 4}, &clearing, 4);
		assert(both.plotX >= 0);
		for (int dy = 0; dy <= clearing.height; ++dy)
			for (int dx = 0; dx <= clearing.width; ++dx)
			{
				const int i = t.at(both.plotX + dx, both.plotY + dy);
				assert(!both.sand[i] && !both.water[i] && plotted[i] == GRASS);
			}
	}
	layBeaches(sketch, t);
	writeUndermap(map, sketch);
	const int planted = plantFarm(map, t, farm, 10, 10, [](int) { return true; });
	assert(planted == 20 && countResource(map, WHEAT) == 10 && countResource(map, WOOD) == 10);
	int woodRow = -1;
	for (int i = 0; i < t.size(); ++i)
		if (map.getResource(i % t.w, i / t.w).type == WOOD)
		{
			assert(woodRow < 0 || farm.row[i] == woodRow);
			woodRow = farm.row[i];
		}

	const Torus sea(64, 64);
	std::vector<unsigned char> land(sea.size(), 0), everywhere(sea.size(), 1);
	for (int y = 28; y < 36; ++y)
		for (int x = 0; x < 64; ++x)
			land[sea.at(x, y)] = 1;
	// The strip is two homes, colony 0's to the west and colony 1's to the east.
	std::vector<int> homeOf(sea.size(), -1);
	for (int i = 0; i < sea.size(); ++i)
		if (land[i])
			homeOf[i] = i % sea.w < 32 ? 0 : 1;
	const std::vector<int> fields = growFarmFields(
		sea, land, homeOf, everywhere, {{16, 26}, {48, 26}}, {0, 1}, {{16, 32}, {48, 32}}, 4, 2);
	int sizes[2] = {0, 0};
	bool touches[2] = {false, false};
	for (int i = 0; i < sea.size(); ++i)
		if (fields[i] >= 0 && !land[i])
		{
			++sizes[fields[i]];
			for (const auto &step : kCardinalSteps)
				touches[fields[i]] =
					touches[fields[i]] ||
					homeOf[sea.at(i % 64 + step[0], i / 64 + step[1])] == fields[i];
		}
	assert(sizes[0] > 400 && std::abs(sizes[0] - sizes[1]) < sizes[0] / 5 && touches[0] &&
		   touches[1]);
	std::vector<unsigned char> first(sea.size(), 0), eastHome(sea.size(), 0);
	for (int i = 0; i < sea.size(); ++i)
	{
		first[i] = fields[i] == 0 && !land[i];
		eastHome[i] = homeOf[i] == 1;
	}
	const std::vector<int> fromFirst = stepsFrom(sea, first), fromEast = stepsFrom(sea, eastHome);
	for (int i = 0; i < sea.size(); ++i)
	{
		if (fields[i] == 1 && !land[i])
			assert(fromFirst[i] >= 4);
		if (first[i])
			assert(fromEast[i] >= 4);
	}

	std::vector<int> halves(sea.size(), 0);
	for (int i = 0; i < sea.size(); ++i)
		halves[i] = i % sea.w < 32 ? 0 : 1;
	separateTerritories(sea, halves, 4);
	assert(halves[sea.at(31, 5)] == -1 && halves[sea.at(32, 5)] == -1 &&
		   halves[sea.at(29, 5)] == 0);

	// fillToNearest closes the gap again, each tile to its nearer side, and labelBorders with a door
	// walls the border everywhere but where the door leaves it open.
	{
		std::vector<unsigned char> gap(sea.size(), 0);
		for (int i = 0; i < sea.size(); ++i)
			gap[i] = halves[i] < 0;
		std::vector<int> nearby = halves;
		assert(fillToNearest(sea, nearby, gap, 1)[sea.at(30, 5)] && nearby[sea.at(30, 5)] == 0 &&
			   nearby[sea.at(31, 5)] == -1);
		const std::vector<unsigned char> filled = fillToNearest(sea, halves, gap, 4);
		assert(filled[sea.at(31, 5)] && halves[sea.at(31, 5)] == 0 && halves[sea.at(32, 5)] == 1);
		for (int i = 0; i < sea.size(); ++i)
			assert(halves[i] >= 0);
		const auto door = [&](int i, int) { return i / sea.w >= 10 && i / sea.w < 14; };
		const std::vector<unsigned char> walls = labelBorders(sea, halves, door);
		assert(walls[sea.at(32, 5)] && !walls[sea.at(31, 5)] && !walls[sea.at(32, 11)]);
		std::vector<unsigned char> reached(sea.size(), 0);
		for (int i = 0; i < sea.size(); ++i)
			reached[i] = !walls[i];
		assert(firstRegionLeak(sea, reached, halves, [](int, int) { return false; }).tile >= 0);
		// Through the door the sides meet; with the door's rows shut, nothing leaks, across the wrap too.
		for (int i = 0; i < sea.size(); ++i)
			if (i / sea.w >= 9 && i / sea.w < 15)
				reached[i] = 0;
		assert(firstRegionLeak(sea, reached, halves, [](int, int) { return false; }).tile < 0);
	}

	std::vector<unsigned char> lake(sea.size(), 0);
	std::vector<int> queued(sea.size(), 0), depth(sea.size(), 0), room(sea.size(), 20);
	const int site = sea.at(32, 32);
	assert(growLakeBeside(
			   sea, lake, depth, room, 2, 40, site, 0, 1, 6, 20, [](int) { return 0.0; }, queued,
			   1) == 40);
	assert(growLakeBeside(
			   sea, lake, depth, room, 2, 40, site, 0, -1, 6, 20, [](int) { return 0.0; }, queued,
			   2) == 40);
	for (int i = 0; i < sea.size(); ++i)
		if (lake[i])
			assert(std::abs(sea.offsetY(32, i / sea.w)) >= 6);

	std::vector<int> owner(sea.size(), -1);
	std::vector<unsigned char> buildable(sea.size(), 0), target(sea.size(), 0), keep(sea.size(), 0);
	for (int y = 0; y < 64; ++y)
		for (int x = 0; x < 64; ++x)
		{
			const int i = sea.at(x, y);
			if (x >= 4 && x < 28)
				owner[i] = 0;
			if (x >= 32 && x < 60)
				owner[i] = 1;
			buildable[i] = owner[i] >= 0;
			target[i] = owner[i] >= 0;
		}
	TowerRequest request;
	request.range = 7;
	request.towers = 3;
	request.pads = 2;
	request.spacing = 4;
	const TowerPlan plan = chooseTowerSites(sea, owner, buildable, target, keep, 2, request);
	assert(plan.towers[0].size() == 3 && plan.towers[1].size() == 3 && plan.pads[0].size() == 2);
	assert(plan.stocked[0].size() == 3 && plan.stocked[1].size() == 3);
	for (size_t a = 0; a < 3; ++a)
	{
		// A tower with the other colony's tower in range starts empty; one without starts stocked.
		bool facing = false;
		const int s = plan.towers[0][a];
		for (int o : plan.towers[1])
			facing = facing || std::max(std::abs(sea.offsetX(s % 64, o % 64)),
										std::abs(sea.offsetY(s / 64, o / 64))) -
									   1 <=
								   request.range;
		assert(bool(plan.stocked[0][a]) == !facing);
		assert(s % 64 >= 19); // beside the four-tile gap, the only border within range
	}
	const std::vector<unsigned char> footprints = towerFootprints(sea, plan);
	assert(std::count(footprints.begin(), footprints.end(), 1) == 4 * 10);
}

// Shortcuts retain parallel edge identities and rank the detour through the unchanged
// open graph. The disconnected fifth cell also exercises the unreachable sentinel.
inline void shortcutChecks()
{
	CellGraph g;
	g.cellEdges.resize(5);
	g.edgeCells = {{0, 1}, {1, 2}, {2, 3}, {0, 3}, {0, 1}, {0, 4}};
	for (int e = 0; e < int(g.edgeCells.size()); ++e)
		for (int cell : g.edgeCells[e])
			g.cellEdges[cell].push_back(e);
	std::vector<unsigned char> open{1, 1, 1, 0, 0, 0};
	assert((closedEdges(g, open) == std::vector<int>{3, 4, 5}));
	std::vector<unsigned char> blocked{0, 0, 0, 1, 0};
	assert((closedEdges(g, open, blocked) == std::vector<int>{4, 5}));
	const auto detours = edgeDetours(g, open, {4, 3, 5});
	assert(detours[3] == 3 && detours[4] == 1 && detours[5] == -1 && detours[0] == -1);
	assert((open == std::vector<unsigned char>{1, 1, 1, 0, 0, 0}));
	open[3] = 1;
	assert(edgeDetours(g, open, {3})[3] == 1);
}

// On a two-column torus the same cells share two edges. Their crossings must stay
// on different sides of the centres, while both reserve the requested central disc.
inline void cellCrossingChecks()
{
	const auto g = squareTessellation(32, 32, 16);
	std::vector<std::vector<unsigned char>> masks;
	for (int e : g.cells[0].edges)
	{
		if (g.other(e, 0) != 1)
			continue;
		const auto path = cellCrossing(g, e, 3, 1);
		assert(path.size() == 3);
		const int owner = g.edges[e].cells[0];
		const auto from = tilePoint(g.cells[owner].centre);
		const auto to = tilePoint(g.centreAcross(e, owner));
		assert(std::abs(std::hypot(path.front().x - from.x, path.front().y - from.y) - 3) < 1e-9);
		assert(std::abs(std::hypot(path.back().x - to.x, path.back().y - to.y) - 3) < 1e-9);
		masks.emplace_back(g.t.size(), 0);
		strokePath(masks.back(), g.t, path);
		assert(std::count(masks.back().begin(), masks.back().end(), 1) > 0);
		assert(cellCrossing(g, e, 100, 1).empty());
	}
	assert(masks.size() == 2);
	for (int i = 0; i < g.t.size(); ++i)
		assert(!masks[0][i] || !masks[1][i]);
}

inline void partitionChecks()
{
	const Torus t(8, 8);
	std::vector<unsigned char> open(t.size(), 0);
	std::vector<int> labels(t.size(), -1);
	// Unlabelled ground must still carry connectivity between labelled plots.
	for (int x = 0; x <= 2; ++x)
		open[t.at(x, 0)] = 1;
	labels[t.at(0, 0)] = 4;
	labels[t.at(2, 0)] = 9;
	assert(labelComponents(connectedRegions(open, t.w, t.h, true), labels).conflictTile >= 0);
	open[t.at(1, 0)] = 0;
	assert(labelComponents(connectedRegions(open, t.w, t.h, true), labels).conflictTile < 0);
	// A diagonal crop seam is a leak under eight-neighbor growth, but not cardinal growth.
	open.assign(t.size(), 0);
	labels.assign(t.size(), -1);
	open[t.at(0, 0)] = open[t.at(1, 1)] = 1;
	labels[t.at(0, 0)] = 0;
	labels[t.at(1, 1)] = 1;
	assert(labelComponents(connectedRegions(open, t.w, t.h, true, GridNeighbors::Eight), labels)
			   .conflictTile >= 0);
	assert(labelComponents(connectedRegions(open, t.w, t.h, true, GridNeighbors::Cardinal), labels)
			   .conflictTile < 0);
	// The same contract must see a connection crossing the horizontal seam.
	open.assign(t.size(), 0);
	labels.assign(t.size(), -1);
	open[t.at(0, 3)] = open[t.at(7, 3)] = 1;
	labels[t.at(0, 3)] = 0;
	labels[t.at(7, 3)] = 1;
	assert(labelComponents(connectedRegions(open, t.w, t.h, true), labels).conflictTile >= 0);
	assert(labelComponents(connectedRegions(open, t.w, t.h, false), labels).conflictTile < 0);
}

inline void gatePartitionChecks()
{
	const Torus t(16, 8);
	std::vector<unsigned char> open(t.size(), 0);
	std::vector<int> labels(t.size(), -1);
	for (int y = 1; y <= 6; ++y)
		for (int x = 1; x <= 14; ++x)
			if (x <= 6 || x >= 9)
			{
				open[t.at(x, y)] = 1;
				labels[t.at(x, y)] = x <= 6 ? 0 : 1;
			}
	// Two plugs connect the same rooms, one across the seam. They may be clearable
	// (absent from open) or already open; sealing them must yield the same partition.
	const std::vector<TileGate> gates{{{0, 1}, {t.at(7, 3), t.at(8, 3)}},
									  {{0, 1}, {t.at(15, 3), t.at(0, 3)}}};
	const auto clearable = checkGatePartition(t, open, labels, gates);
	assert(clearable.leakTile < 0 && clearable.badGate < 0);
	for (const auto &gate : gates)
		for (int tile : gate.tiles)
			open[tile] = 1;
	const auto opened = checkGatePartition(t, open, labels, gates);
	assert(opened.leakTile < 0 && opened.badGate < 0);
	auto broken = gates;
	broken[0].tiles.push_back(t.at(4, 0));
	assert(checkGatePartition(t, open, labels, broken).badGate == 0);
	broken[0].tiles.clear();
	assert(checkGatePartition(t, open, labels, broken).badGate == 0);
	broken = gates;
	broken[0].regions[1] = 2;
	assert(checkGatePartition(t, open, labels, broken).badGate == 0);
	// A two-tile diagonal bypass must be caught even though neither gate changed.
	open[t.at(7, 1)] = open[t.at(8, 2)] = 1;
	assert(checkGatePartition(t, open, labels, gates).leakTile >= 0);
	// Three mutually disconnected rooms touch one plug. Merely finding the two
	// intended sides would miss this unintended third exit.
	open.assign(t.size(), 0);
	labels.assign(t.size(), -1);
	const int rooms[] = {t.at(3, 3), t.at(5, 3), t.at(4, 5)};
	for (int k = 0; k < 3; ++k)
	{
		open[rooms[k]] = 1;
		labels[rooms[k]] = k;
	}
	assert(checkGatePartition(t, open, labels, {{{0, 1}, {t.at(4, 4)}}}).badGate == 0);
// Premade bases: the tier and garrison tables; one plan's footprints at every facing; a finished
// city and a city of sites raised on grass with their stock, units and lists; the validator's proof
// and the structural check's worker-count hook. Then the square compounds, the lane grid and its
// pads, and the routes between sites the base landscapes are built from.
inline void baseChecks()
{
	assert(baseTier(16) == BaseTier::Hamlet && baseTier(20) == BaseTier::Hamlet);
	assert(baseTier(24) == BaseTier::Town && baseTier(28) == BaseTier::Town);
	assert(baseTier(32) == BaseTier::City && baseTier(48) == BaseTier::City);
	const BaseGarrison large = baseGarrison(32, true);
	assert(large.workers == 32 && large.warriors == 12 && large.warriorLevel == 1 &&
		   large.explorers == 4);
	assert(baseGarrison(16, true).warriors == 6 && baseGarrison(16, true).explorers == 2);
	assert(baseGarrison(32, false).warriors == 0 && baseGarrison(32, false).explorers == 0);
	const BasePlan city = standardBasePlan(BaseTier::City, BaseKind::Finished, 2, true);
	const BasePlan town = standardBasePlan(BaseTier::Town, BaseKind::Finished, 0, false);
	const BasePlan hamlet = standardBasePlan(BaseTier::Hamlet, BaseKind::Finished, 0, false);
	assert(city.pieces.size() == 10 && city.depots.size() == 1 && city.reach == 10);
	assert(town.pieces.size() == 6 && hamlet.pieces.size() == 4 && hamlet.reach < city.reach);
	const Torus t(64, 64);
	const std::vector<unsigned char> all(t.size(), 1);
	// A quarter turn: facing 1's footprints are facing 0's turned about the site.
	const std::vector<unsigned char> f0 = baseFootprints(t, city, {{32, 32, 0}});
	const std::vector<unsigned char> f1 = baseFootprints(t, city, {{32, 32, 1}});
	// Three 4x4 footprints (swarm, barracks, racetrack) and seven 2x2 (three inns, hospital,
	// school, two towers).
	assert(std::count(f0.begin(), f0.end(), 1) == 3 * 16 + 7 * 4);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			assert(f0[t.at(x, y)] == f1[t.at(32 - (y - 32), 32 + (x - 32))]);
	for (int facing = 0; facing < 4; ++facing)
		assert(basePlanFits(t, city, {32, 32, facing}, all, all));
	std::vector<unsigned char> blocked = all, ringBlocked = all;
	blocked[t.at(32, 32)] = 0;     // a swarm tile
	ringBlocked[t.at(34, 32)] = 0; // the tile right of the swarm: its walking ring
	assert(!basePlanFits(t, city, {32, 32, 0}, blocked, all));
	assert(!basePlanFits(t, city, {32, 32, 0}, all, ringBlocked));
	const std::vector<unsigned char> around = baseSurroundings(t, city, {{32, 32, 0}});
	assert(around[t.at(34, 32)] && around[t.at(32, 32)] && !around[t.at(32, 32 + 12)]);
	{
		// A finished city on grass: every building, its stock, the garrison, the lists, the start.
		Game game(nullptr);
		grassMap(game, 6, 6);
		GenerationRequest request;
		request.seed = 5;
		request.wDec = request.hDec = 6;
		request.nbTeams = 1;
		request.nbWorkers = 4;
		GenerationContext context(request);
		game.addTeam();
		assert(raiseBase(game, context, 0, city, {32, 32, 2}, large, nullptr, "bases"));
		const Team &team = *game.teams[0];
		int counts[16] = {}, wheat = 0, bullets = 0;
		for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
			if (const Building *b = team.myBuildings[slot])
			{
				assert(!b->type->isBuildingSite);
				++counts[b->type->shortTypeNum];
				wheat += b->resources[WHEAT];
				bullets += b->bullets;
			}
		assert(counts[IntBuildingType::SWARM_BUILDING] == 1 &&
			   counts[IntBuildingType::FOOD_BUILDING] == 3 &&
			   counts[IntBuildingType::HEAL_BUILDING] == 1 &&
			   counts[IntBuildingType::SCIENCE_BUILDING] == 1 &&
			   counts[IntBuildingType::ATTACK_BUILDING] == 1 &&
			   counts[IntBuildingType::WALKSPEED_BUILDING] == 1 &&
			   counts[IntBuildingType::DEFENSE_BUILDING] == 2);
		assert(wheat == 20 + 30 + 30 + 10 && bullets == 2 * 12);
		assert(team.swarms.size() == 1 && team.turrets.size() == 2);
		int workers = 0, warriors = 0, explorers = 0;
		for (int slot = 0; slot < Unit::MAX_COUNT; ++slot)
			if (const Unit *unit = team.myUnits[slot])
				(unit->typeNum == WORKER ? workers : unit->typeNum == WARRIOR ? warriors : explorers)++;
		assert(workers == 32 && warriors == 12 && explorers == 4);
		// At facing 2 the swarm's frame corners (-2,-2)..(1,1) turn to (2,2)..(-1,-1), so its
		// top-left lands one tile up and left of the site.
		assert(team.startPosSet == Team::START_POS_FROM_SWARM && team.startPosX == 32 - 1 &&
			   team.startPosY == 32 - 1 && context.bootX[0] == team.startPosX &&
			   context.bootY[0] == team.startPosY);
		assert(validateBase(game, t, 0, city, {32, 32, 2}, 32).empty());
		assert(!validateBase(game, t, 0, city, {32, 32, 2}, 31).empty());
		assert(!validateBase(game, t, 0, city, {33, 32, 2}, 32).empty());
		assert(!validateBase(game, t, 0, city, {32, 32, 0}, 32).empty());
		// The structural check counts the colony's workers against the lobby's value unless the
		// definition owns the count.
		GeneratorDefinition definition{"test-base", 998, "Test base", 1, false, {}, nullptr, true};
		assert(validateGeneratedWorld(game, request, definition) == "Incomplete starting colony");
		definition.startingWorkers = [](const GenerationRequest &) { return 32; };
		assert(validateGeneratedWorld(game, request, definition).empty());
		plantBaseDepots(game.map, context, t, city, {32, 32, 2});
		assert(countResource(game.map, STONE) > 0);
	}
	{
		// A city of sites: the swarm and one inn finished, eight level-0 sites, wood stacked beside.
		Game game(nullptr);
		grassMap(game, 6, 6);
		GenerationRequest request;
		request.seed = 6;
		request.wDec = request.hDec = 6;
		request.nbTeams = 1;
		GenerationContext context(request);
		game.addTeam();
		const BasePlan sites = standardBasePlan(BaseTier::City, BaseKind::Sites, 0, false);
		assert(sites.pieces.size() == 10 && sites.depots.size() == 4);
		assert(raiseBase(game, context, 0, sites, {20, 40, 3}, baseGarrison(24, false), nullptr, "bases"));
		int finished = 0, unfinished = 0;
		for (int slot = 0; slot < Building::MAX_COUNT; ++slot)
			if (const Building *b = game.teams[0]->myBuildings[slot])
			{
				if (b->type->isBuildingSite)
				{
					assert(b->type->level == 0 && b->hp == 1);
					++unfinished;
				}
				else
					++finished;
			}
		assert(finished == 2 && unfinished == 8);
		plantBaseDepots(game.map, context, t, sites, {20, 40, 3});
		assert(countResource(game.map, WOOD) >= 4);
		assert(validateBase(game, t, 0, sites, {20, 40, 3}, 24).empty());
	}
	{
		// A square compound: interior, wall and two gates; the walls' standing proof.
		CompoundMasks masks(t.size());
		stampCompound(t, {32, 32, 0}, 5, 2, 3, 0, masks);
		int interior = 0, wall = 0, gate = 0;
		for (int i = 0; i < t.size(); ++i)
		{
			interior += masks.interiorOf[i] == 0;
			wall += masks.wall[i];
			gate += masks.gate[i];
		}
		assert(interior == 81 && wall == 34 && gate == 6);
		assert(masks.gate[t.at(37, 32)] && masks.gate[t.at(27, 33)] && masks.wall[t.at(32, 37)]);
		assert(compoundsApart(t, {32, 32, 0}, {45, 32, 0}, 5, 2) && !compoundsApart(t, {32, 32, 0}, {44, 32, 0}, 5, 2));
		Game game(nullptr);
		grassMap(game, 6, 6);
		assert(!wallStanding(game.map, t, masks.wall, masks.gate, "test wall").empty());
		for (int i = 0; i < t.size(); ++i)
			if (masks.wall[i])
				game.map.setResource(i % t.w, i / t.w, STONE, 1);
		assert(wallStanding(game.map, t, masks.wall, masks.gate, "test wall").empty());
		game.map.setResource(37, 32, STONE, 1);
		assert(!wallStanding(game.map, t, masks.wall, masks.gate, "test wall").empty());
	}
	{
		// Lanes about 12 apart on a 64x32 torus: five columns, three rows, spread by whole tiles.
		const Torus small(64, 32);
		const LaneGrid grid = layLanes(small, 12, 3, 5);
		assert(grid.columns() == 5 && grid.rows() == 3);
		const std::vector<int> expectedX{3, 15, 28, 41, 54}, expectedY{5, 15, 26};
		assert(grid.laneX == expectedX && grid.laneY == expectedY);
		assert(grid.column(20) == 1 && grid.column(1) == 4 && grid.row(30) == 2 && grid.row(5) == 0);
		assert(grid.columnSpan(0).start == 4 && grid.columnSpan(0).count == 11);
		assert(grid.columnSpan(4).start == 55 && grid.columnSpan(4).count == 12);
		const std::vector<unsigned char> lanes = laneTiles(grid);
		assert(std::count(lanes.begin(), lanes.end(), 1) == 5 * 32 + 3 * 64 - 15);
		TerrainSketch sketch(small.size(), GRASS);
		Farm pads;
		pads.water.assign(small.size(), 0);
		pads.sand.assign(small.size(), 0);
		pads.plot.assign(small.size(), 0);
		pads.row.assign(small.size(), -1);
		int x0 = -1, y0 = -1;
		assert(stampLotPad(sketch, grid, pads, 1, 1, {6, 4, 2}, x0, y0) == 6 && x0 == 18 && y0 == 17);
		assert(std::count(pads.plot.begin(), pads.plot.end(), 1) == 36);
		assert(sketch[small.at(17, 16)] == SAND && sketch[small.at(18, 17)] == GRASS &&
			   sketch[small.at(25, 24)] == SAND);
		assert(stampLotPad(sketch, grid, pads, 2, 1, {6, 4, 2}, x0, y0, 2) == 4);
		assert(stampLotPad(sketch, grid, pads, 3, 1, {6}, x0, y0, 3) == 0);
	}
	{
		// Routes across the wrap: midpoints, neighbours, stepping stones and headings.
		const ShapePoint a{60, 10}, b{4, 10}, c{32, 40};
		const ShapePoint middle = midpointAcross(t, a, b);
		assert(middle.x == 0 && middle.y == 10 && siteDistance(t, a, b) == 8);
		const auto pairs = nearestPairs(t, {a, b, c}, 1);
		const std::pair<int, int> ab(0, 1), ac(0, 2);
		assert(pairs.size() == 2 && pairs[0] == ab && pairs[1] == ac);
		const auto stones = waypointsAlong(t, a, b, 3, 1, 1);
		assert(stones.size() == 3 && stones[0].x == 61 && stones[1].x == 0 && stones[2].x == 3);
		assert(waypointsAlong(t, a, b, 3, 5, 5).empty());
		assert(quarterTurn(headingAcross(t, a, b)) == 0 && quarterTurn(headingAcross(t, b, a)) == 2);
		assert(quarterTurn(headingAcross(t, {10, 10}, {10, 20})) == 1 &&
			   quarterTurn(headingAcross(t, {10, 20}, {10, 10})) == 3);
	}
}

inline void toolkitChecks()
{
	boundaryExclusionChecks();
	floodChecks();
	baseChecks();
	sketchChecks();
	plantingChecks();
	roadChecks();
	settlementChecks();
	balancedStartChecks();
	scatterChecks();
	noiseChecks();
	wedgeChecks();
	shuffleChecks();
	drawingChecks();
	branchChecks();
	stretchChecks();
	dressingChecks();
	layerChecks();
	wallChecks();
	territoryChecks();
	arenaChecks();
	farmAndTowerChecks();
	subtileRasterChecks();
	tessellationChecks();
	graphMazeChecks();
	shortcutChecks();
	cellCrossingChecks();
	partitionChecks();
	gatePartitionChecks();
	puts("PASS shared toolkit: floods, sketch, planting, roads, settlements, balanced starts, "
	puts("PASS shared toolkit: floods, premade bases, compounds, lanes and lots, routes, sketch, "
		 "planting, roads, settlements, balanced starts, "
		 "scatter, lattice noise, wedge frame, shuffle, drawing, branches, stretch, sand patches, "
		 "algae growth, fields and clumps, walls and tower reach, territories, arena primitives, "
		 "sealed lines and polygons, tessellations and warp, graph mazes, shortcuts, cell "
		 "crossings, region labels and gate partitions");
}
} // namespace ToolkitChecks
