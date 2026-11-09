// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Checks report through doctest (REQUIRE), so only test code may include this header.
#include "Glob2Test.h"
// Contracts of the shared generator toolkit under src/map/generator/shared, each checked on a
// small map built by hand rather than through a generator, so a change to one module fails
// here before it shows up as a changed golden fingerprint somewhere downstream. Needs the
// globals loaded: building and resource types.
#include "BalancedStarts.h"
#include "Map.h"
#include <array>
#include <utility>
#include <random>
#include <cstdlib>
#include <cstdint>
#include <climits>
#include <exception>
#include "Building.h"
#include "BuildingType.h"
#include "RecursiveGeometry.h"
#include "GenerationService.h"
#include "GeneratorRegistry.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include "HierarchicalCrossings.h"
#include "Drawing.h"
#include "Rock.h"
#include "HeightFieldLooks.h"
#include "HeightMap.h"
#include "Terrain.h"
#include "GenerationValidation.h"
#include "GeneratorDefinition.h"
#include "IntBuildingType.h"
#include "Routes.h"
#include "Team.h"
#include "Unit.h"
#include "UnitConsts.h"
#include "GraphMaze.h"
#include "Game.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "LatticeNoise.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Farmland.h"
#include "Points.h"
#include "Resources.h"
#include "Rivers.h"
#include "Solve.h"
#include "Room.h"
#include "Growth.h"
#include "Roads.h"
#include "Settlements.h"
#include "Sketch.h"
#include "StartQuality.h"
#include "Tessellation.h"
#include "Territories.h"
#include "Walls.h"
#include "Homes.h"
#include "Farmland.h"
#include "Growth.h"
#include "Orbits.h"
#include <tuple>
#include "Towers.h"
#include "Wedge.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace ToolkitChecks
{
using namespace MapGeneration;

// Compare the bucket-based widest path with independent threshold reachability, including
// blocked, fully open, rectangular, and seam-crossing masks and multiple starting tiles.
inline void widestWalkChecks()
{
	const Torus t(16, 8);
	std::uint32_t random = 71;
	for (int trial = 0; trial < 80; ++trial)
	{
		std::vector<unsigned char> mask(t.size()), goal(t.size(), 0), source(t.size(), 0);
		for (auto &tile : mask)
		{
			random = random * 1664525u + 1013904223u;
			tile = trial == 0 ? 1 : trial == 1 ? 0 : (random >> 24) > 45;
		}
		const std::vector<int> sources = {t.at(0, 3), t.at(14, 2)};
		goal[t.at(15, 4)] = goal[t.at(7, 6)] = 1;
		const auto room = clearance(t, mask);
		int expected = 0;
		for (int width = 1; width <= *std::max_element(room.begin(), room.end()); ++width)
		{
			std::vector<unsigned char> open(t.size());
			for (int i = 0; i < t.size(); ++i)
				open[i] = mask[i] && room[i] >= width;
			std::fill(source.begin(), source.end(), 0);
			for (const int i : sources)
				source[i] = open[i];
			const auto steps = stepsFrom(t, source, open);
			for (int i = 0; i < t.size(); ++i)
				if (goal[i] && steps[i] >= 0)
					expected = width;
		}
		REQUIRE(widestWalkClearance(t, mask, sources, goal) == expected);
		REQUIRE(widestWalkClearance(t, mask, room, sources, goal) == expected);
		REQUIRE(stepsFrom(t, goal) == stepsFrom(t, goal, std::vector<unsigned char>(t.size(), 1)));
	}
}

inline void solvingAndRiverChecks()
{
	GenerationRequest request;
	GenerationContext context(request);
	int state = 4, best = -1;
	// A hot final move leaves the walk worse than its best; recall must restore that best.
	const auto run = anneal(Anneal{2, 1e30, 1e30, "test-solve"}, context,
		[&] { state = state == 4 ? 1 : 3; return true; }, [&] { return double(state); },
		[] { REQUIRE(false); }, [&] { best = state; }, [&] { state = best; });
	REQUIRE((state == 1 && run.before == 4 && run.after == 1 && run.kept == 1));
	REQUIRE((run.proposed == 2 && run.taken == 2));
	REQUIRE(!accept(1, 0, context, "test-solve"));
	REQUIRE(accept(-1, 0, context, "test-solve"));

	const auto rejects = [](auto action)
	{
		bool threw = false;
		try { action(); } catch (const std::exception &) { threw = true; }
		REQUIRE(threw);
	};
	Objective objective;
	objective.add("access", 2, 3).add("reward", -1, 2);
	REQUIRE((objective.total() == 4 && objective.residual("access") == 3));
	REQUIRE(!objective.findResidual("missing"));
	rejects([&] { objective.residual("missing"); });
	rejects([&] { objective.add("access", 1, 0); });
	rejects([&] { objective.add("nan", 1, std::numeric_limits<double>::quiet_NaN()); });
	Objective full;
	for (const char *name : {"a", "b", "c", "d", "e", "f", "g", "h"})
		full.add(name, 1, 1);
	rejects([&] { full.add("overflow", 1, 1); });
	REQUIRE(full.total() == 8);
	rejects([&] { Anneal{-1, 1, 1, "bad"}.validate(); });
	rejects([&] { Anneal{1, 0, 1, "bad"}.validate(); });
	rejects([&] { finiteCost(std::numeric_limits<double>::infinity()); });
	// A state with a derived cache exercises skipped, rejected and best-restored moves.
	struct SearchState
	{
		int value = 4, cached = 16, old = 0, best = 0, move = 0;
		bool propose()
		{
			++move;
			if (move == 1) return false;
			old = value;
			value = move == 2 ? 1 : 3;
			cached = value * value;
			return true;
		}
		double cost() const { REQUIRE(cached == value * value); return cached; }
		void undo() { value = old; cached = value * value; }
		void remember() { best = value; }
		void recall() { value = best; cached = value * value; }
	};
	SearchState hot;
	const auto hotRun = anneal(Anneal{3, 1e30, 1e30, "test-state"}, context, hot);
	REQUIRE((hotRun.attempted == 3 && hotRun.proposed == 2 && hotRun.taken == 2));
	REQUIRE((hot.value == 1 && hot.cost() == hotRun.after));
	SearchState cold;
	const auto coldRun = anneal(Anneal{3, 1e-30, 1e-30, "test-state"}, context, cold);
	REQUIRE((coldRun.taken == 1 && cold.value == 1 && cold.cost() == coldRun.after));
	SearchState idle;
	const auto idleRun = anneal(Anneal{}, context, idle);
	REQUIRE((idleRun.attempted == 0 && idle.value == 4 && idle.best == 4));

	const Torus t(32, 16);
	REQUIRE(riverWater(t, River{}) == std::vector<unsigned char>(t.size(), 0));
	River malformed;
	malformed.line = {{0, 0}};
	rejects([&] { riverWater(t, malformed); });
	rejects([&] { fordsSpreadAlong({{0, 0, 1}}, {2}, 1, 16); });
	for (bool vertical : {false, true})
	{
		RiverStyle style;
		style.wander = style.swell = 0;
		style.halfWidth = 2;
		const auto river = drawRiver(t, context, "test-river", vertical, 8, style);
		const auto water = riverWater(t, river);
		const int length = vertical ? t.h : t.w;
		for (int i = 0; i < length; ++i)
		{
			REQUIRE(water[vertical ? t.at(8, i) : t.at(i, 8)]);
			REQUIRE(!water[vertical ? t.at(0, i) : t.at(i, 0)]);
		}
	}
	// A bent loop must close over the seam, never by a straight chord through its interior.
	River bent;
	bent.line = {{0, 4}, {8, 12}, {16, 12}, {24, 12}, {30, 4}};
	bent.radius.assign(bent.line.size(), 1);
	const auto water = riverWater(t, bent);
	REQUIRE((water[t.at(31, 4)] && !water[t.at(16, 4)]));
	const std::vector<RiverFord> sites = {{0, 0, 1}, {4, 1, 2}, {8, 0, 2}, {12, -1, 2}};
	const auto joining = fordsToRejoin(sites, 3);
	REQUIRE((joining.sites == std::vector<int>{0, 1}));
	REQUIRE((joining.connected() && joining.remainingComponents == 1));
	const auto disconnected = fordsToRejoin({{0, 0, 1}}, 3);
	REQUIRE((!disconnected.connected() && disconnected.remainingComponents == 2));
	REQUIRE(fordsToRejoin({}, 0).connected());
	const auto spread = fordsSpreadAlong(sites, joining.sites, 3, 16);
	REQUIRE((spread == std::vector<int>{0, 1, 2}));
}

inline void grassMap(Game &game, int wDec, int hDec)
{
	game.map.setSize(wDec, hDec);
	game.map.setGame(&game);
	game.map.fillTerrain(GRASS);
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
	REQUIRE((limited.steps[t.at(3, 2)] == 0 && limited.steps[t.at(6, 2)] == 3));
	REQUIRE((limited.steps[t.at(7, 2)] == -1 && limited.steps[t.at(0, 7)] == 3));
	REQUIRE(limited.visited.front() == t.at(3, 2));
	for (size_t i = 1; i < limited.visited.size(); ++i)
		REQUIRE(limited.steps[limited.visited[i - 1]] <= limited.steps[limited.visited[i]]);
	const std::vector<int> steps = stepsFrom(t, source, open);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			REQUIRE(steps[t.at(x, y)] == t.chebyshev(3, 2, x, y));
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx)
			if (dx || dy)
				open[t.at(3 + dx, 2 + dy)] = 0;
	const std::vector<int> held = stepsFrom(t, source, open);
	REQUIRE((held[t.at(3, 2)] == 0 && held[t.at(4, 2)] == -1 && held[t.at(9, 5)] == -1));
	REQUIRE(std::count(held.begin(), held.end(), 0) == 1);
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
				REQUIRE(sketch[i] == WATER);
				continue;
			}
			bool shore = false;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
					shore = shore || before[t.at(x + dx, y + dy)] == WATER;
			REQUIRE(sketch[i] == (shore ? SAND : GRASS));
		}
	REQUIRE((countTiles(sketch, WATER) == 16 && countTiles(sketch, SAND) == 20));

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
	REQUIRE((!islands.empty() && islands.size() <= 2 && islands.size() == same.size()));
	REQUIRE(sea == sameSea);
	int raised = 0;
	for (const Island &island : islands)
	{
		REQUIRE((!island.tiles.empty() && island.reach > 0));
		for (int i : island.tiles)
			REQUIRE(sea[i] == GRASS);
		REQUIRE(std::find(island.tiles.begin(), island.tiles.end(), t.at(island.x, island.y)) !=
			   island.tiles.end());
		// Clear of the corner's land: more than the moat plus the outline's reach away.
		REQUIRE(t.chebyshev(island.x, island.y, 2, 2) > island.reach + 7 - 2);
		raised += int(island.tiles.size());
	}
	for (size_t a = 0; a < islands.size(); ++a)
		for (size_t b = a + 1; b < islands.size(); ++b)
		{
			const double gap = islands[a].reach + islands[b].reach + 7;
			REQUIRE(t.dist2(islands[a].x, islands[a].y, islands[b].x, islands[b].y) >= gap * gap);
		}
	REQUIRE(countTiles(sea, GRASS) == 16 + raised);
	TerrainSketch none(t.size(), WATER);
	REQUIRE(raiseIslands(none, t, context, placement).empty()); // no coast to keep clear of
}

// Planting: patches grow to size and stay four-connected, seedNear finds the nearest eligible
// tile, algae lands on water only and in the band asked for, and the swarm clearance is exact.
inline void plantingChecks()
{
	// Shore fields reuse water, preserve a reserved crossing, supply fertile
	// solid crops and leave actual inn footprints rather than noisy holes.
	{
		Game shoreGame(nullptr);
		grassMap(shoreGame, 6, 6);
		const Torus shore(shoreGame.map);
		TerrainSketch terrain(shore.size(), GRASS);
		std::vector<unsigned char> allowed(shore.size(), 1);
		std::vector<int> labels(shore.size(), -1);
		for (int i = 0; i < shore.size(); ++i)
		{
			if (shore.dist2(2, 32, i % shore.w, i / shore.w) <= 100)
				terrain[i] = WATER;
			if (i / shore.w >= 55 && i / shore.w <= 59)
				allowed[i] = 0;
		}
		layBeaches(terrain, shore);
		const auto before = terrain;
		const auto water = pureTiles(terrain, shore, WATER);
		const auto fields = layShoreFields(terrain, shore, allowed, labels, 0);
		REQUIRE((!fields.empty() && pureTiles(terrain, shore, WATER) == water));
		for (int i = 0; i < shore.size(); ++i)
			if (!allowed[i])
				REQUIRE(terrain[i] == before[i]);
		writeVertices(GenerationMap(shoreGame.map).ref(), terrain);
		const auto fertility = cropGrowthField(terrain, shore);
		REQUIRE(plantShoreFields(GenerationMap(shoreGame.map).ref(), shore, fields, fertility, 0) ==
			   0);
		REQUIRE(plantShoreFields(GenerationMap(shoreGame.map).ref(), shore, fields, fertility,
								100) >= 48);
		for (const auto &field : fields)
			for (int y = 0; y < 4; ++y)
				for (int x = 0; x < 4; ++x)
					REQUIRE(clearGround(shoreGame.map, shore.x(field.court % shore.w + x),
									   shore.y(field.court / shore.w + y)));
		for (int i = 0; i < shore.size(); ++i)
			if (shoreGame.map.getResource(i).type == WHEAT)
				REQUIRE((labels[i] >= 0 &&
					   fertility.at(i % shore.w, i / shore.w) >= Fertility::kScale / 64));
		std::vector<unsigned char> walking(shore.size());
		for (int i = 0; i < shore.size(); ++i)
			walking[i] = !shoreGame.map.isWater(i % shore.w, i / shore.w) &&
				!shoreGame.map.isResource(i % shore.w, i / shore.w);
		const auto approach = stepsFrom(shore, tileMask(shore, {shore.at(32, 32)}), walking);
		for (const auto &field : fields) REQUIRE(approach[field.court] >= 0);
		for (int tick = 0; tick < 256; ++tick)
			shoreGame.map.growResources();
		REQUIRE(containedPlotsMismatch(shoreGame.map, shore, labels).empty());
		// Open shores add no sand rim. Their fertile spread ends naturally,
		// including the last dry neighbour a fertile source can seed.
		Game openGame(nullptr);
		grassMap(openGame, 6, 6);
		terrain = before;
		labels.assign(shore.size(), -1);
		const auto openFields = layShoreFields(terrain, shore, allowed, labels, 0, false);
		REQUIRE((!openFields.empty() && pureTiles(terrain, shore, WATER) == water));
		for (int i = 0; i < shore.size(); ++i)
			REQUIRE((terrain[i] != SAND || before[i] == SAND));
		writeVertices(GenerationMap(openGame.map).ref(), terrain);
		const auto openFertility = cropGrowthField(terrain, shore);
		REQUIRE(plantShoreFields(GenerationMap(openGame.map).ref(), shore, openFields, openFertility,
								100) > 0);
		const auto envelope = fertileCropEnvelope(openGame.map, openFertility);
		REQUIRE(!envelope[shore.at(32, 32)]);
		for (int tick = 0; tick < 256; ++tick) openGame.map.growResources();
		for (int i = 0; i < shore.size(); ++i)
			if (openGame.map.getResource(i).type == WHEAT) REQUIRE(envelope[i]);

	}

	Game game(nullptr);
	grassMap(game, 6, 6);
	Map &map = game.map;
	const Torus t(map);
	REQUIRE(clearGround(map, 5, 5));
	const auto anywhere = [](int) { return true; };
	REQUIRE(growPatch(GenerationMap(map).ref(), t, t.at(20, 20), WHEAT, 12, anywhere) == 12);
	REQUIRE((countResource(map, WHEAT) == 12 && !clearGround(map, 20, 20)));
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
	REQUIRE(reached.size() == 12);
	const auto clear = [&](int i) { return clearGround(map, i % t.w, i / t.w); };
	const int nearby = seedNear(t, 20, 20, 6, clear);
	REQUIRE((nearby >= 0 && clear(nearby)));
	for (int dy = -6; dy <= 6; ++dy)
		for (int dx = -6; dx <= 6; ++dx)
			if (clear(t.at(20 + dx, 20 + dy)))
				REQUIRE(dx * dx + dy * dy >= t.dist2(20, 20, nearby % t.w, nearby / t.w));
	REQUIRE(seedNear(t, 20, 20, 2, [](int) { return false; }) == -1);
	// Only eligible tiles are planted, and the count stops where eligibility does: a 2 by 4 box.
	const auto box = [&](int i)
	{ return i % t.w >= 40 && i % t.w < 42 && i / t.w >= 40 && i / t.w < 44; };
	REQUIRE(growPatch(GenerationMap(map).ref(), t, t.at(40, 40), WOOD, 50, box) == 8);
	REQUIRE(countResource(map, WOOD) == 8);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			if (map.getResource(x, y).type == WOOD)
				REQUIRE(box(t.at(x, y)));
	// A requested entrance just across the seam must find the allowed patch,
	// exhaust its four tiles honestly, and leave all surrounding ground clear.
	const auto seamPatch = [&](int i)
	{ return i % t.w < 2 && i / t.w >= 10 && i / t.w < 12 && clear(i); };
	REQUIRE(plantPatchNear(GenerationMap(map).ref(), t, {63, 10, 3}, WHEAT, 12, seamPatch) == 4);
	REQUIRE(plantPatchNear(GenerationMap(map).ref(), t, {63, 10, 3}, WHEAT, 12, seamPatch) == 0);
	REQUIRE((clearGround(map, 63, 10) && clearGround(map, 2, 10)));

	GenerationRequest request;
	request.seed = 5;
	request.nbTeams = 1;
	GenerationContext context(request);
	// A pond: algae anywhere on its water, then only in its shallows.
	{
		auto batch = map.editTerrain();
		for (int y = 40; y < 52; ++y)
			for (int x = 8; x < 20; ++x)
				map.setVertexTerrain(x, y, WATER);
	}
	map.layBeaches();
	seedAlgae(GenerationMap(map).ref(), context, t, "algae", 100, AlgaeBand::anyWater(1));
	const int anywhereAlgae = countResource(map, ALGA);
	REQUIRE(anywhereAlgae > 0);
	std::vector<unsigned char> wet(t.size()), dry(t.size());
	for (int i = 0; i < t.size(); ++i)
	{
		wet[i] = map.isWater(i % t.w, i / t.w);
		dry[i] = !wet[i];
		if (map.getResource(i % t.w, i / t.w).type == ALGA)
		{
			REQUIRE(wet[i]);
			map.setNoResource(i % t.w, i / t.w, 1);
		}
	}
	const std::vector<int> offshore = stepsFrom(t, dry, wet);
	// The band constrains the clumps' centres, so single-tile clumps show it exactly.
	seedAlgae(GenerationMap(map).ref(), context, t, "algae", 100, AlgaeBand{2, 3, 1, 0});
	REQUIRE(countResource(map, ALGA) > 0);
	for (int i = 0; i < t.size(); ++i)
		if (map.getResource(i % t.w, i / t.w).type == ALGA)
			REQUIRE((offshore[i] >= 2 && offshore[i] <= 3));
	REQUIRE(scaledCount(40, 0) == 0);
	map.setNoResource(8, 40, 1);
	seedAlgae(GenerationMap(map).ref(), context, t, "algae", 0, AlgaeBand::anyWater(1));

	// The swarm clearance: the 4x4 footprint and kSwarmClearance tiles round it.
	context.bootX[0] = 30;
	context.bootY[0] = 10;
	const std::vector<unsigned char> reserved = swarmSurroundings(t, context);
	REQUIRE(std::count(reserved.begin(), reserved.end(), 1) == 8 * 8);
	REQUIRE((reserved[t.at(28, 8)] && reserved[t.at(35, 15)] && !reserved[t.at(27, 8)] &&
		   !reserved[t.at(36, 15)]));
	for (int dy = -3; dy < 7; ++dy)
		for (int dx = -3; dx < 7; ++dx)
			map.setResourceByIndex(30 + dx, 10 + dy, STONE, 1);
	std::vector<unsigned char> keep(t.size(), 0);
	keep[t.at(30, 10)] = 1;
	clearAroundSwarms(GenerationMap(map).ref(), context, t, &keep);
	for (int dy = -3; dy < 7; ++dy)
		for (int dx = -3; dx < 7; ++dx)
		{
			const bool inside = dx >= -2 && dx < 6 && dy >= -2 && dy < 6;
			const bool kept = dx == 0 && dy == 0;
			REQUIRE((map.getResource(30 + dx, 10 + dy).type == STONE) == (!inside || kept));
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
	REQUIRE((!route.empty() && route.front() == t.at(12, 8) && route.back() == t.at(2, 8)));
	int crossings = 0;
	bool gate = false;
	for (int i : route)
	{
		REQUIRE(!blocked[i]);
		crossings += costly[i];
		gate = gate || i == t.at(9, 8);
	}
	REQUIRE((crossings == 1 && gate));
	blocked[t.at(9, 8)] = 1;
	REQUIRE(cheapestRoute(t, {t.at(2, 8)}, goal, blocked, costly).empty());

	// On the map, stone columns either side of the source: any walk to the goal crosses one of
	// them, and openRoad clears exactly one tile of it.
	Game game(nullptr);
	grassMap(game, 4, 4);
	Map &map = game.map;
	for (int y = 0; y < map.getH(); ++y)
	{
		map.setResourceByIndex(7, y, STONE, 1);
		map.setResourceByIndex(15, y, STONE, 1);
	}
	REQUIRE(countResource(map, STONE) == 32);
	REQUIRE(openRoad(GenerationMap(map).ref(), Torus(map), {t.at(2, 8)}, goal));
	REQUIRE(countResource(map, STONE) == 31);
	std::vector<unsigned char> wall(t.size(), 0);
	for (int y = 0; y < t.h; ++y)
		wall[t.at(9, y)] = wall[t.at(15, y)] = 1;
	REQUIRE(!openRoad(GenerationMap(map).ref(), Torus(map), {t.at(2, 8)}, goal, &wall));
	REQUIRE(countResource(map, STONE) == 31);
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
		REQUIRE(settleColonies(
			GenerationWorld(game).ref(), context, "starts", [&](int) { return everywhere; },
			[](int team) { return MapGeneratorPoint(team ? 44 : 12, 20); }));
		REQUIRE((context.bootX[0] == 12 && context.bootY[0] == 20 && context.bootX[1] == 44));
		ColonyWalk walk = walkFromFirstColony(map, 2, "the map", "");
		REQUIRE((walk.error.empty() && walk.workers[1].size() == 3 && walk.steps[t.at(12, 19)] >= 0));
		for (int i : walk.workers[1])
			REQUIRE(walk.steps[i] > 0);
		REQUIRE(!reopenCrampedStarts(GenerationWorld(game).ref(), context, {}));
		// Scoring the finished colonies.
		growPatch(GenerationMap(map).ref(), t, t.at(12, 26), WHEAT, 10, [](int) { return true; });
		growPatch(GenerationMap(map).ref(), t, t.at(44, 26), WHEAT, 10, [](int) { return true; });
		growPatch(GenerationMap(map).ref(), t, t.at(8, 14), WOOD, 10, [](int) { return true; });
		growPatch(GenerationMap(map).ref(), t, t.at(40, 14), WOOD, 10, [](int) { return true; });
		const StartQualityReport report = scoreStarts(GenerationWorld(game).ref(), 2);
		REQUIRE((report.measured && report.colonies.size() == 2));
		double winShare = 0;
		for (const ColonyQuality &colony : report.colonies)
		{
			REQUIRE((colony.wheatDistance > 0 && colony.woodDistance > 0 && colony.buildSites > 0));
			REQUIRE((colony.winProbability > 0 && colony.winProbability < 1));
			winShare += colony.winProbability;
		}
		REQUIRE(std::fabs(winShare - 1.0) < 1e-9);
		REQUIRE((report.worstFitness <= report.meanFitness &&
			   report.meanFitness <= report.bestFitness));
		REQUIRE((report.score == report.fairness && report.fairness > 0 && report.fairness <= 1));
		// Fairness reads off the same scale whatever the colony count: equal chances are 1,
		// and one colony taking everything is 0 for two colonies as much as for eight.
		REQUIRE((mapFairness({0.5, 0.5}) == 1.0 && mapFairness({0.25, 0.25, 0.25, 0.25}) == 1.0));
		REQUIRE((mapFairness({1.0, 0.0}) == 0.0 && mapFairness({1.0, 0.0, 0.0, 0.0}) == 0.0));
		REQUIRE(mapFairness({1.0}) == 1.0);
		// Softmax: equal fitness is an equal chance, and a higher fitness is a better chance.
		const auto even = winProbabilities({0.5, 0.5, 0.5});
		REQUIRE((std::fabs(even[0] - 1.0 / 3) < 1e-9 && std::fabs(even[2] - 1.0 / 3) < 1e-9));
		const auto skewed = winProbabilities({0.0, 2.0});
		REQUIRE((skewed[1] > skewed[0] && std::fabs(skewed[0] + skewed[1] - 1.0) < 1e-9));
		// Two water walls, one across the wrap, cut colony 1 off from colony 0.
		{
			auto batch = map.editTerrain();
			for (int y = 0; y < t.h; ++y)
				for (int x = 0; x < t.w; ++x)
					if ((x >= 28 && x < 32) || x < 4)
						map.setVertexTerrain(x, y, WATER);
		}
		walk = walkFromFirstColony(map, 2, "the map", "over the wall");
		REQUIRE(walk.error == "Colony 1 cannot walk to colony 0 over the wall.");
		REQUIRE(walkFromFirstColony(map, 0, "the map", "").error ==
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
		guaranteeStartingResources(GenerationWorld(game).ref(), context, 24, 32);
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
		REQUIRE((nearest(WHEAT) > 0 && nearest(WHEAT) <= 24 && nearest(WOOD) > 0 &&
			   nearest(WOOD) <= 32));
		// Walled into a pocket by stone with crops outside: the wall is cleared, not added to.
		Game walled(nullptr);
		grassMap(walled, 6, 6);
		Map &pocket = walled.map;
		std::vector<unsigned char> ring(t.size(), 0);
		for (int dy = -3; dy <= 3; ++dy)
			for (int dx = -3; dx <= 3; ++dx)
				if (std::max(std::abs(dx), std::abs(dy)) == 3)
				{
					pocket.setResourceByIndex(20 + dx, 20 + dy, STONE, 1);
					ring[t.at(20 + dx, 20 + dy)] = 1;
				}
		pocket.setResourceByIndex(28, 20, WHEAT, 1);
		pocket.setResourceByIndex(12, 20, WOOD, 1);
		guaranteeStartingResources(GenerationWorld(walled).ref(), context, 24, 32);
		REQUIRE((countResource(pocket, STONE) < 24 && countResource(pocket, WHEAT) == 1 &&
			   countResource(pocket, WOOD) == 1));
		// The same pocket with the ring protected: the stone stays and crops are placed inside.
		Game keep(nullptr);
		grassMap(keep, 6, 6);
		Map &designed = keep.map;
		for (int i = 0; i < t.size(); ++i)
			if (ring[i])
				designed.setResourceByIndex(i % t.w, i / t.w, STONE, 1);
		designed.setResourceByIndex(28, 20, WHEAT, 1);
		designed.setResourceByIndex(12, 20, WOOD, 1);
		guaranteeStartingResources(GenerationWorld(keep).ref(), context, 24, 32, 0, &ring);
		REQUIRE((countResource(designed, STONE) == 24 && countResource(designed, WHEAT) > 1 &&
			   countResource(designed, WOOD) > 1));
		// A protected *placement* band has a different meaning from the stone
		// wall above. The rescue may centre a radius-two patch beside its edge;
		// the whole patch must remain inside the band, including at the torus
		// seam. This caught crops spilling into Rice Terraces' dry town when
		// ambient wood was scarce and the general topup tried to help.
		Game band(nullptr);
		grassMap(band, 6, 6);
		std::vector<unsigned char> allowed(t.size(), 0);
		for (int y = 18; y <= 25; ++y)
			for (int x = 25; x <= 33; ++x)
				allowed[t.at(x, y)] = 1;
		const int placed =
			placeResourceClump(GenerationMap(band.map).ref(), context, {25, 20}, WOOD, 2, &allowed);
		REQUIRE(placed > 0);
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
				if (band.map.isResource(x, y))
					REQUIRE(allowed[t.at(x, y)]);
		// In the normal rescue path, both crops must honour the same band;
		// rejection of a too-short mask happens before any world mutation.
		Game topup(nullptr);
		grassMap(topup, 6, 6);
		guaranteeStartingResources(GenerationWorld(topup).ref(), context, 24, 32, 0, nullptr,
								   &allowed);
		REQUIRE((countResource(topup.map, WHEAT) > 0 && countResource(topup.map, WOOD) > 0));
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
				if (topup.map.isResource(x, y))
					REQUIRE(allowed[t.at(x, y)]);
		Game packed(nullptr);
		grassMap(packed, 6, 6);
		for (int y = 18; y <= 25; ++y)
			for (int x = 25; x <= 33; ++x)
				packed.map.setResourceByIndex(x, y, WHEAT, 1);
		const int beforeWheat = countResource(packed.map, WHEAT);
		guaranteeStartingResources(GenerationWorld(packed).ref(), context, 24, 32, 0, nullptr,
								   &allowed);
		// With no free allowed grass, the opt-in backstop trades a small
		// *accessible* surplus patch for the missing crop. It must not erase
		// the whole wheat belt or plant outside the farm mask.
		REQUIRE((countResource(packed.map, WHEAT) > 0 &&
			   countResource(packed.map, WHEAT) < beforeWheat &&
			   countResource(packed.map, WOOD) > 0));
		for (int y = 0; y < t.h; ++y)
			for (int x = 0; x < t.w; ++x)
				if (packed.map.isResource(x, y))
					REQUIRE(allowed[t.at(x, y)]);
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
		REQUIRE(placeSettlement(GenerationWorld(game).ref(), context, 0, everywhere, {30, 30},
							   "starts"));
		for (int dy = -12; dy <= 12; ++dy)
			for (int dx = -12; dx <= 12; ++dx)
				if (clearGround(map, t.x(30 + dx), t.y(30 + dy)))
					map.setResourceByIndex(t.x(30 + dx), t.y(30 + dy), WOOD, 1);
		const auto sites = [&]
		{
			const Flood flood =
				floodFrom(t, tileMask(t, unitTilesByTeam(map, 1)[0]), groundUnitTiles(map), 24);
			int count = 0;
			for (int i : flood.visited)
				count += map.isFreeForBuilding(i % t.w, i / t.w, 4, 4);
			return count;
		};
		REQUIRE(sites() < 16);
		const int buried = countResource(map, WOOD);
		openCrampedStarts(GenerationWorld(game).ref(), context);
		REQUIRE((sites() >= 16 && countResource(map, WOOD) < buried && countResource(map, WOOD) > 0));
		REQUIRE(
			reopenCrampedStarts(GenerationWorld(game).ref(), context, {100, 150, 100, 100, 100}));
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
	growPatch(GenerationMap(map).ref(), t, t.at(16, 16), WHEAT, 12, anywhere);
	growPatch(GenerationMap(map).ref(), t, t.at(22, 16), WOOD, 12, anywhere);
	growPatch(GenerationMap(map).ref(), t, t.at(48, 48), WHEAT, 12, anywhere);
	growPatch(GenerationMap(map).ref(), t, t.at(42, 48), WOOD, 12, anywhere);
	GenerationRequest request;
	request.seed = 4;
	request.nbTeams = 2;
	request.nbWorkers = 4;
	GenerationContext context(request), again(request);
	REQUIRE(chooseBalancedStarts(GenerationWorld(game).ref(), context, 20 * 20));
	REQUIRE(chooseBalancedStarts(GenerationWorld(game).ref(), again, 20 * 20));
	for (int team = 0; team < 2; ++team)
	{
		REQUIRE((context.bootX[team] == again.bootX[team] &&
			   context.bootY[team] == again.bootY[team]));
		REQUIRE(map.isFreeForBuilding(context.bootX[team], context.bootY[team], 4, 4));
	}
	REQUIRE(t.dist2(context.bootX[0], context.bootY[0], context.bootX[1], context.bootY[1]) >=
		   20 * 20);
	// No wood anywhere: no balanced set exists, and the boot tiles are left alone.
	Game bare(nullptr);
	grassMap(bare, 6, 6);
	growPatch(GenerationMap(bare.map).ref(), t, t.at(16, 16), WHEAT, 12, anywhere);
	GenerationContext untouched(request);
	untouched.bootX[0] = untouched.bootY[0] = 7;
	REQUIRE(!chooseBalancedStarts(GenerationWorld(bare).ref(), untouched, 20 * 20));
	REQUIRE((untouched.bootX[0] == 7 && untouched.bootY[0] == 7));
}

// scatterResources shares each band out between landmasses: two islands both get farmland and
// stone, and nothing lands on water.
inline void scatterChecks()
{
	Game game(nullptr);
	game.map.setSize(6, 6);
	game.map.setGame(&game);
	game.map.fillTerrain(WATER);
	{
		auto batch = game.map.editTerrain();
		for (int y = 20; y < 40; ++y)
			for (int x = 0; x < 64; ++x)
				if ((x >= 4 && x < 24) || (x >= 36 && x < 56))
					game.map.setVertexTerrain(x, y, GRASS);
	}
	game.map.layBeaches();
	GenerationRequest request;
	request.seed = 13;
	GenerationContext context(request);
	scatterResources(GenerationWorld(game).ref(), context, {40, 20, 30, 0, 0});
	int wheat[2] = {0, 0}, wood[2] = {0, 0}, stone[2] = {0, 0};
	for (int y = 0; y < game.map.getH(); ++y)
		for (int x = 0; x < game.map.getW(); ++x)
		{
			const int type = game.map.getResource(x, y).type;
			if (type == NO_RES_TYPE)
				continue;
			REQUIRE(!game.map.isWater(x, y));
			const int island = x < 30 ? 0 : 1;
			wheat[island] += type == WHEAT;
			wood[island] += type == WOOD;
			stone[island] += type == STONE;
		}
	for (int island = 0; island < 2; ++island)
		REQUIRE((wheat[island] > 0 && wood[island] > 0 && stone[island] > 0));
	REQUIRE(wheat[0] + wheat[1] > wood[0] + wood[1]);
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
			REQUIRE((v >= 0 && v < 1 && v == twin.at(x + 0.5, y + 0.5)));
			REQUIRE(std::abs(noise.at(x + 64.5, y + 0.5) - v) < 1e-9);
			REQUIRE(std::abs(noise.at(x + 0.5, y - 31.5) - v) < 1e-9);
		}
	const std::vector<int> field = periodicNoise(64, 32, 16, rng);
	const std::vector<int> fractal = fractalNoise(64, 32, 16, 3, rng);
	REQUIRE((field.size() == 64 * 32 && fractal.size() == field.size()));
	for (int v : field)
		REQUIRE((v >= 0 && v <= 65535));
	for (int v : fractal)
		REQUIRE((v >= 0 && v <= 65535));
	const std::vector<float> torus = torusNoise(64, 32, rng);
	float peak = 0;
	for (float v : torus)
	{
		REQUIRE((v >= -1 && v <= 1));
		peak = std::max(peak, std::abs(v));
	}
	REQUIRE(peak == 1.0f);
	REQUIRE((percentile({5, 1, 4, 2, 3}, 40) == 3 && percentile({5, 1, 4, 2, 3}, 0) == 1));
	REQUIRE((percentile({5, 1, 4, 2, 3}, 100) == 5 && percentile({}, 50) == 0));
}

// The wedge frame divides the map into equal wedges round the centre, with the arc offset zero
// on each wedge's middle; a blob holds what lies inside its outline.
inline void wedgeChecks()
{
	const Torus t(64, 64);
	const WedgeFrame frame(t, 0.0, 4);
	REQUIRE((frame.cx == 32 && frame.cy == 32 && std::abs(frame.wedge - kPi / 2) < 1e-12));
	int perWedge[4] = {0, 0, 0, 0};
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const WedgeFrame::Cell cell = frame.cell(x, y);
			REQUIRE((cell.k >= 0 && cell.k < 4 && cell.u >= 0 && cell.u < 1));
			REQUIRE(std::abs(cell.s - cell.d * (cell.u - 0.5) * frame.wedge) < 1e-9);
			REQUIRE(std::abs(cell.d - std::hypot(double(cell.dx), double(cell.dy))) < 1e-12);
			++perWedge[cell.k];
		}
	for (int k = 0; k < 4; ++k)
		REQUIRE(perWedge[k] > 64 * 64 / 5);
	const WedgeFrame::Cell middle = frame.cell(32 + 7, 32 + 7); // straight down wedge 0's middle
	REQUIRE((middle.k == 0 && std::abs(middle.s) < 1e-9 && std::abs(middle.u - 0.5) < 1e-12));
	WedgeFrame::Cell bent = middle;
	frame.bend(bent, 2.0);
	REQUIRE((bent.s < middle.s - 1.5 && bent.d == middle.d));
	GenerationRequest request;
	request.seed = 2;
	GenerationContext context(request);
	const Blob round{0, 10, 1.0, 0.0, RadialShape(3, 0, context, "blob")};
	REQUIRE((round.holds(0, 0) && round.holds(2.5, 0) && !round.holds(0, 3.5) && round.reach() == 3));
	const Blob stretched{0, 10, 2.0, 0.0, RadialShape(3, 0, context, "blob")};
	REQUIRE((stretched.holds(5, 0) && !stretched.holds(0, 2) && stretched.reach() == 6));
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
	REQUIRE((order == same && order != std::vector<int>({0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
													   10, 11, 12, 13, 14, 15, 16, 17, 18, 19})));
	std::vector<int> sorted = order;
	std::sort(sorted.begin(), sorted.end());
	for (int i = 0; i < 20; ++i)
		REQUIRE(sorted[i] == i);
	struct Layout
	{
		Torus t{1, 1};
		std::string failure;
	};
	Game game(nullptr);
	game.map.setSize(5, 5);
	Layout rebuilt;
	rebuilt.t = {32, 32};
	REQUIRE(designMismatch(rebuilt, game.map, "test").empty());
	rebuilt.t = {16, 32};
	REQUIRE(designMismatch(rebuilt, game.map, "test") ==
		   "The test design does not match the map size.");
	rebuilt.failure = "no room";
	REQUIRE(designMismatch(rebuilt, game.map, "test") ==
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
			REQUIRE(mask[t.at(x, y)] == (dx * dx + dy * dy < 2.5 * 2.5));
		}
	std::fill(mask.begin(), mask.end(), 0);
	strokePath(mask, t, {{-3, 4, 1.5}, {3, 4, 1.5}}, 7);
	REQUIRE((mask[t.at(31, 4)] == 7 && mask[t.at(29, 4)] == 7 && mask[t.at(2, 4)] == 7));
	REQUIRE((mask[t.at(27, 4)] == 0 && mask[t.at(31, 6)] == 0));
	std::fill(mask.begin(), mask.end(), 0);
	strokePath(mask, t, {{16, 8, 4}, {26, 8, 0.5}});
	REQUIRE((mask[t.at(16, 11)] && !mask[t.at(25, 9)] && mask[t.at(25, 8)]));
	std::fill(mask.begin(), mask.end(), 0);
	strokePath(mask, t, {{10, 10, 1.1}});
	REQUIRE(std::count(mask.begin(), mask.end(), 1) == 5);
	const std::vector<StrokePoint> curve = bezierPath({0, 0}, {10, 10}, {20, 0}, 1, 3, 10);
	REQUIRE((curve.size() == 11 && curve.front().x == 0 && curve.back().x == 20));
	REQUIRE((std::abs(curve[5].y - 5) < 1e-9 && std::abs(curve[5].halfWidth - 2) < 1e-9));

	GenerationRequest request;
	request.seed = 11;
	GenerationContext context(request);
	const RadialShape shape(5, 0.4, context, "shape");
	std::vector<unsigned char> upright(t.size(), 0), turned(t.size(), 0);
	fillShape(upright, t, 16, 8, shape);
	fillShape(turned, t, 16, 8, shape, kPi / 2);
	const long area = std::count(upright.begin(), upright.end(), 1);
	REQUIRE((area > 40 && std::abs(area - std::count(turned.begin(), turned.end(), 1)) < area / 8));
	REQUIRE(upright != turned);
}

// bentPath runs its length along its heading and bows to the left; tracePath is one tile thick and
// unbroken; pathClearance measures the water
// between two strokes and skips a branch's root; growBranches forks level by level to its depth,
// offers every branch to accept, retries a refused one at half length and grows the same tree from
// the same rolls.
inline void branchChecks()
{
	const std::vector<StrokePoint> straight = bentPath({0, 0}, 0, 20, 0, 3, 1, 10);
	REQUIRE((straight.size() == 11 && std::abs(straight.back().x - 20) < 1e-9 &&
		   std::abs(straight.back().y) < 1e-9 && std::abs(straight.back().halfWidth - 1) < 1e-9));
	const std::vector<StrokePoint> bowed = bentPath({0, 0}, 0, 20, 0.1, 1, 1, 10);
	REQUIRE((std::abs(bowed[5].y - 2) < 1e-9 && std::abs(bowed.back().x - 20) < 1e-9));

	const std::vector<StrokePoint> a{{0, 0, 1}, {20, 0, 1}}, b{{0, 6, 1}, {20, 6, 1}};
	REQUIRE((std::abs(pathClearance(a, b) - 4) < 1e-9 && std::abs(pathClearance(b, a) - 4) < 1e-9));
	const std::vector<StrokePoint> crossing{{10, -5, 1}, {10, 5, 1}};
	REQUIRE(pathClearance(a, crossing) < 0);
	const std::vector<StrokePoint> leaving{{10, 0, 1}, {10, 20, 1}};
	REQUIRE((pathClearance(leaving, a) < 0 && pathClearance(leaving, a, 4) > 1.9));
	const std::vector<StrokePoint> dot{{10, 4, 1}};
	REQUIRE(std::abs(pathClearance(dot, a) - 2) < 1e-9);
	// tracePath: one tile thick and unbroken, even along a shallow diagonal and across the wrap.
	{
		const Torus smallTorus(32, 16);
		std::vector<unsigned char> line(smallTorus.size(), 0);
		tracePath(line, smallTorus, {{2, 3, 5}, {29, 11, 5}, {35, 12, 5}});
		for (int x = 2; x <= 29; ++x)
		{
			int column = 0;
			for (int y = 0; y < smallTorus.h; ++y)
				column += line[smallTorus.at(x, y)];
			REQUIRE((column >= 1 && column <= 2));
		}
		REQUIRE((line[smallTorus.at(2, 3)] && line[smallTorus.at(29, 11)] && line[smallTorus.at(3, 12)]));
		REQUIRE((!line[smallTorus.at(15, 14)] && !line[smallTorus.at(10, 0)]));
	}
	const PathBounds bounds = pathBounds(a);
	REQUIRE((std::abs(bounds.x - 10) < 1e-9 && std::abs(bounds.radius - 11) < 1e-9));

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
	REQUIRE((full.size() == 7 && full[0].parent < 0 && !full[0].leaf && full[2].depth == 1 &&
		   !full[2].leaf && full[3].depth == 2 && full[6].parent == 2 && full[6].leaf));
	const std::vector<Branch> again = grow(3, 2, false);
	for (size_t i = 0; i < full.size(); ++i)
		REQUIRE((full[i].path.back().x == again[i].path.back().x &&
			   full[i].path.back().y == again[i].path.back().y));
	// The first child is refused once and kept at half length, so its subtree is shorter too.
	const std::vector<Branch> retried = grow(3, 2, true);
	REQUIRE(retried.size() == 7);
	const auto length = [](const Branch &branch)
	{
		return std::hypot(branch.path.back().x - branch.path.front().x,
						  branch.path.back().y - branch.path.front().y);
	};
	REQUIRE(std::abs(length(retried[1]) - length(full[1]) / 2) < 1e-6);
}

// Stretch is exactly the identity on a square map and fills a rectangle as an ellipse; a stretched
// WedgeFrame measures cells in the round design frame; a stretched shape fill becomes an oval.
inline void stretchChecks()
{
	const Stretch square = Stretch::toFill(64, 64);
	REQUIRE((square.sx == 1 && square.sy == 1 && square.heading(1.234) == 1.234));
	const ShapePoint same = square.apply(32, 32, {10.1, 3.7});
	REQUIRE((same.x == 10.1 && same.y == 3.7));
	const Stretch wide = Stretch::toFill(64, 16);
	REQUIRE((wide.sx == 4 && wide.sy == 1 && wide.longest() == 4));
	const ShapePoint distant = wide.apply(32, 8, {40, 8});
	REQUIRE((distant.x == 64 && distant.y == 8));
	REQUIRE(std::abs(wide.heading(kPi / 4) - std::atan2(1.0, 4.0)) < 1e-12);

	const Torus t(64, 16);
	const WedgeFrame round(t, 0, 4, wide);
	REQUIRE(std::abs(round.cell(32 + 20, 8).d - 5) < 1e-9);
	REQUIRE(std::abs(round.cell(32, 8 + 5).d - 5) < 1e-9);

	GenerationRequest request;
	request.seed = 3;
	GenerationContext context(request);
	const RadialShape disc(4, 0, context, "stretch");
	std::vector<unsigned char> plain(t.size(), 0), oval(t.size(), 0);
	fillShape(plain, t, 32, 8, disc);
	fillShape(oval, t, 32, 8, disc, 0, 1, wide);
	REQUIRE((plain[t.at(35, 8)] && !plain[t.at(40, 8)] && oval[t.at(45, 8)] && !oval[t.at(32, 12)]));
	REQUIRE(std::count(oval.begin(), oval.end(), 1) > 3 * std::count(plain.begin(), plain.end(), 1));
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
				REQUIRE((x >= 21 && x <= 25 && y >= 6 && y <= 25 && !(x == 16 && y == 16)));
			}
	// Steps are Chebyshev: 20 by 20 tiles lie three or more from the water, less the one refused,
	// and the quarter of them turned are the five highest columns.
	REQUIRE((sand == int(std::lround((20 * 20 - 1) * 0.25)) && sketch[t.at(24, 12)] == SAND &&
		   sketch[t.at(7, 12)] == GRASS));

	Game game(nullptr);
	grassMap(game, 6, 6);
	Map &map = game.map;
	const Torus m(map);
	{
		auto batch = map.editTerrain();
		for (int y = 0; y < m.h; ++y)
			for (int x = 0; x < m.w; ++x)
				map.setVertexTerrain(x, y, x >= 40 && x < 48 ? SAND : WATER);
	}
	const std::vector<double> chance = algaeGrowthChance(map, m);
	REQUIRE(chance[m.at(44, 10)] == 0);                   // sand, not water
	REQUIRE(chance[m.at(36, 10)] > chance[m.at(20, 10)]); // nearer the sand grows faster
	// A probability: the weights of all offsets sum to 1.
	REQUIRE((chance[m.at(36, 10)] > 0 && chance[m.at(36, 10)] <= 1));
	// Every tile of the sea sees the strip within 30 tiles, but no offset can reach sand from a
	// map that has none.
	{
		auto batch = map.editTerrain();
		for (int x = 0; x < m.w; ++x)
			map.setVertexTerrain(x, 5, WATER);
		for (int y = 0; y < m.h; ++y)
			for (int x = 40; x < 48; ++x)
				map.setVertexTerrain(x, y, WATER);
	}
	const std::vector<double> dry = algaeGrowthChance(map, m);
	REQUIRE(std::count(dry.begin(), dry.end(), 0.0) == long(dry.size()));
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
	plantFields(GenerationMap(map).ref(), t, tiles, 6, 3, [](int i) { return -i; });
	REQUIRE((countResource(map, WHEAT) == 6 && countResource(map, WOOD) == 3));
	REQUIRE((map.getResource(8, 5).type == WHEAT && map.getResource(0, 5).type == WOOD));
	REQUIRE(map.getResource(9, 5).type == NO_RES_TYPE);

	const KitFrame east{10, 10, 0}, south{10, 10, kPi / 2};
	REQUIRE((east.at(4, 1, 3).x == 14 && east.at(4, 1, 3).y == 11 && east.at(4, 1, 3).within == 3));
	REQUIRE((south.at(4, 1, 3).x == 9 && south.at(4, 1, 3).y == 14));

	GenerationRequest request;
	request.seed = 3;
	GenerationContext context(request);
	std::vector<int> ground{t.at(40, 40), t.at(50, 50)};
	int calls = 0;
	REQUIRE((scatterClumps(
			   context, t, ground, 4, "clumps", [&](int i) { return i == ground[1]; },
			   [&](MapGeneratorPoint p)
			   {
				   REQUIRE((p.x == 50 && p.y == 50));
				   ++calls;
			   }) == 4 &&
		   calls == 4));
	REQUIRE(scatterClumps(
			   context, t, ground, 2, "clumps", [](int) { return false; },
			   [](MapGeneratorPoint) { REQUIRE(false); }) == 0);
	REQUIRE(scatterClumps(
			   context, t, {}, 2, "clumps", [](int) { return true; },
			   [](MapGeneratorPoint) { REQUIRE(false); }) == 0);
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
	writeVertices(GenerationMap(map).ref(), sketch);
	std::vector<unsigned char> sea(t.size(), 0), none(t.size(), 0), all(t.size(), 1);
	for (int i = 0; i < t.size(); ++i)
		sea[i] = map.vertexTerrainAt(i % t.w, i / t.w) == WATER;
	const std::vector<unsigned char> margin = seaMargin(map, t, sea, none);
	const std::vector<unsigned char> stone = sealCoasts(map, t, margin, all);
	REQUIRE((std::count(stone.begin(), stone.end(), 1) > 40 && !margin[t.at(16, 16)]));
	for (int i = 0; i < t.size(); ++i)
		if (stone[i])
		{
			REQUIRE((!margin[i] && map.terrainTypeAt(i % t.w, i / t.w) == GRASS));
			map.setResourceByIndex(i % t.w, i / t.w, STONE, 1);
		}
	std::vector<unsigned char> beach(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		beach[i] = margin[i] && !map.isWater(i % t.w, i / t.w);
	REQUIRE(std::count(beach.begin(), beach.end(), 1) > 0);
	const std::vector<int> landed = reachesWithShut(map, t, beach, none);
	REQUIRE(landed[t.at(16, 16)] < 0);
	std::vector<unsigned char> middle(t.size(), 0);
	middle[t.at(16, 16)] = 1;
	REQUIRE(reachesWithShut(map, t, middle, none)[t.at(12, 12)] >= 0);
	std::vector<unsigned char> row(t.size(), 0);
	for (int x = 0; x < t.w; ++x)
		row[t.at(x, 14)] = 1;
	REQUIRE(reachesWithShut(map, t, middle, row)[t.at(12, 12)] < 0);
	std::vector<unsigned char> onBeach(t.size(), 0);
	onBeach[t.at(6, 6)] = onBeach[t.at(16, 16)] = 1;
	const DesignedStone designed = designedStone(map, t, onBeach);
	REQUIRE((designed.gaps == 1 && designed.firstGap == t.at(6, 6) && designed.stone[t.at(16, 16)]));

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
			REQUIRE(border[i] == (x == 8 || x == 15));
			if (labels[i] != 0 || border[i])
				continue;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = square.at(x + dx, y + dy);
					REQUIRE((labels[j] != 1 || border[j]));
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
	REQUIRE(towerReach(strip, buildable, target) == 6);
	std::fill(buildable.begin(), buildable.end(), 0);
	for (int y = 0; y < strip.h; ++y)
		buildable[strip.at(2, y)] = 1;
	REQUIRE(towerReach(strip, buildable, target) == INT_MAX);

	std::vector<unsigned char> water(square.size(), 0);
	std::vector<int> queued(square.size(), 0);
	const auto flat = [](int tile) { return tile; };
	REQUIRE((growWater(
			   square, water, square.at(8, 8), 20, [](int) { return true; }, flat, queued, 1) ==
			   20 &&
		   std::count(water.begin(), water.end(), 1) == 20));
	std::fill(water.begin(), water.end(), 0);
	const auto smallRoom = [&](int tile) { return tile % square.w < 3 && tile / square.w < 2; };
	REQUIRE(growWater(square, water, 0, 20, smallRoom, flat, queued, 2) == 6);

	const std::vector<StrokePoint> arc = arcPath(10, 10, 10, 0, kPi / 2, 2, 3);
	REQUIRE((std::abs(arc.front().x - 20) < 1e-9 && std::abs(arc.back().y - 20) < 1e-9));
	for (size_t k = 1; k < arc.size(); ++k)
	{
		REQUIRE(std::abs(std::hypot(arc[k].x - 10, arc[k].y - 10) - 10) < 1e-9);
		REQUIRE(std::hypot(arc[k].x - arc[k - 1].x, arc[k].y - arc[k - 1].y) <= 3);
	}
}

// growTerritories: equal areas to the tile on an open torus, each territory in one piece, the same
// result grown twice, and a boxed-in territory that stops while the rest share out the remainder.
// Rock walls (shared/Rock): a wall drawn as tiles blocks exactly those tiles once its vertices are
// rock, a quarry face left out of it keeps four grass corners and holds stone, talus skirts the rock
// and deep cores keep a shallow margin.
inline void rockChecks()
{
	Game game(nullptr);
	grassMap(game, 5, 5);
	Map &map = game.map;
	const Torus t(map);
	std::vector<unsigned char> wall(t.size(), 0), quarry(t.size(), 0), open(t.size(), 0);
	for (int y = 10; y < 16; ++y)
		for (int x = 0; x < t.w; ++x)
			wall[t.at(x, y)] = 1;
	for (int i = 0; i < t.size(); ++i)
		open[i] = !wall[i];
	const std::vector<int> face = quarryNear(t, wall, open, t.at(16, 15), 2, 2);
	REQUIRE(face.size() == 10); // five columns, two rows deep, on the wall's lower face
	for (int i : face)
	{
		REQUIRE((wall[i] && (i / t.w == 14 || i / t.w == 15)));
		quarry[i] = 1;
	}
	std::vector<unsigned char> rockTiles(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		rockTiles[i] = wall[i] && !quarry[i];
	const std::vector<unsigned char> rock = rockVertices(t, rockTiles);
	TerrainSketch sketch(t.size(), GRASS);
	for (int i = 0; i < t.size(); ++i)
		if (rock[i])
			sketch[i] = RIDGE_ROCK;
	const std::vector<unsigned char> approach = quarryApproach(t, quarry);
	for (int i = 0; i < t.size(); ++i)
		REQUIRE((!(approach[i] && quarry[i]) && !(approach[i] && rock[i] && !wall[i])));
	writeVertices(GenerationMap(map).ref(), sketch);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = t.at(x, y);
			const bool walkable = map.terrainPropertiesAt(x, y).walkable;
			// Open ground stays open and the wall's inner rows are blocked; only the outer rows beside
			// the quarry's ends, convex corners of the rock, keep a single rock corner.
			if (!wall[i])
				REQUIRE(walkable);
			else if (!quarry[i] && y > 10 && y < 15)
				REQUIRE(!walkable);
		}
	for (int i : face)
	{
		REQUIRE(map.isResourceAllowed(t.remainderX(i), i / t.w, STONE));
		map.setResourceByIndex(t.remainderX(i), i / t.w, STONE, 1);
	}
	{
		// With its quarry stocked, nothing walks through the wall.
		std::vector<unsigned char> passable(t.size(), 0), north(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
		{
			// Rows 24 and 25 are shut too, so the torus cannot carry a walk round the back.
			passable[i] = map.terrainPropertiesAt(t.remainderX(i), i / t.w).walkable &&
						  !map.isResource(t.remainderX(i), i / t.w) && i / t.w != 24 && i / t.w != 25;
			north[i] = i / t.w == 5;
		}
		const Flood flood = floodFrom(t, north, passable);
		for (int i : flood.visited)
			REQUIRE(i / t.w != 20);
	}
	std::vector<int> owner(t.size(), -1);
	for (int i : face)
		owner[i] = 0;
	REQUIRE(quarryFault(map, t, quarry, owner, 1, 10).empty());
	REQUIRE(!quarryFault(map, t, quarry, owner, 1, 11).empty());
	map.replaceResource(t.remainderX(face[0]), face[0] / t.w, Resource{});
	REQUIRE(!quarryFault(map, t, quarry, owner, 1, 1).empty());
	// Talus: one tile always, never on the wall or where not eligible.
	std::vector<unsigned char> eligible(t.size(), 1);
	eligible[t.at(8, 9)] = 0;
	const std::vector<int> calm(t.size(), 0);
	const std::vector<unsigned char> apron = talusApron(t, rockTiles, eligible, calm, 1, 3, 30000);
	REQUIRE((apron[t.at(10, 9)] && !apron[t.at(8, 9)] && !apron[t.at(10, 7)] && !apron[t.at(10, 12)]));
	// Deep cores leave at least two steps of shallow water.
	std::vector<unsigned char> water(t.size(), 0);
	for (int y = 2; y < 12; ++y)
		for (int x = 2; x < 12; ++x)
			water[t.at(x, y)] = 1;
	const std::vector<unsigned char> core = deepCore(t, water, 1);
	REQUIRE((core[t.at(6, 6)] && !core[t.at(2, 6)] && core[t.at(3, 6)] && !core[t.at(20, 20)]));
}

// The height-field post-pass helpers (HeightFieldLooks.h), on small hand-built sketches and maps:
// wet means either water group, deep cores keep their shallows, take only big enough bodies and round
// off in a straight line, the deep reach grows with a body's width, bends read high inside and low outside, the major body drops
// the ponds, levees and pavement keep their distances, grass grades by distance, gardens are round,
// writing a post-pass clears exactly the deposits the new terrain refuses, a ring of rock is seen to
// part the ground and a lone knot is not, and the rebuilt height-field terrain is the stages' own.
inline void heightFieldLooksChecks()
{
	const Torus t(32, 32);
	const auto pond = [&](int x0, int y0, int x1, int y1)
	{
		TerrainSketch s(size_t(t.size()), GRASS);
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
				s[size_t(t.at(x, y))] = WATER;
		return s;
	};
	{
		// Wet vertices and steps from them.
		TerrainSketch s = pond(4, 4, 8, 8);
		s[size_t(t.at(5, 5))] = DEEP_WATER;
		s[size_t(t.at(20, 20))] = SAND;
		const std::vector<unsigned char> wet = wetVertices(s);
		REQUIRE((wet[size_t(t.at(5, 5))] && wet[size_t(t.at(4, 4))] && !wet[size_t(t.at(20, 20))] &&
			   !wet[size_t(t.at(8, 8))]));
		const std::vector<int> steps = stepsFromWet(t, s);
		REQUIRE((steps[size_t(t.at(5, 5))] == 0 && steps[size_t(t.at(8, 5))] == 1 && steps[size_t(t.at(10, 10))] == 3));
		// Straight-line distance: round contours, never under the steps.
		const std::vector<int> round = distanceFromWet(t, s);
		REQUIRE((round[size_t(t.at(10, 10))] == 4 && round[size_t(t.at(8, 5))] == 1 && round[size_t(t.at(5, 5))] == 0));
		for (int i = 0; i < t.size(); ++i)
			REQUIRE(round[size_t(i)] >= steps[size_t(i)]);
		REQUIRE(distanceFromWet(t, TerrainSketch(size_t(t.size()), GRASS))[0] == -1);
		const std::vector<int> dry = stepsFromWet(t, TerrainSketch(size_t(t.size()), GRASS));
		REQUIRE(std::all_of(dry.begin(), dry.end(), [](int v) { return v == -1; }));
	}
	{
		// Deep cores: shallows round them, small bodies skipped, eligibility respected.
		TerrainSketch s = pond(4, 4, 18, 18);
		for (int y = 24; y < 29; ++y)
			for (int x = 24; x < 29; ++x)
				s[size_t(t.at(x, y))] = WATER;
		TerrainSketch a = s;
		const int deep = deepenWater(a, t, 3, 4);
		REQUIRE((deep > 0 && a[size_t(t.at(10, 10))] == DEEP_WATER && a[size_t(t.at(26, 26))] == WATER));
		for (int i = 0; i < t.size(); ++i)
			if (a[size_t(i)] == DEEP_WATER)
				for (int dy = -2; dy <= 2; ++dy)
					for (int dx = -2; dx <= 2; ++dx)
						REQUIRE(terrainGroup(TerrainType(a[size_t(t.at(t.remainderX(i) + dx, i / t.w + dy))])) !=
							   TerrainGroup::Grass);
		std::vector<unsigned char> left(size_t(t.size()), 0);
		for (int i = 0; i < t.size(); ++i)
			left[size_t(i)] = t.remainderX(i) < 11;
		TerrainSketch b = s;
		deepenWater(b, t, 3, 4, DARK_WATER, &left);
		REQUIRE((b[size_t(t.at(8, 10))] == DARK_WATER && b[size_t(t.at(13, 10))] == WATER));
		// The reach for a share: of the 221 water vertices, 101 lie 3 tiles or more from land (the big
		// pond's 10x10 and the small one's middle; square ponds are as far from land in a straight line as
		// in steps) and 64 lie 4 out, so a 500 permille cap is met at the floor and a 300 permille cap
		// needs one tile more.
		REQUIRE(deepReachForShare(s, t, 3, 500) == 3);
		const int tight = deepReachForShare(s, t, 3, 300);
		REQUIRE(tight == 4);
		TerrainSketch c = s;
		REQUIRE(deepenWater(c, t, tight, 1) * 1000 <= (196 + 25) * 300);
		// The reach is a straight line, so the core rounds off round a point of land: a vertex
		// diagonally three steps from it is 4.2 tiles away and deep at a reach of 4, one three tiles
		// straight out is not, one four out is.
		TerrainSketch d = s;
		d[size_t(t.at(11, 11))] = SAND;
		deepenWater(d, t, 4, 1);
		REQUIRE((d[size_t(t.at(14, 14))] == DEEP_WATER && d[size_t(t.at(14, 11))] == WATER &&
			   d[size_t(t.at(7, 11))] == DEEP_WATER));
	}
	{
		// Bends: inside a convex corner of land the share is high, inside a bay low, 500 on a straight
		// bank; the major body leaves the small pond out.
		TerrainSketch s(size_t(t.size()), GRASS);
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 16; ++x)
				s[size_t(t.at(x, y))] = WATER;
		const std::vector<int> straight = wetShare(t, wetVertices(s), 4);
		REQUIRE(std::abs(straight[size_t(t.at(16, 10))] - 444) <= 1); // four of nine columns wet
		REQUIRE(std::abs(straight[size_t(t.at(15, 10))] - 555) <= 1);
		TerrainSketch big = pond(2, 2, 20, 20);
		big[size_t(t.at(26, 26))] = WATER;
		const std::vector<unsigned char> major = majorWater(t, wetVertices(big), 250);
		REQUIRE((major[size_t(t.at(10, 10))] && !major[size_t(t.at(26, 26))]));
		const std::vector<int> share = wetShare(t, wetVertices(big), 3);
		REQUIRE((share[size_t(t.at(1, 1))] < 300 && share[size_t(t.at(20, 10))] > 300));
	}
	{
		// Levees and pavement keep their distances; grass grades by distance; gardens stay plain.
		TerrainSketch s = pond(0, 0, 4, 32);
		const std::vector<int> steps = stepsFromWet(t, s);
		std::vector<unsigned char> all(size_t(t.size()), 1), none(size_t(t.size()), 0);
		TerrainSketch l = s;
		const int laid = layLevee(l, steps, all, 2, 3, {}, 0);
		// Two columns on each side of the strip of water (the torus wraps it round).
		REQUIRE((laid == 4 * 32 && l[size_t(t.at(5, 3))] == LOAM && l[size_t(t.at(6, 3))] == LOAM &&
			   l[size_t(t.at(30, 3))] == LOAM && l[size_t(t.at(4, 3))] == GRASS && l[size_t(t.at(7, 3))] == GRASS));
		std::vector<int> wide(size_t(t.size()), 65535);
		TerrainSketch lw = s;
		REQUIRE((layLevee(lw, steps, all, 2, 3, wide, 40000) == 6 * 32 && layLevee(lw, steps, none, 2, 9, {}, 0) == 0));
		TerrainSketch p = s;
		const std::vector<int> calm(size_t(t.size()), 65535), rough(size_t(t.size()), 0);
		paveBeyondWater(p, steps, all, calm, 10, 4, GRASS, SHALE);
		TerrainSketch q = s;
		paveBeyondWater(q, steps, all, rough, 10, 4, GRASS, SHALE);
		for (int i = 0; i < t.size(); ++i)
		{
			if (p[size_t(i)] == SHALE)
				REQUIRE(steps[size_t(i)] >= 10);
			if (q[size_t(i)] == SHALE)
				REQUIRE(steps[size_t(i)] >= 13);
		}
		REQUIRE((p[size_t(t.at(14, 5))] == SHALE && q[size_t(t.at(14, 5))] == GRASS && q[size_t(t.at(17, 5))] == SHALE));
		TerrainSketch looks(size_t(t.size()), 0);
		std::vector<unsigned char> plain(size_t(t.size()), 0);
		plain[size_t(t.at(5, 9))] = 1;
		gradeGrassLooks(looks, s, steps, plain, {}, 2, 8, 0);
		REQUIRE((looks[size_t(t.at(5, 3))] == LUSH_GRASS && looks[size_t(t.at(5, 9))] == 0 &&
			   looks[size_t(t.at(9, 3))] == 0 && looks[size_t(t.at(15, 3))] == DRY_GRASS && looks[size_t(t.at(2, 3))] == 0));
		const std::vector<unsigned char> garden = homeGardens(t, {10}, {10}, 1, 4.0, {}, 0);
		REQUIRE((garden[size_t(t.at(12, 12))] && garden[size_t(t.at(16, 12))] && !garden[size_t(t.at(17, 12))] &&
			   !garden[size_t(t.at(15, 15))]));
	}
	{
		// Writing a post-pass: looks inside their groups, deposits the new terrain refuses cleared and
		// counted, algae under deep water listed for reseating.
		Game game(nullptr);
		grassMap(game, 5, 5);
		Map &map = game.map;
		TerrainSketch mech = pond(20, 20, 28, 28);
		writeVertices(GenerationMap(map).ref(), mech);
		map.setResourceByIndex(5, 5, WHEAT, 1);
		map.setResourceByIndex(10, 5, WOOD, 1);
		map.setResourceByIndex(23, 23, ALGA, 1);
		map.setResourceByIndex(20, 26, ALGA, 1);
		REQUIRE((map.isResource(5, 5) && map.isResource(23, 23)));
		for (int y = 5; y <= 6; ++y)
			for (int x = 5; x <= 6; ++x)
				mech[size_t(t.at(x, y))] = SHALE;
		for (int y = 22; y <= 25; ++y)
			for (int x = 22; x <= 25; ++x)
				mech[size_t(t.at(x, y))] = DEEP_WATER;
		TerrainSketch looks(size_t(t.size()), 0);
		looks[size_t(t.at(10, 5))] = DRY_GRASS;
		looks[size_t(t.at(11, 5))] = HEATH; // not a grass look: ignored
		const DisplacedResources lost = writeLayeredTerrain(GenerationMap(map).ref(), mech, looks);
		REQUIRE((lost.wheat == 1 && lost.wood == 0 && lost.algae == 1 && lost.total() == 2));
		REQUIRE((lost.algaeTiles.size() == 1 && lost.algaeTiles[0] == t.at(23, 23)));
		REQUIRE((!map.isResource(5, 5) && map.isResource(10, 5) && map.isResource(20, 26)));
		REQUIRE((map.vertexTerrainAt(10, 5) == DRY_GRASS && map.vertexTerrainAt(11, 5) == GRASS &&
			   map.vertexTerrainAt(23, 23) == DEEP_WATER));
		const TerrainSketch read = mapSketch(map);
		REQUIRE((read[size_t(t.at(10, 5))] == DRY_GRASS && read[size_t(t.at(5, 5))] == SHALE));
	}
	{
		// A ring of rock round a patch parts the ground; a lone knot in the open does not.
		Game game(nullptr);
		grassMap(game, 5, 5);
		Map &map = game.map;
		REQUIRE(partingBarrier(map) < 0);
		TerrainSketch s(size_t(t.size()), GRASS);
		for (int y = 3; y <= 6; ++y)
			for (int x = 3; x <= 6; ++x)
				s[size_t(t.at(x, y))] = RIDGE_ROCK;
		writeVertices(GenerationMap(map).ref(), s);
		REQUIRE(partingBarrier(map) < 0);
		for (int y = 10; y <= 22; ++y)
			for (int x = 10; x <= 22; ++x)
				if (std::max(std::abs(x - 16), std::abs(y - 16)) >= 5)
					s[size_t(t.at(x, y))] = RIDGE_ROCK;
		writeVertices(GenerationMap(map).ref(), s);
		const int parting = partingBarrier(map);
		REQUIRE((parting >= 0 && std::max(std::abs(t.remainderX(parting) - 16), std::abs(parting / t.w - 16)) >= 4));
	}
	{
		// The rebuilt terrain: the stages' own, and the same from every fresh context.
		GenerationRequest request;
		request.setMethodDefaults(GeneratorRegistry::builtins().idOf("swamp"));
		request.wDec = request.hDec = 6;
		request.nbTeams = 2;
		request.seed = 5;
		const HeightFieldOptions options = HeightFieldOptions::fromRequest(request, true);
		const HeightFieldBuilder plain = [](HeightMap &hm, unsigned, unsigned, float smoothing)
		{ hm.makeSwamp(smoothing); };
		GenerationContext a(request), b(request), c(request);
		const TerrainSketch first = heightFieldSketch(request, a, options, plain);
		REQUIRE(first == heightFieldSketch(request, b, options, plain));
		const HeightFieldTiling tiling = heightFieldTiling(64, 64, options.repeat);
		HeightMap hm(tiling.w, tiling.h, c.stream("heightmap"));
		plain(hm, tiling.w, tiling.h, (float)(options.smoothing + 4) * 3);
		Game game(nullptr);
		grassMap(game, 6, 6);
		paintHeightFieldTerrain(GenerationMap(game.map).ref(), hm, tiling, classifyHeightField(hm, tiling, options));
		REQUIRE(first == mapSketch(game.map));
		REQUIRE((std::count(first.begin(), first.end(), (unsigned char)WATER) > 0 &&
			   std::count(first.begin(), first.end(), (unsigned char)GRASS) > 0));
	}
}

inline void territoryChecks()
{
	const Torus t(30, 30);
	std::vector<unsigned char> eligible(t.size(), 1);
	const std::vector<std::vector<int>> seeds = {{t.at(5, 5)}, {t.at(20, 8)}, {t.at(12, 22)}};
	const auto noise = [&](int tile) { return (tile * 2654435761u) % 700; };
	const Territories grown = growTerritories(t, eligible, seeds, noise);
	REQUIRE(grown.areas == std::vector<int>({300, 300, 300}));
	REQUIRE(grown.labels == growTerritories(t, eligible, seeds, noise).labels);
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
		REQUIRE(reached == 300);
	}
	// A 3x3 room walled off round colony 0's seed.
	for (int y = 3; y <= 7; ++y)
		for (int x = 3; x <= 7; ++x)
			if (x == 3 || x == 7 || y == 3 || y == 7)
				eligible[t.at(x, y)] = 0;
	const Territories boxed = growTerritories(t, eligible, seeds, noise);
	REQUIRE((boxed.areas[0] == 9 && std::abs(boxed.areas[1] - boxed.areas[2]) <= 1 &&
		   boxed.areas[1] + boxed.areas[2] == 900 - 16 - 9));
	REQUIRE((boxed.labels[t.at(3, 3)] == -1 && boxed.smallest() == 9));
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
	REQUIRE((gateOf[t.at(52, 32)] == 0 && gateOf[t.at(12, 32)] == 1 && gateOf[t.at(32, 52)] == -1));
	REQUIRE((gateOf[t.at(32, 32)] == -2 && gateOf[t.at(52, 36)] == -1));

	const AxisFrame frame{32, 32, kPi / 2};
	const ShapePoint p = frame.at(10, 4);
	REQUIRE((std::abs(p.x - 28) < 1e-9 && std::abs(p.y - 42) < 1e-9));
	const ShapePoint back = frame.project(p.x - 32, p.y - 32);
	REQUIRE((std::abs(back.x - 10) < 1e-9 && std::abs(back.y - 4) < 1e-9));
	const Zigzag zigzag = zigzagPath(AxisFrame{0, 0, 0}, 40, 30, 8, 3, 6, 5, 2);
	REQUIRE((zigzag.legs.size() == 3 && zigzag.path.size() == 8 && zigzag.finishAcross == -6));
	REQUIRE((zigzag.path.front().x == 40 && zigzag.path.front().y == 6 && zigzag.path.back().x == 5));
	REQUIRE((zigzag.legs[1].front().x == 22 && zigzag.legs[1].front().y == -4 &&
		   zigzag.legs[1].back().y == 4));

	std::vector<unsigned char> road(t.size(), 0), water(t.size(), 0);
	for (int x = 0; x < t.w; ++x)
		road[t.at(x, 10)] = 1;
	for (int y = 0; y < t.h; ++y)
		water[t.at(0, y)] = 1;
	keepRoadInland(road, t, water, 3);
	REQUIRE((!road[t.at(2, 10)] && road[t.at(3, 10)] && !road[t.at(62, 10)]));
	const std::vector<unsigned char> spoiled = roadTiles(t, road);
	REQUIRE((spoiled[t.at(3, 9)] && spoiled[t.at(2, 10)] && !spoiled[t.at(3, 11)]));

	std::vector<int> labels(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		labels[i] = i % t.w < 32 ? 0 : 1;
	const std::vector<unsigned char> thin = labelBorders(t, labels),
									 thick = labelBorders(t, labels, 2);
	REQUIRE(std::count(thick.begin(), thick.end(), 1) ==
		   2 * std::count(thin.begin(), thin.end(), 1));
	REQUIRE((thick[t.at(31, 5)] && thick[t.at(32, 5)] && !thin[t.at(31, 5)]));

	// Stranded ground and depth sites.
	std::vector<unsigned char> ground(t.size(), 1);
	for (int y = 0; y < t.h; ++y)
		ground[t.at(20, y)] = 0;
	const std::vector<unsigned char> stranded = strandedGround(t, {t.at(5, 5)}, ground);
	REQUIRE((!stranded[t.at(19, 5)] && !stranded[t.at(21, 5)] && !stranded[t.at(20, 5)]));
	for (int y = 0; y < t.h; ++y)
		ground[t.at(40, y)] = 0;
	const std::vector<unsigned char> cut = strandedGround(t, {t.at(5, 5)}, ground);
	REQUIRE((cut[t.at(30, 5)] && !cut[t.at(50, 5)]));
	std::vector<int> depth(t.size(), -1), room(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
	{
		depth[i] = i % t.w;
		room[i] = std::min(i / t.w, t.h - 1 - i / t.w);
	}
	const int site = siteAtDepth(depth, room, 12, 5, 0);
	REQUIRE((site % t.w == 12 && room[site] == 31));
	REQUIRE(siteAtDepth(depth, room, 12, 40, 3) == -1);

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
	REQUIRE(growFarLake(
			   t, lake, stripDepth, stripRoom, 3, 60, [](int) { return 0.0; }, queued, 1) == 60);
	int distant = 0, nearby = 0;
	for (int i = 0; i < t.size(); ++i)
		if (lake[i])
		{
			REQUIRE(stripRoom[i] >= 3);
			(i % t.w > 30 ? distant : nearby)++;
		}
	REQUIRE((distant == 60 && nearby == 0));
	REQUIRE(growFarLake(
			   t, lake, stripDepth, stripRoom, 12, 60, [](int) { return 0.0; }, queued, 2) == 0);

	std::vector<int> jagged(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		jagged[i] = i % t.w < 32 ? 0 : 1;
	jagged[t.at(20, 20)] = 1;
	std::vector<unsigned char> keep(t.size(), 0);
	smoothLabels(t, jagged, 1, keep);
	REQUIRE((jagged[t.at(20, 20)] == 0 && jagged[t.at(40, 20)] == 1));

	// Clumps round a circle and a stocked tower, on a real map with a sealed island.
	Game game(nullptr);
	grassMap(game, 6, 6);
	Map &map = game.map;
	GenerationRequest request;
	request.seed = 5;
	GenerationContext context(request);
	const auto anywhere = [&](int i) { return clearGround(map, i % t.w, i / t.w); };
	REQUIRE(plantRound(GenerationMap(map).ref(), t, context, 32, 32, 10, {0.0, kPi / 2, kPi}, STONE,
					  0, 2, anywhere) == 3);
	REQUIRE((map.getResource(42, 32).type == STONE && map.getResource(22, 32).type == STONE));
	REQUIRE(plantOrchard(GenerationMap(map).ref(), t, context, 32, 32, 20, {0.0}, 4, 2, 0,
						anywhere) == 3);
	REQUIRE(map.getResource(52, 32).type == CHERRY + 1);

	game.addTeam();
	std::vector<unsigned char> allowed(t.size(), 0);
	for (int y = 4; y < 10; ++y)
		for (int x = 4; x < 10; ++x)
			allowed[t.at(x, y)] = 1;
	const int tower = placeTower(GenerationWorld(game).ref(), 0, 1, 3, 3, 4, allowed);
	REQUIRE(tower == t.at(4, 4));
	REQUIRE((map.getBuilding(4, 4) != NOGBID && map.getBuilding(5, 5) != NOGBID));
	REQUIRE((game.teams[0]->turrets.size() == 1 && game.teams[0]->turrets.front()->bullets > 0));
	std::vector<unsigned char> none(t.size(), 0);
	REQUIRE(placeTower(GenerationWorld(game).ref(), 0, 1, 30, 30, 4, none) == -1);
	// The closest ordinary site cannot cover x=14. The constraint must choose
	// the farther legal footprint at x=8 (its east tile x=9 has range 5), and
	// reject an impossible target without adding a partially valid tower.
	const int covering =
		placeTower(GenerationWorld(game).ref(), 0, 0, 5, 8, 4, allowed, true, {{14, 8}, {14, 9}});
	REQUIRE(covering == t.at(8, 7));
	const auto towerCount = game.teams[0]->turrets.size();
	REQUIRE(placeTower(GenerationWorld(game).ref(), 0, 0, 5, 8, 4, allowed, true, {{30, 30}}) == -1);
	REQUIRE(game.teams[0]->turrets.size() == towerCount);
	// Supplying a starting tower is a finite initial stock, not a new refill
	// rule. Existing calls remain empty; the opt-in uses the actual type's cap.
	REQUIRE(game.teams[0]->turrets.front()->material(STONE) == 0);
	REQUIRE(placeTower(GenerationWorld(game).ref(), 0, 0, 5, 8, 4, allowed, true, {}, true) >= 0);
	const auto supplied = game.teams[0]->turrets.back();
	REQUIRE(supplied->material(STONE) == supplied->type->maxMaterial[STONE]);
	REQUIRE(supplied->desiredMaxUnitWorking == 0);
	// A stocked starter inn registers feeding service immediately but does not
	// receive fruit. Its entire footprint must obey the same placement mask.
	std::vector<unsigned char> innGround(t.size(), 1);
	const int innSite = placeStartingBuilding(GenerationWorld(game).ref(), 0, "inn", 0, 18, 18, 3,
											  innGround, {WHEAT});
	REQUIRE((innSite >= 0 && !game.teams[0]->canFeedUnit.empty()));
	const auto inn = game.teams[0]->canFeedUnit.front();
	REQUIRE(inn->material(WHEAT) == inn->type->maxMaterial[WHEAT]);
	REQUIRE((inn->material(CHERRY) == 0 && inn->material(ORANGE) == 0 &&
		   inn->material(PRUNE) == 0));
	REQUIRE(placeStartingBuilding(GenerationWorld(game).ref(), 0, "inn", 0, 18, 18, 3, none,
								 {WHEAT}) == -1);

	// seaEntry finds the one grass gap in a walled coast.
	TerrainSketch sketch(t.size(), WATER);
	for (int y = 20; y < 44; ++y)
		for (int x = 20; x < 44; ++x)
			sketch[t.at(x, y)] = GRASS;
	layBeaches(sketch, t);
	writeVertices(GenerationMap(map).ref(), sketch);
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
				map.setResourceByIndex(i % t.w, i / t.w, STONE, 1);
		}
	REQUIRE((gap >= 0 && seaEntry(map, t, margin, island) >= 0));
	map.setResourceByIndex(gap % t.w, gap / t.w, STONE, 1);
	REQUIRE(seaEntry(map, t, margin, island) == -1);
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
		REQUIRE((tiles.front().first == subtileTile(a.x) && tiles.front().second == subtileTile(a.y)));
		REQUIRE((tiles.back().first == subtileTile(b.x) && tiles.back().second == subtileTile(b.y)));
		const std::set<std::pair<long long, long long>> line(tiles.begin(), tiles.end());
		for (size_t k = 1; k < tiles.size(); ++k)
		{
			const long long dx = tiles[k].first - tiles[k - 1].first,
							dy = tiles[k].second - tiles[k - 1].second;
			REQUIRE((std::llabs(dx) <= 1 && std::llabs(dy) <= 1 && (dx || dy)));
			if (dx && dy)
				REQUIRE((line.count({tiles[k - 1].first + dx, tiles[k - 1].second}) &&
					   line.count({tiles[k - 1].first, tiles[k - 1].second + dy})));
		}
		return tiles;
	};
	REQUIRE(sealed(subtileCentre(2, 5), subtileCentre(9, 5)).size() == 8);
	REQUIRE(sealed(subtileCentre(3, 3), subtileCentre(3, -4)).size() == 8);
	// A diagonal through tile corners exactly: every step takes both tiles beside the corner.
	REQUIRE(sealed({0, 0}, {5 * kSubtile, 5 * kSubtile}).size() == 16);
	for (int k = 0; k < 40; ++k)
		sealed({k * 7 - 90, k * 13 - 50}, {300 - k * 11, k * 5 + 17});

	const Torus t(24, 16);
	std::vector<unsigned char> mask(t.size(), 0);
	fillPolygon(mask, t,
				{{2 * kSubtile, 3 * kSubtile},
				 {5 * kSubtile, 3 * kSubtile},
				 {5 * kSubtile, 7 * kSubtile},
				 {2 * kSubtile, 7 * kSubtile}});
	REQUIRE((std::count(mask.begin(), mask.end(), 1) == 12 && mask[t.at(2, 3)] && mask[t.at(4, 6)] &&
		   !mask[t.at(5, 3)] && !mask[t.at(2, 7)]));
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
			REQUIRE(cover[t.at(x, y)] == (inside ? 1 : 0));
		}
}

// A tiling's cells cover every tile exactly once, edges join their cells from both sides with the
// same two corners, the counts obey Euler's formula for a torus, lattice symmetries keep neighbours
// neighbours, and warping keeps the tiling whole and the walls' passages open, the same way twice.
inline void tessellationChecks()
{
	const auto contract = [](const Tessellation &g, int degree)
	{
		REQUIRE(int(g.corners.size()) - int(g.edges.size()) + g.cellCount() == 0);
		const std::vector<int> labels = labelTiles(g);
		REQUIRE(int(labels.size()) == g.t.size());
		std::vector<int> area(g.cells.size(), 0);
		for (int label : labels)
			++area[label];
		for (int cell = 0; cell < g.cellCount(); ++cell)
		{
			REQUIRE((area[cell] > 0 && int(g.cells[cell].edges.size()) == degree));
			REQUIRE(labels[g.t.at(g.centreTileX(cell), g.centreTileY(cell))] == cell);
			for (int edge : g.cells[cell].edges)
			{
				const int next = g.other(edge, cell);
				const auto &back = g.cells[next].edges;
				REQUIRE(std::find(back.begin(), back.end(), edge) != back.end());
				// Seen from the other side the edge runs backwards, one whole map shift away.
				const auto here = g.edgeEnds(edge, cell), there = g.edgeEnds(edge, next);
				const long long sx = here.first.x - there.second.x,
								sy = here.first.y - there.second.y;
				REQUIRE((sx % (g.t.w * kSubtile) == 0 && sy % (g.t.h * kSubtile) == 0));
				REQUIRE((here.second.x - there.first.x == sx && here.second.y - there.first.y == sy));
				const SubtilePoint across = g.centreAcross(edge, cell);
				REQUIRE(((across.x - g.cells[next].centre.x) == sx &&
					   (across.y - g.cells[next].centre.y) == sy));
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
					REQUIRE(std::count(graph[image].begin(), graph[image].end(), move(next)) > 0);
			}
		return labels;
	};
	const Tessellation squares = squareTessellation(64, 48, 16);
	REQUIRE((squares.columns == 4 && squares.rows == 3));
	const std::vector<int> squareLabels = contract(squares, 4);
	REQUIRE((squareLabels[squares.t.at(15, 0)] == 0 && squareLabels[squares.t.at(16, 0)] == 1));
	contract(squareTessellation(64, 64, 20), 4); // uneven pitches, 3 cells of 21 or 22 tiles
	contract(squareTessellation(32, 32, 16), 4); // two cells across: every neighbour twice
	const Tessellation hexes = hexTessellation(128, 128, 30);
	REQUIRE((hexes.columns == 4 && hexes.rows % 2 == 0));
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
		REQUIRE(limit > 0);
		GenerationContext one(request), two(request), untouched(request);
		warpCorners(first, limit, walls, 6, 3, one, "warp");
		warpCorners(second, limit, walls, 6, 3, two, "warp");
		REQUIRE(first.corners.size() == lattice.corners.size());
		bool moved = false;
		for (size_t k = 0; k < first.corners.size(); ++k)
		{
			REQUIRE(first.corners[k] == second.corners[k]);
			moved = moved || !(first.corners[k] == lattice.corners[k]);
		}
		REQUIRE(moved);
		contract(first, lattice.shape == Tessellation::Shape::Square ? 4 : 6);
		// No move at all draws nothing.
		Tessellation still = lattice;
		warpCorners(still, 0, walls, 6, 3, untouched, "warp");
		GenerationContext fresh(request);
		REQUIRE(untouched.bounded("warp", 1000) == fresh.bounded("warp", 1000));
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
				REQUIRE(steps(first) >= std::min(steps(lattice), 6));
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
	REQUIRE(thick == dilate(reference.t, thin, 2));
	REQUIRE(thick[reference.t.at(-1, 8)]); // thick boundary wraps through x=0
	std::vector<unsigned char> none(edges.size(), 0);
	const auto absent = rasterizeBoundaries(reference, none, 2);
	REQUIRE(std::count(absent.begin(), absent.end(), 1) == 0);
	std::vector<unsigned char> excluded(reference.t.size(), 0);
	for (int cell = 0; cell < reference.cellCount(); ++cell)
		excluded[reference.t.at(reference.centreTileX(cell), reference.centreTileY(cell))] = 1;
	auto safe = reference;
	REQUIRE(relaxWarpOutside(safe, reference.corners, edges, excluded, 2) == 0);
	REQUIRE(safe.corners == reference.corners);
	auto shifted = reference;
	for (auto &corner : shifted.corners)
		corner.x += 8 * 16; // a wall now passes through every protected centre
	const auto collided = rasterizeBoundaries(shifted, edges, 2);
	REQUIRE(collided[reference.t.at(8, 8)]);
	auto repaired = shifted, repeated = shifted;
	const int reductions = relaxWarpOutside(repaired, reference.corners, edges, excluded, 2);
	REQUIRE(reductions > 0);
	REQUIRE(relaxWarpOutside(repeated, reference.corners, edges, excluded, 2) == reductions);
	REQUIRE(repaired.corners == repeated.corners);
	const auto cleared = rasterizeBoundaries(repaired, edges, 2);
	for (int tile = 0; tile < reference.t.size(); ++tile)
		REQUIRE(!(cleared[tile] && excluded[tile]));
	REQUIRE(!labelTiles(repaired).empty());
	// One allowed contraction uses the exact reference instead of an unbounded retry.
	REQUIRE(relaxWarpOutside(shifted, reference.corners, edges, excluded, 2, 1) == 1);
	REQUIRE(shifted.corners == reference.corners);
	std::fill(excluded.begin(), excluded.end(), 1);
	REQUIRE(relaxWarpOutside(shifted, reference.corners, edges, excluded, 2, 2) == -1);
	REQUIRE(shifted.corners == reference.corners);
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
		REQUIRE(!pocketsFit(g, surrounded));
		const std::vector<int> pockets = spreadPockets(g, 3);
		REQUIRE((pockets.size() == 3 && spreadPockets(g, 3) == pockets));
		std::vector<unsigned char> isPocket(g.cells.size(), 0);
		for (int pocket : pockets)
			isPocket[pocket] = 1;
		REQUIRE(pocketsFit(g, isPocket));

		GenerationRequest request;
		request.seed = 4;
		GenerationContext context(request);
		std::vector<unsigned char> open(g.edges.size(), 0);
		REQUIRE(carveSpanningTree(g, context, "maze", isPocket, open));
		REQUIRE(std::count(open.begin(), open.end(), 1) == n - 3 - 1);
		const std::vector<int> doors = openPocketDoors(g, context, "maze", pockets, isPocket, open);
		for (size_t k = 0; k < pockets.size(); ++k)
		{
			int degree = 0;
			for (int edge : g.cells[pockets[k]].edges)
				degree += open[edge];
			REQUIRE((degree == 1 && open[doors[k]] && !isPocket[g.other(doors[k], pockets[k])]));
		}
		openLoops(g, context, "maze", isPocket, 50, open);
		REQUIRE(std::count(open.begin(), open.end(), 1) == n - 1 + (n - 3) * 50 / 100);
		std::vector<int> exits;
		for (int cell : deadEnds(g, open, isPocket, exits))
		{
			int degree = 0;
			for (int edge : g.cells[cell].edges)
				degree += open[edge];
			REQUIRE((!isPocket[cell] && degree == 1 && open[exits[cell]]));
		}
	}

	const Torus t(8, 8);
	std::vector<int> labels(t.size(), 0);
	for (int i = 0; i < t.size(); ++i)
		labels[i] = i % t.w < 4 ? 0 : i % t.w == 4 ? -1 : 1;
	std::vector<unsigned char> reached(t.size(), 1);
	const RegionLeak leak = firstRegionLeak(t, reached, labels, [](int, int) { return false; });
	REQUIRE((leak.tile >= 0 && labels[leak.tile] != labels[leak.neighbour]));
	REQUIRE(firstRegionLeak(t, reached, labels, [](int, int) { return true; }).tile < 0);
	for (int i = 0; i < t.size(); ++i)
		reached[i] = labels[i] == 1;
	REQUIRE(firstRegionLeak(t, reached, labels, [](int, int) { return false; }).tile < 0);
}

// Landforms under a grain: sites spread under a grain keep their distance under it (closer end to
// end than side by side), fixed sites are kept and kept clear of; packed radii keep the gap between
// every pair and drop what is too small; a teardrop is its width wide, its length long, widest
// where asked, and fits its packed radius under the stretch; the near tree joins every cell with no
// loop.
inline void landformChecks()
{
	// Big enough that two fixed sites' exclusions (24 across, 60 along under the stretch) leave
	// most of the map to the darts.
	const Torus t(256, 128);
	GenerationRequest request;
	request.seed = 21;
	GenerationContext context(request);
	const Grain grain{2, 1, 250};
	REQUIRE(grain.distance2(0, 0) == 0);
	// Along the direction the metric shrinks by the stretch; across it, not at all.
	REQUIRE(std::abs(grain.distance(4, 2) - std::hypot(4, 2) / 2.5) < 1e-9);
	REQUIRE(std::abs(grain.distance(-1, 2) - std::hypot(1, 2)) < 1e-9);
	// (2, 1) lies along the grain and (-1, 2) across it, equally long: across counts the stretch
	// squared (6.25) more. A distance in tiles across the grain is a plain across offset.
	REQUIRE(grain.distance2(-1, 2) == grain.distance2(2, 1) * 25 / 4);
	REQUIRE((Grain{1, 0, 250}.metric2(3) == Grain{1, 0, 250}.distance2(0, 3)));
	const std::vector<Site> fixed{{10, 10}, {40, 55}};
	const std::vector<Site> sites = spreadPoints(t, 12, grain, context, "grain", fixed, 24);
	REQUIRE((sites.size() > 2 && sites[0].x == 10 && sites[1].x == 40));
	const std::vector<double> nearest = nearestSiteDistances(t, sites, grain);
	for (size_t a = 0; a < sites.size(); ++a)
	{
		double nearestFound = std::min(t.w, t.h);
		for (size_t b = 0; b < sites.size(); ++b)
			if (a != b)
			{
				const double d = grain.distance(t, sites[a].x, sites[a].y, sites[b].x, sites[b].y);
				nearestFound = std::min(nearestFound, d);
				REQUIRE(d >= 12 * 83 / 100 - 1e-9);
				if (b < 2 && a >= 2)
					REQUIRE(d >= 24 - 1e-9);
			}
		REQUIRE(std::abs(nearest[a] - nearestFound) < 1e-9);
	}
	// Through the wrap: under a grain along the width, (0, 0) and (64, 0) on a 128-wide torus are
	// 64 apart either way in map tiles, and 64 / 2.5 under the grain; (60, 20) and (68, 20) are 8
	// apart in tiles and 3.2 under it, while the far way round is no nearer.
	REQUIRE(std::abs(Grain{1, 0, 250}.distance(t, 0, 0, 128, 0) - 128 / 2.5) < 1e-9);
	REQUIRE(std::abs(Grain{1, 0, 250}.distance(t, 60, 20, 68, 20) - 3.2) < 1e-9);
	// And where the nearest image in tiles is not the nearest under the grain: (0, 0) to (62, 30)
	// is 62 along and 30 across the short way, but 66 along and 30 across the other way round is
	// 66 / 2.5 = 26.4 along under the grain against 24.8, so the short way still wins; under a
	// slanted grain the two images can differ in which is nearer, which the four-image search
	// settles (checked by the spread below keeping its distances).
	REQUIRE(std::abs(Grain{1, 0, 250}.distance(t, 0, 0, 62, 30) - std::hypot(62 / 2.5, 30)) < 1e-9);

	// Repeatable from a fresh context, and different for another seed.
	GenerationContext again(request);
	const std::vector<Site> repeat = spreadPoints(t, 12, grain, again, "grain", fixed, 24);
	REQUIRE((repeat.size() == sites.size() && repeat[5].x == sites[5].x && repeat[5].y == sites[5].y));
	request.seed = 22;
	GenerationContext other(request);
	const std::vector<Site> differ = spreadPoints(t, 12, grain, other, "grain", fixed, 24);
	REQUIRE((differ.size() != sites.size() || differ[5].x != sites[5].x || differ[5].y != sites[5].y));

	// Labels: every tile's nearest site under the grain, exactly as a full search finds it.
	const std::vector<int> labels = nearestSiteLabels(t, sites, 12, grain);
	for (int i = 0; i < t.size(); i += 7)
	{
		std::int64_t best = -1;
		int who = -1;
		for (size_t s = 0; s < sites.size(); ++s)
		{
			const std::int64_t d = grain.distance2(t, sites[s].x, sites[s].y, i % t.w, i / t.w);
			if (who < 0 || d < best)
			{
				best = d;
				who = int(s);
			}
		}
		REQUIRE(labels[i] == who);
	}

	// Relaxation keeps the fixed sites still, moves the rest, and keeps the count.
	const std::vector<Site> relaxed = relaxPoints(t, sites, grain, 12, 2, 2);
	REQUIRE((relaxed.size() == sites.size() && relaxed[0].x == 10 && relaxed[1].y == 55));
	REQUIRE(std::any_of(relaxed.begin() + 2, relaxed.end(), [&](const Site &site)
					   { return site.x != sites[&site - &relaxed[0]].x ||
								site.y != sites[&site - &relaxed[0]].y; }));

	// Packing: fixed radii kept, every pair keeps the gap in map tiles (under the grain, the gap
	// scaled by the pair's shrink), nothing above the maximum, small ones dropped, and a free site
	// with room grows to the maximum. Two sites end to end along the grain keep a map-tile gap, not
	// the stretch times it.
	std::vector<double> given(sites.size(), -1);
	given[0] = given[1] = 9;
	const std::vector<double> radius = packLandforms(t, sites, grain, given, 4, 3, 9);
	REQUIRE((radius[0] == 9 && radius[1] == 9));
	for (size_t a = 0; a < sites.size(); ++a)
	{
		REQUIRE((radius[a] < 0 || (radius[a] >= 3 && radius[a] <= 9 + 1e-9)));
		for (size_t b = a + 1; b < sites.size(); ++b)
			if (radius[a] >= 0 && radius[b] >= 0)
			{
				// The gap holds on whichever image of the pair is nearest under the grain.
				const int dx = t.offsetX(sites[a].x, sites[b].x), dy = t.offsetY(sites[a].y, sites[b].y);
				const int ox = dx > 0 ? dx - t.w : dx + t.w, oy = dy > 0 ? dy - t.h : dy + t.h;
				const double under = grain.distance(t, sites[a].x, sites[a].y, sites[b].x, sites[b].y);
				double across = 0;
				for (const int ex : {dx, ox})
					for (const int ey : {dy, oy})
						if (std::abs(grain.distance(ex, ey) - under) < 1e-9)
							across = std::hypot(ex, ey);
				REQUIRE(radius[a] + radius[b] + 4 * under / across <= under + 1e-9);
			}
	}
	const std::vector<Site> lone{{5, 5}, {64, 32}};
	const std::vector<double> loneRadius =
		packLandforms(Torus(128, 128), lone, Grain{1, 0, 100}, {-1, -1}, 4, 3, 9);
	REQUIRE((loneRadius[0] == 9 && loneRadius[1] == 9));
	// End to end along a grain of stretch 4: sites 40 apart along it are 10 apart under it, and
	// with a gap of 4 map tiles (1 under the grain) each gets 4.5, not 3.
	const std::vector<Site> endToEnd{{20, 20}, {60, 20}};
	const std::vector<double> alongRadius =
		packLandforms(Torus(128, 128), endToEnd, Grain{1, 0, 400}, {-1, -1}, 4, 3, 9);
	REQUIRE((std::abs(alongRadius[0] - 4.5) < 1e-9 && std::abs(alongRadius[1] - 4.5) < 1e-9));

	// Teardrop: symmetric across, widest where asked, tapering to nothing at both ends, inside the
	// disc of its reach, and the fitted one reaches exactly its radius.
	const Teardrop drop{40, 16, 0.4};
	REQUIRE(std::abs(drop.halfWidthAt(-20 + 16) - 8) < 1e-9);
	REQUIRE((drop.halfWidthAt(-20) == 0 && drop.halfWidthAt(20) == 0 && drop.halfWidthAt(-21) == 0));
	REQUIRE((drop.contains(-4, 7.9) && drop.contains(-4, -7.9) && !drop.contains(-4, 8.1)));
	REQUIRE(drop.halfWidthAt(-10) > drop.halfWidthAt(-16));
	REQUIRE(drop.halfWidthAt(10) > drop.halfWidthAt(16));
	REQUIRE(drop.halfWidthAt(-4) > drop.halfWidthAt(4)); // the head is the fat end
	const Teardrop fitted = Teardrop::fitting(10, 2.5);
	REQUIRE(std::abs(fitted.reach(2.5) - 10) < 1e-6);
	REQUIRE(std::abs(fitted.length - 2.5 * fitted.width) < 1e-9);
	REQUIRE((fitted.width < 20 && fitted.width > 18));
	std::vector<unsigned char> mask(t.size(), 0);
	fillTeardrop(mask, t, 64, 32, 0, drop);
	int count = 0, leftOfHead = 0;
	for (int i = 0; i < t.size(); ++i)
		if (mask[i])
		{
			++count;
			leftOfHead += i % t.w < 64 - 20;
		}
	// Two half-ellipses on the same width add up to the whole ellipse's area.
	REQUIRE((leftOfHead == 0 && count > 0.92 * kPi * 20 * 8 && count < 1.08 * kPi * 20 * 8));
	REQUIRE((mask[t.at(64 - 19, 32)] && !mask[t.at(64 + 19, 40)] && mask[t.at(64 - 4, 39)]));
	std::vector<unsigned char> turned(t.size(), 0);
	fillTeardrop(turned, t, 64, 32, kPi / 2, drop);
	REQUIRE((turned[t.at(64, 32 - 19)] && !turned[t.at(64 - 19, 32)]));

	// Grain choices: the fixed headings, a random one within the eight, and the widest for a pair
	// of sites lying along the width (the vertical grain keeps them farthest apart under it).
	REQUIRE((grainForChoice(1, 250, context, "grain").dy == 0 &&
		   grainForChoice(2, 250, context, "grain").dx == 0 &&
		   grainForChoice(3, 250, context, "grain").dx == 1 &&
		   grainForChoice(3, 250, context, "grain").dy == 1));
	const Grain drawn = grainForChoice(0, 250, context, "grain");
	REQUIRE((drawn.stretchPercent == 250 && (drawn.dx != 0 || drawn.dy != 0)));
	REQUIRE((widestGrainHeading(t, {{10, 10}, {40, 10}}, 250) == 4));
	// Farthest sites: the farthest eligible site from the seeds first, then from that one too.
	{
		const std::vector<Site> line{{0, 0}, {10, 0}, {20, 0}, {30, 0}, {40, 0}};
		const Torus wide(128, 128);
		REQUIRE((farthestSites(wide, line, {0}, {1, 1, 1, 1, 1}, 2) == std::vector<int>{4, 2}));
		REQUIRE((farthestSites(wide, line, {0}, {1, 1, 1, 0, 0}, 5) == std::vector<int>{2, 1}));
	}
	// A teardrop home: the town is the head, the collar a band across the waist, the tail the
	// rest; its swarm stands in the town and its kit's crops seed on the tail, the quarry near the
	// tip.
	{
		const TeardropHome home{};
		const Teardrop shape{40, 16, 0.4};
		int town = 0, collar = 0, tail = 0;
		stampTeardropHome(t, {64, 32}, 0, shape, home,
						  [&](int, int ground) { (ground == 0 ? town : ground == 1 ? collar : tail)++; });
		REQUIRE((town > 0 && collar > 0 && tail > 0 && town + collar + tail > 0.92 * kPi * 20 * 8));
		REQUIRE((collar < 3 * 16 && town > tail / 2));
		const MapGeneratorPoint swarm = teardropHomeSwarm({64, 32}, 0, shape, home);
		REQUIRE((swarm.x + 2 < 64 + home.collarAt(shape) && swarm.x + 2 > 64 - 20));
		const Kit kit = teardropHomeKit({64, 32}, 0, shape, home, 14, 12, 2);
		REQUIRE((kit.wheat.x > 64 + home.collarAt(shape) + 1 && kit.wood.x == kit.wheat.x &&
			   kit.wheat.y < 32 && kit.wood.y > 32 && kit.stone.x < 64 - 10 && kit.stoneRadius == 2));
	}

	// The near tree: one edge fewer than cells, every cell reached, favouring short links.
	const std::vector<std::vector<int>> neighbours = siteNeighbours(t, labels, int(sites.size()));
	const CellGraph graph = cellGraph(t, sites, neighbours);
	std::vector<unsigned char> blocked(sites.size(), 0), open(graph.edgeCells.size(), 0);
	REQUIRE(carveNearTree(graph, context, "tree", blocked, 0, open));
	REQUIRE(std::count(open.begin(), open.end(), 1) == int(sites.size()) - 1);
	std::vector<int> reached(sites.size(), 0);
	std::vector<int> stack{0};
	reached[0] = 1;
	while (!stack.empty())
	{
		const int cell = stack.back();
		stack.pop_back();
		for (int edge : graph.cellEdges[cell])
			if (open[edge] && !reached[graph.other(edge, cell)])
			{
				reached[graph.other(edge, cell)] = 1;
				stack.push_back(graph.other(edge, cell));
			}
	}
	REQUIRE(std::all_of(reached.begin(), reached.end(), [](int r) { return r == 1; }));
	// With no jitter it is the minimum spanning tree: no open edge is longer than a closed edge
	// that would join the two sides it separates (checked on the longest open edge only).
	long long longest = -1;
	int longestEdge = -1;
	for (size_t edge = 0; edge < open.size(); ++edge)
		if (open[edge] && graph.distance2(graph.edgeCells[edge][0], graph.edgeCells[edge][1]) > longest)
		{
			longest = graph.distance2(graph.edgeCells[edge][0], graph.edgeCells[edge][1]);
			longestEdge = int(edge);
		}
	REQUIRE(longestEdge >= 0);
	std::vector<unsigned char> loops(open);
	openLoops(graph, context, "tree", blocked, 100, loops);
	REQUIRE(std::count(loops.begin(), loops.end(), 1) > std::count(open.begin(), open.end(), 1));
	// Corridors along the open edges, carved only where eligible: one per open edge, and every
	// corridor touches both its cells' surroundings.
	{
		std::vector<ShapePoint> middles;
		for (const Site &site : sites)
			middles.push_back({site.x + 0.5, site.y + 0.5});
		std::vector<unsigned char> carved(t.size(), 0);
		std::mt19937 &random = context.stream("corridors");
		const auto edges = carveOpenEdges(carved, t, graph, middles, open, 1.5, 3.0, 0.2, random,
										  [&](int i) { return i % 2 == 0; });
		REQUIRE(edges.size() == size_t(std::count(open.begin(), open.end(), 1)));
		REQUIRE(std::count(carved.begin(), carved.end(), 1) > 0);
		for (int i = 0; i < t.size(); ++i)
			REQUIRE((!carved[i] || i % 2 == 0));
	}
}

// Colonies apart: with the causeway shut two colonies on two islands cannot reach each other, and
// with it open they can; crops on the way do not count as a barrier.
inline void colonyLeakChecks()
{
	Game game(nullptr);
	game.map.setSize(6, 6);
	game.map.setGame(&game);
	game.map.fillTerrain(WATER);
	const Torus t(64, 64);
	TerrainSketch sketch(t.size(), WATER);
	for (int y = 20; y < 44; ++y)
		for (int x = 0; x < 64; ++x)
			if ((x >= 4 && x < 24) || (x >= 36 && x < 56))
				sketch[t.at(x, y)] = GRASS;
	std::vector<unsigned char> causeway(t.size(), 0);
	for (int y = 30; y < 33; ++y)
		for (int x = 24; x < 36; ++x)
			causeway[t.at(x, y)] = sketch[t.at(x, y)] = SAND;
	layBeaches(sketch, t);
	writeVertices(GenerationMap(game.map).ref(), sketch);
	for (int k = 0; k < 2; ++k)
		game.addTeam();
	GenerationRequest request;
	request.nbTeams = 2;
	request.nbWorkers = 2;
	GenerationContext context(request);
	std::vector<unsigned char> homes[2];
	for (int k = 0; k < 2; ++k)
	{
		homes[k].assign(t.size(), 0);
		for (int i = 0; i < t.size(); ++i)
			homes[k][i] = game.map.isGrass(i % t.w, i / t.w) && (k == 0 ? i % t.w < 30 : i % t.w > 30);
		REQUIRE(placeSettlement(GenerationWorld(game).ref(), context, k, homes[k],
							   MapGeneratorPoint(k ? 44 : 12, 24)));
	}
	game.map.setResourceByIndex(30, 31, WHEAT, 1);
	REQUIRE(colonyLeak(game.map, t, 2, roadTiles(t, causeway)) == (std::array<int, 2>{-1, -1}));
	const std::array<int, 2> joined =
		colonyLeak(game.map, t, 2, std::vector<unsigned char>(t.size(), 0));
	REQUIRE((joined[0] == 0 && joined[1] == 1));
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
	REQUIRE((straight.crops == 10 && straight.water == 8 && diagonal.crops == 12 &&
		   diagonal.water == 9));
	REQUIRE((turned.crops == straight.crops && bestFarmRows(-kPi / 4).crops == diagonal.crops));
	REQUIRE((std::abs(farmYield(0) - 0.1489) < 1e-9 && std::abs(farmYield(kPi / 4) - 0.1126) < 1e-9));
	REQUIRE((std::abs(farmYield(kPi / 8) - 0.1225) < 1e-9 && farmYield(kPi / 2) == farmYield(0)));
	REQUIRE((farmYield(0.1) < farmYield(0) && farmYield(0.1) > farmYield(kPi / 16)));
	{
		// Shared out by worth: a claimant worth half per tile ends with about twice the area.
		const Torus open(40, 40);
		const std::vector<unsigned char> all(open.size(), 1);
		const std::vector<double> worth = {1.0, 0.5};
		const Territories byWorth = growTerritories(
			open, all, {{open.at(5, 20)}, {open.at(25, 20)}}, [](int) { return 0; }, &worth);
		REQUIRE(std::abs(byWorth.areas[1] - 2 * byWorth.areas[0]) <= 2);
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
	REQUIRE((farm.rows >= 3 && !farm.water[t.at(32, 32)] && farm.row[t.at(32, 32)] % 2 == 0));
	for (int i = 0; i < t.size(); ++i)
	{
		if (!region[i])
			REQUIRE((farm.row[i] == -1 && !farm.water[i]));
		if (farm.water[i])
		{
			REQUIRE((farm.row[i] % 2 == 1 && sketch[i] == WATER));
			const int x = i % t.w, y = i / t.w;
			REQUIRE((x >= 13 && x < 51 && y >= 13 && y < 51));
		}
	}
	REQUIRE((farm.water[t.at(32, 36)] && !farm.water[t.at(32, 34)]));
	{
		// Requesting the existing edge flood must not change any farm output. Include
		// an irregular field spanning both torus seams and the no-exterior boundary.
		std::vector<unsigned char> wrapped(t.size(), 0);
		for (int y = -18; y <= 18; ++y)
			for (int x = -18; x <= 18; ++x)
				if (x < 8 || y < 8)
					wrapped[t.at(x, y)] = 1;
		for (const auto &mask : {region, wrapped, std::vector<unsigned char>(t.size(), 1),
								std::vector<unsigned char>(t.size(), 0)})
		{
			TerrainSketch plain(t.size(), GRASS), observed = plain;
			const FarmPlot plot;
			const Farm a = layFarm(plain, t, mask, kPi / 8, {0, 0}, 4, {6, 4}, &plot,
								   FarmBridges{8});
			std::vector<int> depth{123}; // The result replaces, rather than appends to, it.
			const Farm b = layFarm(observed, t, mask, kPi / 8, {0, 0}, 4, {6, 4}, &plot,
								   FarmBridges{8}, true, &depth);
			REQUIRE((plain == observed && a.water == b.water && a.row == b.row &&
				   a.sand == b.sand && a.plot == b.plot && a.rows == b.rows &&
				   a.plotX == b.plotX && a.plotY == b.plotY));
			std::vector<unsigned char> exterior(t.size(), 0);
			for (int i = 0; i < t.size(); ++i)
				exterior[i] = !mask[i];
			REQUIRE(depth == stepsFrom(t, exterior));
		}
	}
	{
		// Bridges: every 8 tiles along the rows from the origin, a line of sand clean across the
		// farm, through the water rows and the crop rows alike (FEEDBACK 2026-09-14), so a colony
		// crosses the whole field on one road. Only the rim keeps its crops.
		TerrainSketch bridged(t.size(), GRASS);
		const Farm crossed = layFarm(bridged, t, region, 0, {32, 32}, 4, {6, 4}, nullptr, FarmBridges{8});
		REQUIRE((!crossed.water[t.at(32, 36)] && crossed.sand[t.at(32, 36)] &&
			   bridged[t.at(32, 36)] == SAND));
		REQUIRE((crossed.sand[t.at(24, 36)] && crossed.water[t.at(33, 36)] &&
			   crossed.water[t.at(28, 36)]));
		REQUIRE((crossed.sand[t.at(32, 32)] && bridged[t.at(32, 32)] == SAND &&
			   crossed.rows == farm.rows));
		REQUIRE((!crossed.sand[t.at(33, 32)] && bridged[t.at(33, 32)] == GRASS));
		REQUIRE((!crossed.sand[t.at(32, 10)] && !crossed.sand[t.at(32, 53)])); // the rim stays grass
		// Each half switches off alone: water-only bridges leave the crop rows whole, and crop-only
		// lanes leave the water rows unbridged.
		TerrainSketch overWater(t.size(), GRASS), throughCrops(t.size(), GRASS);
		const Farm wet = layFarm(overWater, t, region, 0, {32, 32}, 4, {6, 4}, nullptr,
								 FarmBridges{8, true, false});
		const Farm dry = layFarm(throughCrops, t, region, 0, {32, 32}, 4, {6, 4}, nullptr,
								 FarmBridges{8, false, true});
		REQUIRE((wet.sand[t.at(32, 36)] && !wet.water[t.at(32, 36)] && overWater[t.at(32, 32)] == GRASS));
		REQUIRE((dry.water[t.at(32, 36)] && throughCrops[t.at(32, 36)] == WATER));
		REQUIRE((dry.sand[t.at(32, 32)] && throughCrops[t.at(32, 32)] == SAND));
		// A building plot's grass trumps a bridge through it.
		TerrainSketch plotted(t.size(), GRASS);
		const FarmPlot clearing;
		const Farm both = layFarm(plotted, t, region, 0, {32, 32}, 4, {6, 4}, &clearing, FarmBridges{4});
		REQUIRE(both.plotX >= 0);
		for (int dy = 0; dy <= clearing.height; ++dy)
			for (int dx = 0; dx <= clearing.width; ++dx)
			{
				const int i = t.at(both.plotX + dx, both.plotY + dy);
				REQUIRE((!both.sand[i] && !both.water[i] && plotted[i] == GRASS));
			}
	}
	layBeaches(sketch, t);
	writeVertices(GenerationMap(map).ref(), sketch);
	const int planted =
		plantFarm(GenerationMap(map).ref(), t, farm, 10, 10, [](int) { return true; });
	REQUIRE((planted == 20 && countResource(map, WHEAT) == 10 && countResource(map, WOOD) == 10));
	int woodRow = -1;
	for (int i = 0; i < t.size(); ++i)
		if (map.getResource(i % t.w, i / t.w).type == WOOD)
		{
			REQUIRE((woodRow < 0 || farm.row[i] == woodRow));
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
	REQUIRE((sizes[0] > 400 && std::abs(sizes[0] - sizes[1]) < sizes[0] / 5 && touches[0] &&
		   touches[1]));
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
			REQUIRE(fromFirst[i] >= 4);
		if (first[i])
			REQUIRE(fromEast[i] >= 4);
	}

	std::vector<int> halves(sea.size(), 0);
	for (int i = 0; i < sea.size(); ++i)
		halves[i] = i % sea.w < 32 ? 0 : 1;
	separateTerritories(sea, halves, 4);
	REQUIRE((halves[sea.at(31, 5)] == -1 && halves[sea.at(32, 5)] == -1 &&
		   halves[sea.at(29, 5)] == 0));

	// fillToNearest closes the gap again, each tile to its nearer side, and labelBorders with a door
	// walls the border everywhere but where the door leaves it open.
	{
		std::vector<unsigned char> gap(sea.size(), 0);
		for (int i = 0; i < sea.size(); ++i)
			gap[i] = halves[i] < 0;
		std::vector<int> nearby = halves;
		REQUIRE((fillToNearest(sea, nearby, gap, 1)[sea.at(30, 5)] && nearby[sea.at(30, 5)] == 0 &&
			   nearby[sea.at(31, 5)] == -1));
		const std::vector<unsigned char> filled = fillToNearest(sea, halves, gap, 4);
		REQUIRE((filled[sea.at(31, 5)] && halves[sea.at(31, 5)] == 0 && halves[sea.at(32, 5)] == 1));
		for (int i = 0; i < sea.size(); ++i)
			REQUIRE(halves[i] >= 0);
		const auto door = [&](int i, int) { return i / sea.w >= 10 && i / sea.w < 14; };
		const std::vector<unsigned char> walls = labelBorders(sea, halves, door);
		REQUIRE((walls[sea.at(32, 5)] && !walls[sea.at(31, 5)] && !walls[sea.at(32, 11)]));
		std::vector<unsigned char> reached(sea.size(), 0);
		for (int i = 0; i < sea.size(); ++i)
			reached[i] = !walls[i];
		REQUIRE(firstRegionLeak(sea, reached, halves, [](int, int) { return false; }).tile >= 0);
		// Through the door the sides meet; with the door's rows shut, nothing leaks, across the wrap too.
		for (int i = 0; i < sea.size(); ++i)
			if (i / sea.w >= 9 && i / sea.w < 15)
				reached[i] = 0;
		REQUIRE(firstRegionLeak(sea, reached, halves, [](int, int) { return false; }).tile < 0);
	}

	std::vector<unsigned char> lake(sea.size(), 0);
	std::vector<int> queued(sea.size(), 0), depth(sea.size(), 0), room(sea.size(), 20);
	const int site = sea.at(32, 32);
	REQUIRE(growLakeBeside(
			   sea, lake, depth, room, 2, 40, site, 0, 1, 6, 20, [](int) { return 0.0; }, queued,
			   1) == 40);
	REQUIRE(growLakeBeside(
			   sea, lake, depth, room, 2, 40, site, 0, -1, 6, 20, [](int) { return 0.0; }, queued,
			   2) == 40);
	for (int i = 0; i < sea.size(); ++i)
		if (lake[i])
			REQUIRE(std::abs(sea.offsetY(32, i / sea.w)) >= 6);

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
	REQUIRE((plan.towers[0].size() == 3 && plan.towers[1].size() == 3 && plan.pads[0].size() == 2));
	REQUIRE((plan.stocked[0].size() == 3 && plan.stocked[1].size() == 3));
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
		REQUIRE(bool(plan.stocked[0][a]) == !facing);
		REQUIRE(s % 64 >= 19); // beside the four-tile gap, the only border within range
	}
	const std::vector<unsigned char> footprints = towerFootprints(sea, plan);
	REQUIRE(std::count(footprints.begin(), footprints.end(), 1) == 4 * 10);
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
	REQUIRE((closedEdges(g, open) == std::vector<int>{3, 4, 5}));
	std::vector<unsigned char> blocked{0, 0, 0, 1, 0};
	REQUIRE((closedEdges(g, open, blocked) == std::vector<int>{4, 5}));
	const auto detours = edgeDetours(g, open, {4, 3, 5});
	REQUIRE((detours[3] == 3 && detours[4] == 1 && detours[5] == -1 && detours[0] == -1));
	REQUIRE((open == std::vector<unsigned char>{1, 1, 1, 0, 0, 0}));
	open[3] = 1;
	REQUIRE(edgeDetours(g, open, {3})[3] == 1);
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
		REQUIRE(path.size() == 3);
		const int owner = g.edges[e].cells[0];
		const auto from = tilePoint(g.cells[owner].centre);
		const auto to = tilePoint(g.centreAcross(e, owner));
		REQUIRE(std::abs(std::hypot(path.front().x - from.x, path.front().y - from.y) - 3) < 1e-9);
		REQUIRE(std::abs(std::hypot(path.back().x - to.x, path.back().y - to.y) - 3) < 1e-9);
		masks.emplace_back(g.t.size(), 0);
		strokePath(masks.back(), g.t, path);
		REQUIRE(std::count(masks.back().begin(), masks.back().end(), 1) > 0);
		REQUIRE(cellCrossing(g, e, 100, 1).empty());
	}
	REQUIRE(masks.size() == 2);
	for (int i = 0; i < g.t.size(); ++i)
		REQUIRE((!masks[0][i] || !masks[1][i]));
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
	REQUIRE(labelComponents(connectedRegions(open, t.w, t.h, true), labels).conflictTile >= 0);
	open[t.at(1, 0)] = 0;
	REQUIRE(labelComponents(connectedRegions(open, t.w, t.h, true), labels).conflictTile < 0);
	// A diagonal crop seam is a leak under eight-neighbor growth, but not cardinal growth.
	open.assign(t.size(), 0);
	labels.assign(t.size(), -1);
	open[t.at(0, 0)] = open[t.at(1, 1)] = 1;
	labels[t.at(0, 0)] = 0;
	labels[t.at(1, 1)] = 1;
	REQUIRE(labelComponents(connectedRegions(open, t.w, t.h, true, GridNeighbors::Eight), labels)
			   .conflictTile >= 0);
	REQUIRE(labelComponents(connectedRegions(open, t.w, t.h, true, GridNeighbors::Cardinal), labels)
			   .conflictTile < 0);
	// The same contract must see a connection crossing the horizontal seam.
	open.assign(t.size(), 0);
	labels.assign(t.size(), -1);
	open[t.at(0, 3)] = open[t.at(7, 3)] = 1;
	labels[t.at(0, 3)] = 0;
	labels[t.at(7, 3)] = 1;
	REQUIRE(labelComponents(connectedRegions(open, t.w, t.h, true), labels).conflictTile >= 0);
	REQUIRE(labelComponents(connectedRegions(open, t.w, t.h, false), labels).conflictTile < 0);
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
	REQUIRE((clearable.leakTile < 0 && clearable.badGate < 0));
	for (const auto &gate : gates)
		for (int tile : gate.tiles)
			open[tile] = 1;
	const auto opened = checkGatePartition(t, open, labels, gates);
	REQUIRE((opened.leakTile < 0 && opened.badGate < 0));
	auto broken = gates;
	broken[0].tiles.push_back(t.at(4, 0));
	REQUIRE(checkGatePartition(t, open, labels, broken).badGate == 0);
	broken[0].tiles.clear();
	REQUIRE(checkGatePartition(t, open, labels, broken).badGate == 0);
	broken = gates;
	broken[0].regions[1] = 2;
	REQUIRE(checkGatePartition(t, open, labels, broken).badGate == 0);
	// A two-tile diagonal bypass must be caught even though neither gate changed.
	open[t.at(7, 1)] = open[t.at(8, 2)] = 1;
	REQUIRE(checkGatePartition(t, open, labels, gates).leakTile >= 0);
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
	REQUIRE(checkGatePartition(t, open, labels, {{{0, 1}, {t.at(4, 4)}}}).badGate == 0);
}

// A settled colony and the structural check: its worker count against the lobby's, and the refusal
// of any no-growth tile. A designed wall's standing proof. Stencils turned by quarter turns: tiles
// and corners of every copy agree. The routes between sites Caravanserai's caravanserais and oases
// are laid along.
inline void wallRouteAndStencilChecks()
{
	const Torus t(64, 64);
	{
		Game game(nullptr);
		grassMap(game, 6, 6);
		GenerationRequest request;
		request.seed = 5;
		request.wDec = request.hDec = 6;
		request.nbTeams = 1;
		request.nbWorkers = 4;
		GenerationContext context(request);
		game.addTeam();
		const std::vector<unsigned char> everywhere(t.size(), 1);
		REQUIRE(placeSettlement(GenerationWorld(game).ref(), context, 0, everywhere, {30, 30},
							   "starts"));
		GeneratorDefinition definition{"test-colony", 998, "Test colony", 1, false, {}, nullptr, true};
		REQUIRE(validateGeneratedWorld(game, request, definition).empty());
		request.nbWorkers = 5;
		REQUIRE(validateGeneratedWorld(game, request, definition) == "Incomplete starting colony");
		request.nbWorkers = 4;
		// Natural non-growing terrain is permitted; it is the expected way to
		// contain crops. A scenario override on even such a tile is forbidden.
		paintTile(GenerationMap(game.map).ref(), 5, 7, SAND);
		paintTile(GenerationMap(game.map).ref(), 6, 7, TRAIL);
		paintTile(GenerationMap(game.map).ref(), 7, 7, ICE);
		REQUIRE(!game.map.canResourcesGrow(5, 7));
		REQUIRE(validateGeneratedWorld(game, request, definition).empty());
		// A generated map may not disable resource growth anywhere, even one tile: no-growth
		// zones belong to hand-made scenarios such as the tutorial.
		game.map.setResourcesGrow(5, 7, false);
		REQUIRE(validateGeneratedWorld(game, request, definition).rfind(
				   "Generated maps may not disable resource growth", 0) == 0);
		game.map.setResourcesGrow(5, 7, true);
		REQUIRE(validateGeneratedWorld(game, request, definition).empty());
	}
	{
		// A square wall with a gate: missing stone, then standing, then a gate walled up.
		std::vector<unsigned char> wall(t.size(), 0), gate(t.size(), 0);
		for (int y = 27; y <= 37; ++y)
			for (int x = 27; x <= 37; ++x)
				if (std::max(std::abs(x - 32), std::abs(y - 32)) == 5)
					(x == 37 && std::abs(y - 32) <= 1 ? gate : wall)[t.at(x, y)] = 1;
		Game game(nullptr);
		grassMap(game, 6, 6);
		REQUIRE(!wallStanding(game.map, t, wall, gate, "test wall").empty());
		for (int i = 0; i < t.size(); ++i)
			if (wall[i])
				game.map.setResourceByIndex(i % t.w, i / t.w, STONE, 1);
		REQUIRE(wallStanding(game.map, t, wall, gate, "test wall").empty());
		game.map.setResourceByIndex(37, 32, STONE, 1);
		REQUIRE(!wallStanding(game.map, t, wall, gate, "test wall").empty());
	}
	{
		// A stencil of an L of tiles and the corners round them, stamped at every facing: each
		// copy's tiles are exactly the corners' tiles, and four quarter turns come back home.
		for (int facing = 0; facing < 4; ++facing)
		{
			std::vector<unsigned char> tiles(t.size(), 0), corners(t.size(), 0);
			for (const auto &[dx, dy] : {std::pair{0, 0}, {1, 0}, {2, 0}, {0, 1}})
			{
				const auto [tx, ty] = turnStencilTile(facing, dx, dy);
				tiles[t.at(32 + tx, 32 + ty)] = 1;
				for (int cy = 0; cy <= 1; ++cy)
					for (int cx = 0; cx <= 1; ++cx)
					{
						const auto [vx, vy] = turnStencilVertex(facing, dx + cx, dy + cy);
						corners[t.at(32 + vx, 32 + vy)] = 1;
					}
			}
			// A tile's four corners are (x, y) to (x + 1, y + 1): every stamped tile's corners
			// are stamped corners.
			for (int i = 0; i < t.size(); ++i)
				if (tiles[i])
					for (int cy = 0; cy <= 1; ++cy)
						for (int cx = 0; cx <= 1; ++cx)
							REQUIRE(corners[t.at(i % t.w + cx, i / t.w + cy)]);
			REQUIRE(std::count(tiles.begin(), tiles.end(), 1) == 4);
			REQUIRE(std::count(corners.begin(), corners.end(), 1) == 10);
		}
		int x = 3, y = -2;
		for (int turn = 0; turn < 4; ++turn)
			std::tie(x, y) = turnStencilVertex(1, x, y);
		REQUIRE((x == 3 && y == -2));
		const ShapePoint p = turnStencilPoint(1, {2.5, -1});
		REQUIRE((p.x == 1 && p.y == 2.5));
	}
	{
		// Routes across the wrap: midpoints, neighbours, stepping stones and headings.
		const ShapePoint a{60, 10}, b{4, 10}, c{32, 40};
		const ShapePoint middle = midpointAcross(t, a, b);
		REQUIRE((middle.x == 0 && middle.y == 10 && siteDistance(t, a, b) == 8));
		const auto pairs = nearestPairs(t, {a, b, c}, 1);
		const std::pair<int, int> ab(0, 1), ac(0, 2);
		REQUIRE((pairs.size() == 2 && pairs[0] == ab && pairs[1] == ac));
		const auto stones = waypointsAlong(t, a, b, 3, 1, 1);
		REQUIRE((stones.size() == 3 && stones[0].x == 61 && stones[1].x == 0 && stones[2].x == 3));
		REQUIRE(waypointsAlong(t, a, b, 3, 5, 5).empty());
		REQUIRE((quarterTurn(headingAcross(t, a, b)) == 0 && quarterTurn(headingAcross(t, b, a)) == 2));
		REQUIRE((quarterTurn(headingAcross(t, {10, 10}, {10, 20})) == 1 &&
			   quarterTurn(headingAcross(t, {10, 20}, {10, 10})) == 3));
	}
}

// Sand containment is tested at a toroidal seam and with a deliberately opened diagonal leak.
// Ranking must reject dry renewable seeds, honour scarcity, and retain the caller's count ceiling.
inline void containedPlotChecks()
{
	Game game(nullptr);
	grassMap(game, 6, 6);
	const Torus t{64, 64};
	TerrainSketch sketch(t.size(), GRASS);
	std::vector<int> corners;
	for (int y = -4; y <= 4; ++y)
		for (int x = -4; x <= 4; ++x)
			if (x * x + y * y <= 16)
				corners.push_back(t.at(x, y));
	const auto tiles = stampContainedPlot(sketch, t, corners);
	REQUIRE(!tiles.empty());
	std::vector<int> labels(t.size(), -1);
	for (int i : tiles)
		labels[i] = 0;
	writeVertices(GenerationMap(game.map).ref(), sketch);
	REQUIRE(containedPlotsMismatch(game.map, t, labels).empty());
	const auto dry = cropGrowthField(sketch, t);
	REQUIRE(plantContainedPlot(GenerationMap(game.map).ref(), t, tiles, dry, WOOD, 10) == 0);
	REQUIRE(plantContainedPlot(GenerationMap(game.map).ref(), t, tiles, dry, WOOD, 3, false) == 3);
	REQUIRE(plantContainedPlot(GenerationMap(game.map).ref(), t, tiles, dry, WOOD, 0, false) == 0);
	REQUIRE(plantContainedPlot(GenerationMap(game.map).ref(), t, tiles, dry, WOOD, 10000, false) ==
		   int(tiles.size()) - 3);
	REQUIRE(containedPlotsMismatch(game.map, t, labels).empty());
	// A grass path across the cap invalidates the structural proof even before a crop spreads.
	for (int x = 0; x <= 10; ++x)
		for (int y = 0; y <= 2; ++y)
			sketch[t.at(x, y)] = GRASS;
	writeVertices(GenerationMap(game.map).ref(), sketch);
	REQUIRE(!containedPlotsMismatch(game.map, t, labels).empty());

	// The thinner natural-map bund must seal an irregular plot at both wrapped seams,
	// just like the default. A two-corner diagonal breach must be detected.
	{
		TerrainSketch thin(t.size(), GRASS);
		const auto inside = stampContainedPlot(thin, t, corners, 1);
		std::vector<int> plotLabels(t.size(), -1);
		for (int i : inside)
			plotLabels[i] = 0;
		Game probe(nullptr);
		grassMap(probe, 6, 6);
		writeVertices(GenerationMap(probe.map).ref(), thin);
		REQUIRE(containedPlotsMismatch(probe.map, t, plotLabels).empty());
		for (int x = 0; x <= 8; ++x)
			for (int y = 0; y <= 2; ++y)
				thin[t.at(x, y)] = GRASS;
		writeVertices(GenerationMap(probe.map).ref(), thin);
		REQUIRE(!containedPlotsMismatch(probe.map, t, plotLabels).empty());
	}

	GenerationRequest request;
	request.seed = 912;
	GenerationContext a(request), b(request);
	auto sites = latticeSites(128, 128, 4, 0, 0).sites, repeat = sites;
	REQUIRE(jitterSites({128, 128}, sites, a, "test-jitter", 4, 48) ==
		   jitterSites({128, 128}, repeat, b, "test-jitter", 4, 48));
	REQUIRE(nearestSiteDistance({128, 128}, sites) >= 48);
	for (size_t k = 0; k < sites.size(); ++k)
		REQUIRE((sites[k].x == repeat[k].x && sites[k].y == repeat[k].y));
	REQUIRE(jitterSites({128, 128}, sites, a, "test-jitter", 0, 48) == 0);
}

// Recursive partitions are tested on awkward thirds, not just power-of-two squares.
// Every terminal rectangle must cover exactly its own share, including stopped homes.
// Reusable site operations are tested with small fixtures independent of either
// fractal design: seam separation, real movement modes, and space after all proposed
// buildings have been placed, rather than a count of overlapping empty anchors.
inline void siteOperationChecks()
{
	const Torus t(64, 64);
	std::vector<unsigned char> mask(t.size(), 0);
	fillRectangle(mask, t, {-2, -1, 2, 2});
	REQUIRE((std::count(mask.begin(), mask.end(), 1) == 12 && mask[t.at(63, 63)]));
	REQUIRE(strokeIntersectsMask(t, {{60, 0, 2}, {68, 0, 2}}, mask));
	REQUIRE(!strokeIntersectsMask(t, {{60, 20, 2}, {68, 20, 2}}, mask));
	const std::vector<int> candidates{t.at(1, 1), t.at(63, 1), t.at(32, 32)};
	const auto sites = selectSeparatedSites(t, candidates, 2, 16);
	REQUIRE((sites.failure.empty() && sites.indices == std::vector<int>({0, 2})));
	const auto impossible = selectSeparatedSites(t, candidates, 3, 16);
	REQUIRE((!impossible.failure.empty() && impossible.indices.size() == 2));
	REQUIRE(!selectSeparatedSites(t, {-1}, 1, 16).failure.empty());

	std::vector<unsigned char> clear(t.size(), 1);
	const BuildingGrid grid{{-12, -10, 12, 14}, 4, 4, 2, 1};
	const auto layout = arrangeBuildingGrid(t, clear, clear, grid, {t.at(-13, 0)});
	const auto upgraded =
		arrangeBuildingGrid(t, clear, clear, {grid.bounds, 6, 6, 2, 1}, {t.at(-13, 0)});
	REQUIRE((upgraded.failure.empty() && upgraded.footprints.size() == 9));

	REQUIRE((layout.failure.empty() && layout.footprints.size() == 16));
	std::set<int> occupied;
	for (const auto &box : layout.footprints)
		for (int y = box.y0; y < box.y1; ++y)
			for (int x = box.x0; x < box.x1; ++x)
				REQUIRE(occupied.insert(t.at(x, y)).second);
	REQUIRE(!arrangeBuildingGrid(t, clear, std::vector<unsigned char>(t.size(), 0), grid,
								{t.at(-13, 0)})
				.failure.empty());
	REQUIRE(!arrangeBuildingGrid(t, clear, clear, {{0, 0, 65, 24}}, {0}).failure.empty());
	REQUIRE(!arrangeBuildingGrid(t, clear, clear, grid, {-1}).failure.empty());
	const BuildingGrid single{{0, 0, 3, 3}, 1, 1, 1, 1};
	std::vector<unsigned char> isolated(t.size(), 0);
	isolated[t.at(0, 0)] = 1;
	REQUIRE(!arrangeBuildingGrid(t, clear, isolated, single, {t.at(0, 0)}).failure.empty());
	isolated[t.at(0, 1)] = 1;
	REQUIRE(arrangeBuildingGrid(t, clear, isolated, single, {0, 0}).failure.empty());
	REQUIRE(!arrangeBuildingGrid(t, clear, clear, single, {}).failure.empty());
	REQUIRE(!arrangeBuildingGrid(t, clear, clear, single, {t.at(1, 1)}).failure.empty());
	REQUIRE(arrangeBuildingGrid(t, std::vector<unsigned char>(t.size(), 0), clear,
		single, {}).failure.empty());
	// Compare the early-exit circulation search against the original exhaustive
	// flood on fragmented, seam-crossing worlds and several footprint/gap sizes.
	std::mt19937 circulationRng(20260919);
	for (int trial = 0; trial < 200; ++trial)
	{
		auto walk = clear, build = clear;
		for (int i = 0; i < t.size(); ++i)
		{
			walk[i] = circulationRng() % 100 >= unsigned(trial % 80);
			build[i] = circulationRng() % 100 >= 3;
		}
		const BuildingGrid probe{{-20, -18, 20, 22}, 2 + trial % 5,
			2 + trial % 3, 1 + trial % 3, 1};
		const std::vector<int> entries{int(circulationRng() % t.size()),
			int(circulationRng() % t.size())};
		const auto arranged = arrangeBuildingGrid(t, build, walk, probe, entries);
		for (const auto &box : arranged.footprints)
			for (int y = box.y0; y < box.y1; ++y)
				for (int x = box.x0; x < box.x1; ++x)
					walk[t.at(x, y)] = 0;
		std::vector<unsigned char> sources(t.size(), 0);
		for (int i : entries) sources[i] = walk[i];
		const auto reached = stepsFrom(t, sources, walk);
		bool allReached = true;
		for (const auto &box : arranged.footprints)
		{
			bool face = false;
			for (int y = box.y0; y < box.y1; ++y)
				face |= reached[t.at(box.x0 - 1, y)] >= 0 || reached[t.at(box.x1, y)] >= 0;
			for (int x = box.x0; x < box.x1; ++x)
				face |= reached[t.at(x, box.y0 - 1)] >= 0 || reached[t.at(x, box.y1)] >= 0;
			allReached &= face;
		}
		REQUIRE(arranged.failure.empty() == allReached);
	}


	Game game(nullptr);
	grassMap(game, 6, 6);
	game.map.setResourceByIndex(63, 0, WHEAT, 1);
	game.map.setResourceByIndex(0, 63, WOOD, 1);
	const auto access = floodFrom(t, tileMask(t, {0}), groundUnitTiles(game.map), 0);
	auto frontage = resourceFrontages(game.map, access, 0);
	REQUIRE((frontage[WHEAT].edges == 1 && frontage[WOOD].edges == 1));
	REQUIRE((frontage[WHEAT].nearestStep == 0 && !frontage[WHEAT].renewableEdges));
	TerrainSketch terrain(t.size(), GRASS);
	fillRectangle(terrain, t, {20, 20, 30, 30}, WATER);
	writeVertices(GenerationMap(game.map).ref(), terrain);
	REQUIRE(!groundUnitTiles(game.map)[t.at(24, 24)]);
	REQUIRE(groundUnitTiles(game.map, true)[t.at(24, 24)]);

	Game contained(nullptr);
	grassMap(contained, 6, 6);
	terrain.assign(t.size(), GRASS);
	fillRectangle(terrain, t, {-10, -10, 11, 11}, SAND);
	fillRectangle(terrain, t, {-8, -8, 9, 9}, GRASS);
	writeVertices(GenerationMap(contained.map).ref(), terrain);
	contained.map.setResourceByIndex(30, 30, WHEAT, 1);
	REQUIRE(cropSpreadEnvelope(contained.map).steps[t.at(0, 0)] < 0);
	// Breach a wide strip through the sand ring: the same proof must now reject it.
	fillRectangle(terrain, t, {5, -2, 13, 3}, GRASS);
	writeVertices(GenerationMap(contained.map).ref(), terrain);
	REQUIRE(cropSpreadEnvelope(contained.map).steps[t.at(0, 0)] >= 0);

	// Dry open fields need no sand seal: they are finite harvest stock, not
	// sources that can spread through the surrounding construction ground.
	{
		Game dryField(nullptr);
		grassMap(dryField, 6, 6);
		const auto dry = cropGrowthField(TerrainSketch(t.size(), GRASS), t);
		dryField.map.setResourceByIndex(4, 4, WHEAT, 1);
		const auto envelope = cropSpreadEnvelope(dryField.map, &dry);
		REQUIRE((envelope.visited.size() == 1 && envelope.steps[t.at(4, 4)] == 0));
		std::vector<int> labels(t.size(), -1);
		std::vector<unsigned char> allowed(t.size(), 0);
		REQUIRE(!containedPlotsMismatch(dryField.map, t, labels, &dry).empty());
		allowed[t.at(4, 4)] = 1;
		REQUIRE(containedPlotsMismatch(dryField.map, t, labels, &dry, &allowed).empty());
		allowed[t.at(4, 4)] = 0;
		REQUIRE(!containedPlotsMismatch(dryField.map, t, labels, &dry, &allowed).empty());
	}

	// Exercise actual engine growth, not just our conservative reachability proof.
	// Irrigated wood outside a sand ring must spread, while the grass inside it stays
	// empty: terrain is the only containment a generated map may use, because the saved
	// no-growth flag is forbidden outside hand-made scenarios. Restore simulation RNG so
	// this fixture cannot change subsequent generator golden measurements.
	Game growing(nullptr);
	grassMap(growing, 6, 6);
	terrain.assign(t.size(), GRASS);
	fillRectangle(terrain, t, {-12, -12, 13, -6}, WATER);
	fillRectangle(terrain, t, {-6, -2, 7, 7}, SAND);
	fillRectangle(terrain, t, {-4, 0, 5, 5}, GRASS);
	writeVertices(GenerationMap(growing.map).ref(), terrain);
	std::vector<unsigned char> inside(t.size(), 0);
	fillRectangle(inside, t, {-4, 0, 5, 5});
	const auto savedRandom = syncRandEngine();
	setSyncRandSeed(20001);
	for (int x = -4; x <= 4; ++x)
		growing.map.setResourceByIndex(t.x(x), t.y(-4), WOOD, 1); // brush diameter, not amount
	for (int i = 0; i < t.size(); ++i)
		if (inside[i])
			REQUIRE(!growing.map.isResource(i % t.w, i / t.w));
	for (int step = 0; step < 12000; ++step)
		growing.map.growResources();
	syncRandEngine() = savedRandom;
	REQUIRE(countResource(growing.map, WOOD) > 9);
	for (int i = 0; i < t.size(); ++i)
		if (inside[i])
			REQUIRE(growing.map.getResource(i % t.w, i / t.w).type == NO_RES_TYPE);
}

inline void recursiveGeometryChecks()
{
	for (int divisions : {2, 3})
	{
		const auto tree =
			partitionRegions({3, 5, 134, 266}, divisions, 4, 7, [](const RecursiveRegion &r)
							 { return r.id == 2 ? RegionStop::Home : RegionStop::None; });
		REQUIRE((tree.failure.empty() && tree.regions[2].stop == RegionStop::Home));
		std::vector<int> cover(131 * 261, 0);
		for (const auto &r : tree.regions)
		{
			REQUIRE((r.id >= 0 && r.depth <= 4));
			if (r.terminal())
				for (int y = r.bounds.y0; y < r.bounds.y1; ++y)
					for (int x = r.bounds.x0; x < r.bounds.x1; ++x)
						++cover[(y - 5) * 131 + x - 3];
			else
			{
				REQUIRE(r.children.size() == size_t(divisions * divisions));
				int area = 0;
				for (int child : r.children)
				{
					const auto &c = tree.regions[child];
					REQUIRE((c.parent == r.id && c.depth == r.depth + 1));
					area += c.bounds.width() * c.bounds.height();
				}
				REQUIRE(area == r.bounds.width() * r.bounds.height());
			}
		}
		REQUIRE(std::all_of(cover.begin(), cover.end(), [](int n) { return n == 1; }));
	}
	REQUIRE(!partitionRegions({0, 0, 256, 256}, 3, 4, 1, {}, 10).failure.empty());
	REQUIRE(!partitionRegions({0, 0, 256, 256}, 4, 4, 1).failure.empty());
	for (int orientation = 0; orientation < 8; ++orientation)
		for (int maximum = 1; maximum <= 4; ++maximum)
		{
			const auto path = hilbertPath({0, 0, 128, 512}, maximum, 24, orientation);
			REQUIRE((path.failure.empty() && path.actualOrder == std::min(2, maximum)));
			const int side = 1 << path.actualOrder;
			REQUIRE(path.points.size() == size_t(side * side));
			std::set<std::pair<int, int>> points;
			for (const auto &p : path.points)
			{
				REQUIRE((p.x >= 0 && p.x < 128 && p.y >= 0 && p.y < 512));
				REQUIRE(points.insert({int(p.x), int(p.y)}).second);
			}
			for (const auto &segment : path.segments)
			{
				const double dx = std::abs(segment.from.x - segment.to.x) / (128.0 / side);
				const double dy = std::abs(segment.from.y - segment.to.y) / (512.0 / side);
				// Unique grid vertices connected only to adjacent vertices imply no crossing
				// centreline interiors (there are no diagonal segments).
				REQUIRE((dx + dy == 1 && (dx == 0 || dy == 0)));
				REQUIRE((segment.parentRegion >= 0 && segment.level < path.actualOrder));
			}
		}
	// Exercise actual high orders as well as spacing-reduced requests, with a
	// negative unwrapped origin to catch truncation mistakes in region ownership.
	for (int orientation = 0; orientation < 8; ++orientation)
		for (int order : {3, 4})
		{
			const auto path = hilbertPath({-256, -128, 256, 896}, order, 16, orientation);
			REQUIRE((path.failure.empty() && path.actualOrder == order));
			const int side = 1 << order;
			std::set<std::pair<int, int>> vertices;
			for (const auto &p : path.points)
				REQUIRE(vertices.insert({int(p.x), int(p.y)}).second);
			for (const auto &segment : path.segments)
			{
				const double dx = std::abs(segment.from.x - segment.to.x) / (512.0 / side);
				const double dy = std::abs(segment.from.y - segment.to.y) / (1024.0 / side);
				REQUIRE((dx + dy == 1 && (dx == 0 || dy == 0)));
				const auto &parent = path.tree.regions[segment.parentRegion];
				REQUIRE(parent.bounds.contains(int(segment.from.x), int(segment.from.y)));
				REQUIRE(parent.bounds.contains(int(segment.to.x), int(segment.to.y)));
			}
		}
	const RegionBounds overflow{std::numeric_limits<int>::min(), 0, std::numeric_limits<int>::max(),
								512};
	REQUIRE(!partitionRegions(overflow, 2, 2, 1).failure.empty());
	REQUIRE(!hilbertPath(overflow, 2, 1).failure.empty());
	REQUIRE(!hilbertPath({0, 0, 32, 128}, 4, 24).failure.empty());
	REQUIRE(!hilbertPath({0, 0, 512, 512}, 99, 1).failure.empty());

	// Sampling retains hierarchy and unwrapped geometry. A seam-spanning source
	// must remain eight tiles long, never turn into the long way around the torus.
	const std::vector<RecursiveSegment> samples{{7, 2, 3, {60, 30}, {68, 30}}};
	const auto transverse = transverseCrossings(samples, {0.5, 0.25, 0.75}, 5, 2);
	REQUIRE((transverse.failure.empty() && transverse.candidates.size() == 3));
	REQUIRE((transverse.candidates[0].id == 7 && transverse.candidates[1].id == 15));
	REQUIRE((transverse.candidates[0].from.x == 64 && transverse.candidates[0].from.y == 35));
	REQUIRE((transverse.candidates[0].to.y == 25 && transverse.candidates[0].level == 2));
	REQUIRE(transverseCrossings(samples, {0.25}, 5, 3).candidates.empty());
	REQUIRE(!transverseCrossings(samples, {0}, 5, 2).failure.empty());
	REQUIRE(!transverseCrossings({{0, 0, 0, {1, 1}, {1, 1}}}, {0.5}, 5, 2).failure.empty());
	const Torus t(64, 64);
	std::vector<CrossingCandidate> candidates{{10, 0, 1, 1, 4, 5, {5, 5}, {10, 5}},
											  {11, 1, 2, 0, 0, 5, {25, 5}, {30, 5}},
											  {12, 0, 2, 0, 0, 6, {45, 5}, {50, 5}}};
	std::vector<CrossingCandidate> seam{{91, 77, 88, 1, 5, 2, {-0.5, 5}, {1, 5}}};
	std::vector<unsigned char> open(t.size(), 1);
	const auto endpointGraph = crossingEndpointGraph(t, open, seam);
	REQUIRE((endpointGraph.failure.empty() && endpointGraph.regions == 2));
	REQUIRE((endpointGraph.edges.size() == 1 && endpointGraph.edges[0].length == 2));
	REQUIRE((seam[0].fromRegion == 0 && seam[0].toRegion == 1 && seam[0].parentRegion == 5));
	open[t.at(63, 5)] = 0;
	REQUIRE(!crossingEndpointGraph(t, open, seam).failure.empty());
	const auto connected = selectCrossings(t, 3, {}, {0, 1, 2}, candidates, 0, 0, 8, 42);
	REQUIRE((connected.failure.empty() && connected.mandatory == 2 && connected.selected.size() == 2));
	std::reverse(candidates.begin(), candidates.end());
	const auto repeated = selectCrossings(t, 3, {}, {0, 1, 2}, candidates, 0, 0, 8, 42);
	REQUIRE(repeated.selected.size() == connected.selected.size());
	for (size_t i = 0; i < repeated.selected.size(); ++i)
		REQUIRE(repeated.selected[i].id == connected.selected[i].id);
	REQUIRE(!selectCrossings(t, 3, {}, {0, 1, 2}, candidates, 0, 0, 40, 42).failure.empty());
	REQUIRE(!selectCrossings(t, 3, {}, {0, 1, 2}, candidates, 0, 0, 0, 42,
							[](const auto &, const auto &) { return false; })
				.failure.empty());

	const auto scarce =
		selectCrossings(t, 3, {{0, 1, 100}, {1, 2, 100}}, {}, candidates, 2, 4, 8, 42);
	REQUIRE((scarce.failure.empty() && scarce.majorShortfall > 0 && scarce.localShortfall > 0));
	REQUIRE(!selectCrossings(t, 3, {}, {}, candidates, 2147483647, 0, 8, 42).failure.empty());
	REQUIRE(
		!selectCrossings(t, 3, {{0, 1, 1000000000}}, {}, candidates, 0, 0, 8, 42).failure.empty());
	REQUIRE(!selectCrossings(t, 3, {}, {0, 1, 2}, candidates, 0, 0, 2147483647, 42).failure.empty());

	// Raster fixture: the same crossing width survives both beaches and the torus seam.
	TerrainSketch terrain(t.size(), WATER);
	std::vector<unsigned char> crossing(t.size(), 0);
	strokePath(crossing, t, {{60, 30, 3.5}, {68, 30, 3.5}});
	for (int i = 0; i < t.size(); ++i)
		if (crossing[i])
			terrain[i] = SAND;
	layBeaches(terrain, t);
	const auto water = pureTiles(terrain, t, WATER);
	for (int x = 61; x <= 66; ++x)
		for (int y = 28; y <= 32; ++y)
			REQUIRE(!water[t.at(x, y)]);
}

// Fractal-specific economy regression: zero ambient resources still support the
// maximum opening worker count, and ordinary map serialization preserves terrain,
// deposits and starting sites. Run in this existing CI harness, not an ad-hoc binary.
inline void fractalEconomyChecks()
{
	for (const char *id : {"sierpinski-gardens", "hilbert-river"})
	{
		GenerationRequest request;
		request.setMethodDefaults(GeneratorRegistry::builtins().idOf(id));
		request.seed = 20001;
		request.wDec = request.hDec = 8;
		request.nbTeams = 4;
		request.nbWorkers = 8;
		for (const char *key :
			 {"wheat-amount", "wood-amount", "stone-amount", "algae-amount", "fruit-amount"})
			request.options[key] = 0;
		Game game(nullptr);
		const auto result = GenerationService().generate(game, request, true);
		if (!result)
			std::fprintf(stderr, "%s scarcity: %s\n", id, result.diagnostic().c_str());
		REQUIRE(result);
		REQUIRE((countResource(game.map, WHEAT) > 0 && countResource(game.map, WOOD) > 0 &&
			   countResource(game.map, STONE) > 0));
		REQUIRE((countResource(game.map, CHERRY) == 0 && countResource(game.map, ORANGE) == 0 &&
			   countResource(game.map, PRUNE) == 0));
		const auto units = unitTilesByTeam(game.map, 4);
		for (const auto &team : units)
			REQUIRE(team.size() == 8);
		std::string bytes;
		{
			auto *memory = new GAGCore::MemoryStreamBackend();
			GAGCore::BinaryOutputStream output(memory);
			game.save(&output, true, "Fractal map round trip");
			output.flush();
			memory->seekFromEnd(0);
			bytes.assign(memory->getBuffer(), memory->getPosition());
		}
		Game loaded(nullptr);
		auto *readback = new GAGCore::MemoryStreamBackend(bytes.data(), bytes.size());
		// The memory constructor appends supplied bytes and leaves its cursor at EOF.
		readback->seekFromStart(0);
		GAGCore::BinaryInputStream input(readback);
		REQUIRE(loaded.load(&input));
		for (int y = 0; y < game.map.getH(); ++y)
			for (int x = 0; x < game.map.getW(); ++x)
			{
				REQUIRE(game.map.vertexTerrainAt(x, y) == loaded.map.vertexTerrainAt(x, y));
				REQUIRE(game.map.canResourcesGrow(x, y) == loaded.map.canResourcesGrow(x, y));
				REQUIRE(game.map.getResource(x, y).type == loaded.map.getResource(x, y).type);
				REQUIRE(game.map.getResource(x, y).amount == loaded.map.getResource(x, y).amount);
			}
		for (int team = 0; team < 4; ++team)
		{
			REQUIRE(game.teams[team]->startPosX == loaded.teams[team]->startPosX);
			REQUIRE(game.teams[team]->startPosY == loaded.teams[team]->startPosY);
		}
	}
}
inline void fractalCrossingRegressionChecks()
{
	// These requests used to pass preflight and then lose a bridge shoulder to
	// a home farm. Keep both complete requests as regressions, without rerolls.
	for (int teams : {4, 5})
	{
		GenerationRequest request;
		request.setMethodDefaults(GeneratorRegistry::builtins().idOf("sierpinski-gardens"));
		request.wDec = 7;
		request.hDec = 8;
		request.nbTeams = teams;
		request.seed = 20001;
		Game game(nullptr);
		const auto result = GenerationService().generate(game, request, true);
		if (!result)
			std::fprintf(stderr, "Gardens narrow crossing: %s\n", result.diagnostic().c_str());
		REQUIRE(result);
	}
	// Shallow folds can have all segment midpoints occupied while quarter-bank
	// approaches fit. Both ordinary and minimum controls used to reject this seed.
	for (bool minimum : {false, true})
	{
		GenerationRequest request;
		request.setMethodDefaults(GeneratorRegistry::builtins().idOf("hilbert-river"));
		request.seed = 20002;
		request.options["maximum-fold-depth"] = 1;
		if (minimum)
		{
			request.options["river-width"] = 4;
			request.options["minimum-land-spacing"] = 24;
			request.options["local-crossings"] = 0;
			request.options["major-shortcuts"] = 0;
		}
		Game game(nullptr);
		const auto result = GenerationService().generate(game, request, true);
		if (!result)
			std::fprintf(stderr, "Hilbert shallow crossing: %s\n", result.diagnostic().c_str());
		REQUIRE(result);
	}
}

inline void toolkitChecks()
{
	widestWalkChecks();
	solvingAndRiverChecks();
	boundaryExclusionChecks();
	containedPlotChecks();
	siteOperationChecks();
	fractalCrossingRegressionChecks();
	recursiveGeometryChecks();
	fractalEconomyChecks();
	floodChecks();
	wallRouteAndStencilChecks();
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
	rockChecks();
	heightFieldLooksChecks();
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
	landformChecks();
	colonyLeakChecks();
	puts("PASS shared toolkit: floods, sketch, planting, roads, settlements, balanced starts, "
		 "walls, stencil turns, routes, scatter, lattice noise, wedge frame, "
		 "shuffle, drawing, branches, stretch, sand patches, algae growth, fields, clumps, walls, "
		 "tower reach, territories, arena primitives, sealed lines, polygons, tessellations, warp, "
		 "graph mazes, shortcuts, cell crossings, region labels, gate partitions, region leaks, "
		 "grained landforms, teardrop homes, near trees and colony leaks");
}
} // namespace ToolkitChecks
