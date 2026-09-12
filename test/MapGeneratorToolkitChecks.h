// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Contracts of the shared generator toolkit under src/map/generator/shared, each checked on a
// small map built by hand rather than through a generator, so a change to one module fails
// here before it shows up as a changed golden fingerprint somewhere downstream. Needs the
// globals loaded: building and resource types.
#include "BalancedStarts.h"
#include "Drawing.h"
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
	assert(growPatch(map, t, t.at(20, 20), CORN, 12, anywhere) == 12);
	assert(countResource(map, CORN) == 12 && !clearGround(map, 20, 20));
	std::vector<unsigned char> corn(t.size(), 0), seed(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		corn[i] = map.getResource(i % t.w, i / t.w).type == CORN;
	seed[t.at(20, 20)] = 1;
	// Four-connected: a cardinal flood over the patch from its seed reaches every tile of it.
	std::vector<int> reached{t.at(20, 20)};
	std::vector<unsigned char> seen(t.size(), 0);
	seen[t.at(20, 20)] = 1;
	for (size_t head = 0; head < reached.size(); ++head)
		for (const auto &step : kCardinalSteps)
		{
			const int n = t.at(reached[head] % t.w + step[0], reached[head] / t.w + step[1]);
			if (corn[n] && !seen[n])
			{
				seen[n] = 1;
				reached.push_back(n);
			}
		}
	assert(reached.size() == 12);
	const auto clear = [&](int i) { return clearGround(map, i % t.w, i / t.w); };
	const int near = seedNear(t, 20, 20, 6, clear);
	assert(near >= 0 && clear(near));
	for (int dy = -6; dy <= 6; ++dy)
		for (int dx = -6; dx <= 6; ++dx)
			if (clear(t.at(20 + dx, 20 + dy)))
				assert(dx * dx + dy * dy >= t.dist2(20, 20, near % t.w, near / t.w));
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
		growPatch(map, t, t.at(12, 26), CORN, 10, [](int) { return true; });
		growPatch(map, t, t.at(44, 26), CORN, 10, [](int) { return true; });
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
		assert(nearest(CORN) > 0 && nearest(CORN) <= 24 && nearest(WOOD) > 0 &&
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
		pocket.setResource(28, 20, CORN, 1);
		pocket.setResource(12, 20, WOOD, 1);
		guaranteeStartingResources(walled, context, 24, 32);
		assert(countResource(pocket, STONE) < 24 && countResource(pocket, CORN) == 1 &&
			   countResource(pocket, WOOD) == 1);
		// The same pocket with the ring protected: the stone stays and crops are placed inside.
		Game keep(nullptr);
		grassMap(keep, 6, 6);
		Map &designed = keep.map;
		for (int i = 0; i < t.size(); ++i)
			if (ring[i])
				designed.setResource(i % t.w, i / t.w, STONE, 1);
		designed.setResource(28, 20, CORN, 1);
		designed.setResource(12, 20, WOOD, 1);
		guaranteeStartingResources(keep, context, 24, 32, 0, &ring);
		assert(countResource(designed, STONE) == 24 && countResource(designed, CORN) > 1 &&
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
			const Flood flood = floodFrom(t, tileMask(t, unitTilesByTeam(map, 1)[0]),
										  groundUnitTiles(map), 24);
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
	growPatch(map, t, t.at(16, 16), CORN, 12, anywhere);
	growPatch(map, t, t.at(22, 16), WOOD, 12, anywhere);
	growPatch(map, t, t.at(48, 48), CORN, 12, anywhere);
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
		assert(context.bootX[team] == again.bootX[team] && context.bootY[team] == again.bootY[team]);
		assert(map.isFreeForBuilding(context.bootX[team], context.bootY[team], 4, 4));
	}
	assert(t.dist2(context.bootX[0], context.bootY[0], context.bootX[1], context.bootY[1]) >=
		   20 * 20);
	// No wood anywhere: no balanced set exists, and the boot tiles are left alone.
	Game bare(nullptr);
	grassMap(bare, 6, 6);
	growPatch(bare.map, t, t.at(16, 16), CORN, 12, anywhere);
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
	int corn[2] = {0, 0}, wood[2] = {0, 0}, stone[2] = {0, 0};
	for (int y = 0; y < game.map.getH(); ++y)
		for (int x = 0; x < game.map.getW(); ++x)
		{
			const int type = game.map.getResource(x, y).type;
			if (type == NO_RES_TYPE)
				continue;
			assert(!game.map.isWater(x, y));
			const int island = x < 30 ? 0 : 1;
			corn[island] += type == CORN;
			wood[island] += type == WOOD;
			stone[island] += type == STONE;
		}
	for (int island = 0; island < 2; ++island)
		assert(corn[island] > 0 && wood[island] > 0 && stone[island] > 0);
	assert(corn[0] + corn[1] > wood[0] + wood[1]);
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
	assert(order == same && order != std::vector<int>({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
													  13, 14, 15, 16, 17, 18, 19}));
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
	assert(countResource(map, CORN) == 6 && countResource(map, WOOD) == 3);
	assert(map.getResource(8, 5).type == CORN && map.getResource(0, 5).type == WOOD);
	assert(map.getResource(9, 5).type == NO_RES_TYPE);

	const KitFrame east{10, 10, 0}, south{10, 10, kPi / 2};
	assert(east.at(4, 1, 3).x == 14 && east.at(4, 1, 3).y == 11 && east.at(4, 1, 3).within == 3);
	assert(south.at(4, 1, 3).x == 9 && south.at(4, 1, 3).y == 14);

	GenerationRequest request;
	request.seed = 3;
	GenerationContext context(request);
	std::vector<int> ground{t.at(40, 40), t.at(50, 50)};
	int calls = 0;
	assert(scatterClumps(context, t, ground, 4, "clumps", [&](int i) { return i == ground[1]; },
						 [&](MapGeneratorPoint p)
						 {
							 assert(p.x == 50 && p.y == 50);
							 ++calls;
						 }) == 4 &&
		   calls == 4);
	assert(scatterClumps(context, t, ground, 2, "clumps", [](int) { return false; },
						 [](MapGeneratorPoint) { assert(false); }) == 0);
	assert(scatterClumps(context, t, {}, 2, "clumps", [](int) { return true; },
						 [](MapGeneratorPoint) { assert(false); }) == 0);
}

inline void toolkitChecks()
{
	floodChecks();
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
	layerChecks();
	puts("PASS shared toolkit: floods, sketch, planting, roads, settlements, balanced starts, "
		 "scatter, lattice noise, wedge frame, shuffle, drawing, fields and clumps");
}
} // namespace ToolkitChecks
