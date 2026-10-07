// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "GradientTest.h"
#include <cmath>

#include "Map.h"
#include "MapInternal.h"
#include "TerrainType.h"

#include <algorithm>
#include <cstdlib>
#include <vector>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <functional>

TEST_SUITE("Gradient")
{
	TEST_CASE_FIXTURE(GradientTest, "OpenGridIsOctileOnTorus") { testOpenGridIsOctileOnTorus(); }
	TEST_CASE_FIXTURE(GradientTest, "ObstaclesForcePathAround") { testObstaclesForcePathAround(); }
	TEST_CASE_FIXTURE(GradientTest, "WaterCostsBySwimClass") { testWaterCostsBySwimClass(); }
	TEST_CASE_FIXTURE(GradientTest, "UnreachableCellsStayUnreachable") { testUnreachableCellsStayUnreachable(); }
	TEST_CASE_FIXTURE(GradientTest, "SeedBelowGoalPropagates") { testSeedBelowGoalPropagates(); }
	TEST_CASE_FIXTURE(GradientTest, "SeedsBeyondBucketWindow") { testSeedsBeyondBucketWindow(); }
	TEST_CASE_FIXTURE(GradientTest, "MaxCostStopsPropagation") { testMaxCostStopsPropagation(); }
	TEST_CASE_FIXTURE(GradientTest, "DirectionPrefersCheapestTotal") { testDirectionPrefersCheapestTotal(); }
	TEST_CASE_FIXTURE(GradientTest, "DirectionBlockedNeighbour") { testDirectionBlockedNeighbour(); }
	TEST_CASE_FIXTURE(GradientTest, "SwimClassFromSpeeds") { testSwimClassFromSpeeds(); }
	TEST_CASE_FIXTURE(GradientTest, "RandomFieldsAgainstReference") { testRandomFieldsAgainstReference(); }
	TEST_CASE_FIXTURE(GradientTest, "MatchesLegacyKernelOnLargeMaps") { testMatchesLegacyKernelOnLargeMaps(); }
}

namespace
{
	constexpr int kMapDec = 3; // 8x8

	// Same minimal fixture as MapQueryTest: bypass setSize() so no Sector /
	// globalContainer link surface is needed.
	struct GrassMap : Map
	{
		GrassMap(int widthDec = kMapDec, int heightDec = kMapDec)
		{
			wDec = widthDec;
			hDec = heightDec;
			w = 1 << wDec;
			h = 1 << hDec;
			wMask = w - 1;
			hMask = h - 1;
			size = static_cast<size_t>(w * h);
			// Test-only private access bootstraps this partial map.
			resourceCells.assign(size, {});
			for (auto &cell : resourceCells) cell.mayGrow = 1;
			occupancyCells.assign(size, {});
			areaCells.assign(size, {});
			legacyTerrain.assign(size, 0);
			scriptAreaCells.assign(size, 0);
			bindBootstrappedArrays();
            importLegacyTerrain();
		}
		~GrassMap()
		{
			w = h = 0;
			wMask = hMask = 0;
			wDec = hDec = 0;
			size = 0;
		}
		size_t cells() const { return size; }
		void putWater(int x, int y) { setCellTerrain(x,y,WATER); }
		void putWaterAt(size_t i) { setCellTerrain(i,WATER); }

		// The kernel before the low-level rewrite, kept verbatim as a differential oracle.
		void legacyPropagateGradient(Uint16 *gradient, int swimClass, int maxCost = GRADIENT_COST_LIMIT) const
		{
			constexpr int WATER_STEP[SWIM_CLASS_COUNT] = { 0, 5, 7, 10, 13, 20, 30 };
			constexpr int MAX_STEP = WATER_STEP[SWIM_CLASS_COUNT - 1] * GRADIENT_DIAGONAL_STEP / GRADIENT_STEP;
			constexpr int BUCKETS = MAX_STEP + 1;
			constexpr int COST_LIMIT = GRADIENT_AT_GOAL - GRADIENT_UNREACHABLE - 1 - MAX_STEP;
			static std::vector<int> buckets[BUCKETS];
			static std::vector<std::pair<int, int>> deferredSeeds;
			auto expandBucket = [&](size_t &pending, int cur, int limit, auto stepAt)
			{
				std::vector<int> &bucket = buckets[cur % BUCKETS];
				for (size_t bi = 0; bi < bucket.size(); bi++)
				{
					int i = bucket[bi];
					pending--;
					if (GRADIENT_AT_GOAL - gradient[i] != cur)
						continue;
					size_t x = i & wMask;
					size_t y = i >> wDec;
					const size_t left = (x - 1) & wMask;
					const size_t right = (x + 1) & wMask;
					const size_t above = ((y - 1) & hMask) << wDec;
					const size_t row = y << wDec;
					const size_t below = ((y + 1) & hMask) << wDec;
					const int step = stepAt(i);
					const int cardinalCost = cur + step;
					const int diagonalCost = cur + step * GRADIENT_DIAGONAL_STEP / GRADIENT_STEP;
					const unsigned cardinalValue = cardinalCost <= limit ? GRADIENT_AT_GOAL - cardinalCost : 1;
					const unsigned diagonalValue = diagonalCost <= limit ? GRADIENT_AT_GOAL - diagonalCost : 1;
					auto& cardinalBucket = buckets[cardinalCost % BUCKETS];
					auto& diagonalBucket = buckets[diagonalCost % BUCKETS];
					auto relax = [&](size_t n, unsigned value, std::vector<int>& destination)
					{
						if (static_cast<unsigned>(gradient[n] - 1) < value - 1)
						{
							gradient[n] = (Uint16)value;
							destination.push_back((int)n);
							pending++;
						}
					};
					relax(above | left, diagonalValue, diagonalBucket);
					relax(above | x, cardinalValue, cardinalBucket);
					relax(above | right, diagonalValue, diagonalBucket);
					relax(row | right, cardinalValue, cardinalBucket);
					relax(below | right, diagonalValue, diagonalBucket);
					relax(below | x, cardinalValue, cardinalBucket);
					relax(below | left, diagonalValue, diagonalBucket);
					relax(row | left, cardinalValue, cardinalBucket);
				}
				bucket.clear();
			};
			const int limit = std::min(maxCost, COST_LIMIT);
			for (int b = 0; b < BUCKETS; b++)
				buckets[b].clear();
			deferredSeeds.clear();
			size_t pending = 0;
			for (size_t i = 0; i < size; i++)
				if (gradient[i] > GRADIENT_UNREACHABLE)
				{
					int cost = GRADIENT_AT_GOAL - gradient[i];
					if (cost <= MAX_STEP)
					{
						buckets[cost % BUCKETS].push_back((int)i);
						pending++;
					}
					else
						deferredSeeds.push_back({cost, (int)i});
				}
			std::sort(deferredSeeds.begin(), deferredSeeds.end());
			auto sweep = [&](auto stepAt)
			{
				size_t nextSeed = 0;
				for (int cur = 0; (pending > 0 || nextSeed < deferredSeeds.size()) && cur <= limit; cur++)
				{
					if (pending == 0)
						cur = deferredSeeds[nextSeed].first;
					for (; nextSeed < deferredSeeds.size() && deferredSeeds[nextSeed].first == cur; nextSeed++)
					{
						buckets[cur % BUCKETS].push_back(deferredSeeds[nextSeed].second);
						pending++;
					}
					expandBucket(pending, cur, limit, stepAt);
				}
			};
			if (swimClass == 0 || swimClass == SWIM_CLASS_EVEN)
				sweep([](int) { return GRADIENT_STEP; });
			else
				sweep([&](int i) { return isWater((unsigned)i) ? WATER_STEP[swimClass] : GRADIENT_STEP; });
		}
		void putGroundUnit(int x, int y) { setGroundUnit(x, y, 0); }
	};

	// Shortest wrapped axis distance on a torus of extent n.
	int wrapDist(int a, int b, int n)
	{
		int d = std::abs(a - b) % n;
		return std::min(d, n - d);
	}

	int octile(int dx, int dy)
	{
		int lo = std::min(dx, dy);
		int hi = std::max(dx, dy);
		return GRADIENT_DIAGONAL_STEP * lo + GRADIENT_STEP * (hi - lo);
	}

	int cost(const std::vector<Uint16>& gradient, const GrassMap& map, int x, int y)
	{
		return GRADIENT_AT_GOAL - gradient[map.coordToIndex(x, y)];
	}

	std::vector<Uint16> blank(const GrassMap& map)
	{
		return std::vector<Uint16>(map.cells(), GRADIENT_UNREACHABLE);
	}
}

void GradientTest::testOpenGridIsOctileOnTorus()
{
	GrassMap map;
	std::vector<Uint16> g = blank(map);
	const int gx = 1, gy = 2;
	g[map.coordToIndex(gx, gy)] = GRADIENT_AT_GOAL;
	map.propagateGradient(g.data(), 0);
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
			CHECK_EQ(octile(wrapDist(x, gx, 8), wrapDist(y, gy, 8)), cost(g, map, x, y));
}

void GradientTest::testObstaclesForcePathAround()
{
	GrassMap map;
	std::vector<Uint16> g = blank(map);
	// Vertical wall at x=4 spanning all rows except y=7: the only way from
	// x>4 to the goal at (0,0) crosses the gap at (4,7) or wraps around.
	for (int y = 0; y < 7; y++)
		g[map.coordToIndex(4, y)] = GRADIENT_FORBIDDEN;
	g[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.propagateGradient(g.data(), 0);
	// (3,0): two cardinal steps west.
	CHECK_EQ(30, cost(g, map, 3, 0));
	// (5,0): wrap east is 3 cardinal steps (5->6->7->0).
	CHECK_EQ(30, cost(g, map, 5, 0));
	// Obstacles keep their value.
	CHECK_EQ((int)GRADIENT_FORBIDDEN, (int)g[map.coordToIndex(4, 3)]);
	// (4,7) gap cell: diagonal to (3,0) wrapped = 14, then 30.
	CHECK_EQ(44, cost(g, map, 4, 7));
}

void GradientTest::testWaterCostsBySwimClass()
{
	GrassMap map;
	// Water column at x=1..2 across all rows; goal at (0,0).
	for (int y = 0; y < 8; y++)
	{
		map.putWater(1, y);
		map.putWater(2, y);
	}
	std::vector<Uint16> g = blank(map);
	g[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.propagateGradient(g.data(), 5); // water costs 20
	// (3,0) -> (2,0) water 20 -> (1,0) water 20 -> (0,0) land 10 = 50; around the torus is 5 land steps = 50 too.
	CHECK_EQ(50, cost(g, map, 3, 0));
	// (2,0): enter (1,0) = 20, then (0,0) = 10.
	CHECK_EQ(30, cost(g, map, 2, 0));
	CHECK_EQ(10, cost(g, map, 1, 0));

	// A class that swims as fast as it walks pays the land rate.
	std::vector<Uint16> even = blank(map);
	even[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.propagateGradient(even.data(), Map::SWIM_CLASS_EVEN);
	CHECK_EQ(30, cost(even, map, 3, 0));

	// Non-swimmer: the caller marks water as an obstacle, so (3,0) must go around.
	std::vector<Uint16> walker = blank(map);
	for (int y = 0; y < 8; y++)
	{
		walker[map.coordToIndex(1, y)] = GRADIENT_FORBIDDEN;
		walker[map.coordToIndex(2, y)] = GRADIENT_FORBIDDEN;
	}
	walker[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.propagateGradient(walker.data(), 0);
	CHECK_EQ(50, cost(walker, map, 3, 0));
	CHECK_EQ(40, cost(walker, map, 4, 0));
}

void GradientTest::testUnreachableCellsStayUnreachable()
{
	GrassMap map;
	std::vector<Uint16> g = blank(map);
	// Enclose (6,6) with obstacles.
	for (int dy = -1; dy <= 1; dy++)
		for (int dx = -1; dx <= 1; dx++)
			if (dx || dy)
				g[map.coordToIndex(6 + dx, 6 + dy)] = GRADIENT_FORBIDDEN;
	g[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.propagateGradient(g.data(), 0);
	CHECK_EQ((int)GRADIENT_UNREACHABLE, (int)g[map.coordToIndex(6, 6)]);
	CHECK_EQ((int)GRADIENT_AT_GOAL, (int)g[map.coordToIndex(0, 0)]);
	CHECK_EQ(2, gradientTiles(g[map.coordToIndex(2, 0)]));
	CHECK_EQ(1, gradientTiles(g[map.coordToIndex(1, 1)]));
}

void GradientTest::testSeedBelowGoalPropagates()
{
	GrassMap map;
	std::vector<Uint16> g = blank(map);
	// A forbidden-zone border cell seeded one step below the goal, as the
	// forbidden gradient does, spreads from its own cost.
	g[map.coordToIndex(4, 4)] = GRADIENT_FORBIDDEN_BORDER;
	map.propagateGradient(g.data(), 0);
	CHECK_EQ(GRADIENT_STEP, cost(g, map, 4, 4));
	CHECK_EQ(2 * GRADIENT_STEP, cost(g, map, 5, 4));
	CHECK_EQ(GRADIENT_STEP + GRADIENT_DIAGONAL_STEP, cost(g, map, 5, 5));
}

void GradientTest::testSeedsBeyondBucketWindow()
{
	// Seeds may start at any cost, as the round-trip gradients seed resource
	// tiles with their distance to a building. Walls along x=2 and y=2 cut
	// the torus into one 7x7 rectangle with the goal in the corner (3,3) and
	// a seed at cost 60 in the opposite corner (1,1), 84 from the goal.
	GrassMap map;
	std::vector<Uint16> g = blank(map);
	for (int i = 0; i < 8; i++)
	{
		g[map.coordToIndex(2, i)] = GRADIENT_FORBIDDEN;
		g[map.coordToIndex(i, 2)] = GRADIENT_FORBIDDEN;
	}
	g[map.coordToIndex(3, 3)] = GRADIENT_AT_GOAL;
	g[map.coordToIndex(1, 1)] = GRADIENT_AT_GOAL - 60;
	map.propagateGradient(g.data(), 0);
	CHECK_EQ(0, cost(g, map, 3, 3));
	CHECK_EQ(60, cost(g, map, 1, 1));
	// (0,0) and (1,0): 70 through the seed, 70 and 80 from the goal.
	CHECK_EQ(70, cost(g, map, 0, 0));
	CHECK_EQ(70, cost(g, map, 1, 0));
	// (7,7): four diagonals from the goal beat 88 through the seed.
	CHECK_EQ(56, cost(g, map, 7, 7));
	CHECK_EQ((int)GRADIENT_FORBIDDEN, (int)g[map.coordToIndex(2, 5)]);
}

void GradientTest::testMaxCostStopsPropagation()
{
	GrassMap map;
	std::vector<Uint16> g = blank(map);
	g[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.propagateGradient(g.data(), 0, 20);
	CHECK_EQ(20, cost(g, map, 2, 0));
	CHECK_EQ(14, cost(g, map, 1, 1));
	CHECK_EQ((int)GRADIENT_UNREACHABLE, (int)g[map.coordToIndex(3, 0)]);
	CHECK_EQ((int)GRADIENT_UNREACHABLE, (int)g[map.coordToIndex(2, 2)]);
}

void GradientTest::testDirectionPrefersCheapestTotal()
{
	GrassMap map;
	std::vector<Uint16> g = blank(map);
	g[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.propagateGradient(g.data(), 0);
	int dx = 9, dy = 9;
	// From (3,3) the cheapest neighbour is the diagonal (2,2).
	CHECK(map.directionByGradient(1, 0, 3, 3, g.data(), &dx, &dy, true));
	CHECK_EQ(-1, dx);
	CHECK_EQ(-1, dy);
	// From (3,0) it is straight west.
	CHECK(map.directionByGradient(1, 0, 3, 0, g.data(), &dx, &dy, true));
	CHECK_EQ(-1, dx);
	CHECK_EQ(0, dy);
	// At the goal: stay.
	CHECK(map.directionByGradient(1, 0, 0, 0, g.data(), &dx, &dy, true));
	CHECK_EQ(0, dx);
	CHECK_EQ(0, dy);

	// Swimmer of class 5 (water costs 20) at (3,0) with water on (2,0) and (1,0).
	// (2,0) is worth 28 (two land diagonals) minus a 20 water step, (2,1) and
	// (2,7) are worth 24 minus a 14 land diagonal: the unit skirts the water.
	GrassMap water;
	water.putWater(1, 0);
	water.putWater(2, 0);
	std::vector<Uint16> f = blank(water);
	f[water.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	water.propagateGradient(f.data(), 5);
	CHECK_EQ(28, cost(f, water, 2, 0));
	CHECK_EQ(24, cost(f, water, 2, 1));
	CHECK(water.directionByGradient(1, 5, 3, 0, f.data(), &dx, &dy, true));
	CHECK_EQ(-1, dx);
	CHECK(dy != 0); // never straight into the water
}

void GradientTest::testDirectionBlockedNeighbour()
{
	GrassMap map;
	std::vector<Uint16> g = blank(map);
	g[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.propagateGradient(g.data(), 0);
	// Another unit stands on (2,2): from (3,3) the strict move must still make
	// progress through (2,3) or (3,2), never the occupied diagonal.
	map.putGroundUnit(2, 2);
	int dx = 9, dy = 9;
	CHECK(map.directionByGradient(1, 0, 3, 3, g.data(), &dx, &dy, true));
	CHECK(((dx == -1 && dy == 0) || (dx == 0 && dy == -1)));
	// Fully surrounded by units: strict fails, and so does the sidestep (no free cell).
	for (int ddy = -1; ddy <= 1; ddy++)
		for (int ddx = -1; ddx <= 1; ddx++)
			if (ddx || ddy)
				map.putGroundUnit(3 + ddx, 3 + ddy);
	CHECK(!map.directionByGradient(1, 0, 3, 3, g.data(), &dx, &dy, true));
	CHECK(!map.directionByGradient(1, 0, 3, 3, g.data(), &dx, &dy, false));
}

void GradientTest::testSwimClassFromSpeeds()
{
	CHECK_EQ(0, Map::swimClass(16, 0));
	// Equal speeds: water costs the same as land.
	CHECK_EQ(Map::SWIM_CLASS_EVEN, Map::swimClass(30, 30));
	// A slow walker that swims fast prefers water; a fast walker avoids it.
	CHECK(Map::swimClass(16, 30) < Map::SWIM_CLASS_EVEN);
	CHECK(Map::swimClass(30, 10) > Map::SWIM_CLASS_EVEN);
	CHECK_EQ(SWIM_CLASS_COUNT - 1, Map::swimClass(30, 10));
}

void GradientTest::testRandomFieldsAgainstReference()
{
	std::mt19937 random(184);
	constexpr int waterCosts[] = {0, 5, 7, 10, 13, 20, 30};
	for (int trial = 0; trial < 700; ++trial)
	{
		// Include one-cell axes, rectangular maps and all movement classes.
		const int width = 1 << (trial % 6);
		const int height = 1 << ((trial / 6) % 6);
		const int swimClass = trial % SWIM_CLASS_COUNT;
		GrassMap map(trial % 6, (trial / 6) % 6);
		auto input = blank(map);
		std::vector<bool> water(map.cells());
		for (size_t i = 0; i < map.cells(); ++i)
		{
			water[i] = random() % 3 == 0;
			if (water[i]) map.putWater(i % width, i / width);
			if (random() % 4 == 0 || (water[i] && swimClass == 0))
				input[i] = GRADIENT_FORBIDDEN;
			else if (trial % 10 != 0 && random() % 12 == 0)
				input[i] = GRADIENT_AT_GOAL - random() % 43;
		}

		// Independent heap-based shortest paths; ordinary modulo handles wrapping.
		auto expected = input;
		using Entry = std::pair<int, size_t>;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
		for (size_t i = 0; i < expected.size(); ++i)
			if (expected[i] > GRADIENT_UNREACHABLE)
				queue.emplace(GRADIENT_AT_GOAL - expected[i], i);
		while (!queue.empty())
		{
			const auto [cost, i] = queue.top();
			queue.pop();
			if (cost != GRADIENT_AT_GOAL - expected[i]) continue;
			const int x = i % width, y = i / width;
			const int step = water[i] && swimClass > 0 ? waterCosts[swimClass] : 10;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (dx == 0 && dy == 0) continue;
					const size_t n = ((y + dy + height) % height) * width + (x + dx + width) % width;
					if (expected[n] == GRADIENT_FORBIDDEN) continue;
					const int candidate = cost + (dx && dy ? step * 14 / 10 : step);
					if (candidate < GRADIENT_AT_GOAL - expected[n])
					{
						expected[n] = GRADIENT_AT_GOAL - candidate;
						queue.emplace(candidate, n);
					}
				}
		}
		map.propagateGradient(input.data(), swimClass);
		CHECK(input == expected);
	}
}

void GradientTest::testMatchesLegacyKernelOnLargeMaps()
{
	std::mt19937 random(2026);
	// Widths 1-4 exercise the column-edge paths; the rest mostly the interior.
	const int shapes[][2] = { {6, 6}, {7, 7}, {8, 8}, {7, 5}, {5, 8}, {0, 7}, {1, 7}, {2, 6}, {8, 1} };
	int trial = 0;
	for (const auto &shape : shapes)
		for (int swimClass = 0; swimClass < SWIM_CLASS_COUNT; ++swimClass)
			for (int variant = 0; variant < 6; ++variant, ++trial)
			{
				GrassMap map(shape[0], shape[1]);
				std::vector<Uint16> input = blank(map);
				const unsigned obstaclePercent = (variant * 17 + trial) % 45;
				const unsigned waterPercent = (variant * 23 + trial * 7) % 60;
				// One goal; a few goals; many goals; seeds within the first bucket
				// window; seeds far beyond it; many goals under a cost cap.
				const unsigned seedEvery = variant == 0 ? 0 : variant == 1 ? 997 : 61;
				for (size_t i = 0; i < map.cells(); ++i)
				{
					const bool water = random() % 100 < waterPercent;
					if (water)
						map.putWaterAt(i);
					if (random() % 100 < obstaclePercent || (water && swimClass == 0))
						input[i] = GRADIENT_FORBIDDEN;
					else if (seedEvery && random() % seedEvery == 0)
						input[i] = variant == 3 ? GRADIENT_AT_GOAL - random() % 43
							: variant == 4 ? GRADIENT_AT_GOAL - random() % 3000
							: GRADIENT_AT_GOAL;
				}
				if (variant == 0)
					input[random() % map.cells()] = GRADIENT_AT_GOAL;
				const int maxCost = variant == 5 ? 25 + int(random() % 400) : Map::GRADIENT_COST_LIMIT;

				auto expected = input;
				map.legacyPropagateGradient(expected.data(), swimClass, maxCost);
				map.propagateGradient(input.data(), swimClass, maxCost);
				CHECK_MESSAGE(input == expected, ("trial " + std::to_string(trial)));
			}
}
