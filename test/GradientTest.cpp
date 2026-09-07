// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "GradientTest.h"

#include "Map.h"
#include "MapInternal.h"
#include "TerrainType.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

CPPUNIT_TEST_SUITE_REGISTRATION( GradientTest );

namespace
{
	constexpr int kMapDec = 3; // 8x8

	// Same minimal fixture as MapQueryTest: bypass setSize() so no Sector /
	// globalContainer link surface is needed.
	struct GrassMap : Map
	{
		GrassMap()
		{
			wDec = kMapDec;
			hDec = kMapDec;
			w = 1 << kMapDec;
			h = 1 << kMapDec;
			wMask = w - 1;
			hMask = h - 1;
			size = static_cast<size_t>(w * h);
			cases.assign(size, Case());
		}
		~GrassMap()
		{
			w = h = 0;
			wMask = hMask = 0;
			wDec = hDec = 0;
			size = 0;
		}
		size_t cells() const { return size; }
		void trackTraffic() { trafficDirection = new Uint8[size * 8](); }
		void putWater(int x, int y) { cases[coordToIndex(x, y)].terrain = 256; }
		void putGroundUnit(int x, int y) { cases[coordToIndex(x, y)].groundUnit = 0; }
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
			CPPUNIT_ASSERT_EQUAL(octile(wrapDist(x, gx, 8), wrapDist(y, gy, 8)), cost(g, map, x, y));
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
	CPPUNIT_ASSERT_EQUAL(30, cost(g, map, 3, 0));
	// (5,0): wrap east is 3 cardinal steps (5->6->7->0).
	CPPUNIT_ASSERT_EQUAL(30, cost(g, map, 5, 0));
	// Obstacles keep their value.
	CPPUNIT_ASSERT_EQUAL((int)GRADIENT_FORBIDDEN, (int)g[map.coordToIndex(4, 3)]);
	// (4,7) gap cell: diagonal to (3,0) wrapped = 14, then 30.
	CPPUNIT_ASSERT_EQUAL(44, cost(g, map, 4, 7));
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
	CPPUNIT_ASSERT_EQUAL(50, cost(g, map, 3, 0));
	// (2,0): enter (1,0) = 20, then (0,0) = 10.
	CPPUNIT_ASSERT_EQUAL(30, cost(g, map, 2, 0));
	CPPUNIT_ASSERT_EQUAL(10, cost(g, map, 1, 0));

	// A class that swims as fast as it walks pays the land rate.
	std::vector<Uint16> even = blank(map);
	even[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.propagateGradient(even.data(), Map::SWIM_CLASS_EVEN);
	CPPUNIT_ASSERT_EQUAL(30, cost(even, map, 3, 0));

	// Non-swimmer: the caller marks water as an obstacle, so (3,0) must go around.
	std::vector<Uint16> walker = blank(map);
	for (int y = 0; y < 8; y++)
	{
		walker[map.coordToIndex(1, y)] = GRADIENT_FORBIDDEN;
		walker[map.coordToIndex(2, y)] = GRADIENT_FORBIDDEN;
	}
	walker[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.propagateGradient(walker.data(), 0);
	CPPUNIT_ASSERT_EQUAL(50, cost(walker, map, 3, 0));
	CPPUNIT_ASSERT_EQUAL(40, cost(walker, map, 4, 0));
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
	CPPUNIT_ASSERT_EQUAL((int)GRADIENT_UNREACHABLE, (int)g[map.coordToIndex(6, 6)]);
	CPPUNIT_ASSERT_EQUAL((int)GRADIENT_AT_GOAL, (int)g[map.coordToIndex(0, 0)]);
	CPPUNIT_ASSERT_EQUAL(2, gradientTiles(g[map.coordToIndex(2, 0)]));
	CPPUNIT_ASSERT_EQUAL(1, gradientTiles(g[map.coordToIndex(1, 1)]));
}

void GradientTest::testSeedBelowGoalPropagates()
{
	GrassMap map;
	std::vector<Uint16> g = blank(map);
	// A forbidden-zone border cell seeded one step below the goal, as the
	// forbidden gradient does, spreads from its own cost.
	g[map.coordToIndex(4, 4)] = GRADIENT_FORBIDDEN_BORDER;
	map.propagateGradient(g.data(), 0);
	CPPUNIT_ASSERT_EQUAL(GRADIENT_STEP, cost(g, map, 4, 4));
	CPPUNIT_ASSERT_EQUAL(2 * GRADIENT_STEP, cost(g, map, 5, 4));
	CPPUNIT_ASSERT_EQUAL(GRADIENT_STEP + GRADIENT_DIAGONAL_STEP, cost(g, map, 5, 5));
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
	CPPUNIT_ASSERT_EQUAL(0, cost(g, map, 3, 3));
	CPPUNIT_ASSERT_EQUAL(60, cost(g, map, 1, 1));
	// (0,0) and (1,0): 70 through the seed, 70 and 80 from the goal.
	CPPUNIT_ASSERT_EQUAL(70, cost(g, map, 0, 0));
	CPPUNIT_ASSERT_EQUAL(70, cost(g, map, 1, 0));
	// (7,7): four diagonals from the goal beat 88 through the seed.
	CPPUNIT_ASSERT_EQUAL(56, cost(g, map, 7, 7));
	CPPUNIT_ASSERT_EQUAL((int)GRADIENT_FORBIDDEN, (int)g[map.coordToIndex(2, 5)]);
}

void GradientTest::testTrafficMakesNarrowCellsOneWay()
{
	// Wall at x=4 with a one-wide gap at (4,3); goal at (3,3). From (5,3) the
	// way is two steps west through the gap, or six steps east around the torus.
	GrassMap map;
	map.trackTraffic();
	auto walled = [&map]()
	{
		std::vector<Uint16> g = blank(map);
		for (int y = 0; y < 8; y++)
			if (y != 3)
				g[map.coordToIndex(4, y)] = GRADIENT_FORBIDDEN;
		return g;
	};
	std::vector<Uint16> g = walled();
	g[map.coordToIndex(3, 3)] = GRADIENT_AT_GOAL;
	map.propagateGradient(g.data(), 0);
	CPPUNIT_ASSERT_EQUAL(20, cost(g, map, 5, 3));

	// Sixteen units recently crossed the gap eastward (tabClose[3] = east): a
	// westbound step into it now pays the full lane penalty, three tiles.
	map.trafficDirection[map.coordToIndex(4, 3) * 8 + 3] = 16;
	std::vector<Uint16> lane = walled();
	lane[map.coordToIndex(3, 3)] = GRADIENT_AT_GOAL;
	map.propagateGradient(lane.data(), 0);
	CPPUNIT_ASSERT_EQUAL(50, cost(lane, map, 5, 3));
	// Following the flow costs nothing extra: eastbound from (3,3) to (5,3).
	std::vector<Uint16> along = walled();
	along[map.coordToIndex(5, 3)] = GRADIENT_AT_GOAL;
	map.propagateGradient(along.data(), 0);
	CPPUNIT_ASSERT_EQUAL(20, cost(along, map, 3, 3));
	// Eight units the other way make it a partial penalty: 8 of 16.
	map.trafficDirection[map.coordToIndex(4, 3) * 8 + 7] = 8;
	std::vector<Uint16> partial = walled();
	partial[map.coordToIndex(3, 3)] = GRADIENT_AT_GOAL;
	map.propagateGradient(partial.data(), 0);
	CPPUNIT_ASSERT_EQUAL(20 + 15, cost(partial, map, 5, 3));
	// A unit at (5,3) sees the same cost and still takes the gap (around is 60).
	int dx = 0, dy = 0;
	CPPUNIT_ASSERT(map.directionByGradient(1, 0, 5, 3, partial.data(), &dx, &dy, true));
	CPPUNIT_ASSERT_EQUAL(-1, dx);
	CPPUNIT_ASSERT_EQUAL(0, dy);

	// The same opposing traffic is free on open ground and beside a single wall.
	map.trafficDirection[map.coordToIndex(2, 3) * 8 + 3] = 16;
	map.trafficDirection[map.coordToIndex(2, 7) * 8 + 3] = 16;
	std::vector<Uint16> open = blank(map);
	open[map.coordToIndex(1, 6)] = GRADIENT_FORBIDDEN; // (2,7) has a wall on one side only
	open[map.coordToIndex(0, 3)] = GRADIENT_AT_GOAL;
	open[map.coordToIndex(0, 7)] = GRADIENT_AT_GOAL;
	map.propagateGradient(open.data(), 0);
	CPPUNIT_ASSERT_EQUAL(30, cost(open, map, 3, 3));
	CPPUNIT_ASSERT_EQUAL(30, cost(open, map, 3, 7));

	// Decay halves the counts.
	map.decayTraffic();
	CPPUNIT_ASSERT_EQUAL(8, (int)map.trafficDirection[map.coordToIndex(4, 3) * 8 + 3]);
}

void GradientTest::testMaxCostStopsPropagation()
{
	GrassMap map;
	std::vector<Uint16> g = blank(map);
	g[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.propagateGradient(g.data(), 0, 20);
	CPPUNIT_ASSERT_EQUAL(20, cost(g, map, 2, 0));
	CPPUNIT_ASSERT_EQUAL(14, cost(g, map, 1, 1));
	CPPUNIT_ASSERT_EQUAL((int)GRADIENT_UNREACHABLE, (int)g[map.coordToIndex(3, 0)]);
	CPPUNIT_ASSERT_EQUAL((int)GRADIENT_UNREACHABLE, (int)g[map.coordToIndex(2, 2)]);
}

void GradientTest::testDirectionPrefersCheapestTotal()
{
	GrassMap map;
	std::vector<Uint16> g = blank(map);
	g[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.propagateGradient(g.data(), 0);
	int dx = 9, dy = 9;
	// From (3,3) the cheapest neighbour is the diagonal (2,2).
	CPPUNIT_ASSERT(map.directionByGradient(1, 0, 3, 3, g.data(), &dx, &dy, true));
	CPPUNIT_ASSERT_EQUAL(-1, dx);
	CPPUNIT_ASSERT_EQUAL(-1, dy);
	// From (3,0) it is straight west.
	CPPUNIT_ASSERT(map.directionByGradient(1, 0, 3, 0, g.data(), &dx, &dy, true));
	CPPUNIT_ASSERT_EQUAL(-1, dx);
	CPPUNIT_ASSERT_EQUAL(0, dy);
	// At the goal: stay.
	CPPUNIT_ASSERT(map.directionByGradient(1, 0, 0, 0, g.data(), &dx, &dy, true));
	CPPUNIT_ASSERT_EQUAL(0, dx);
	CPPUNIT_ASSERT_EQUAL(0, dy);

	// Swimmer of class 5 (water costs 20) at (3,0) with water on (2,0) and (1,0).
	// (2,0) is worth 28 (two land diagonals) minus a 20 water step, (2,1) and
	// (2,7) are worth 24 minus a 14 land diagonal: the unit skirts the water.
	GrassMap water;
	water.putWater(1, 0);
	water.putWater(2, 0);
	std::vector<Uint16> f = blank(water);
	f[water.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	water.propagateGradient(f.data(), 5);
	CPPUNIT_ASSERT_EQUAL(28, cost(f, water, 2, 0));
	CPPUNIT_ASSERT_EQUAL(24, cost(f, water, 2, 1));
	CPPUNIT_ASSERT(water.directionByGradient(1, 5, 3, 0, f.data(), &dx, &dy, true));
	CPPUNIT_ASSERT_EQUAL(-1, dx);
	CPPUNIT_ASSERT(dy != 0); // never straight into the water
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
	CPPUNIT_ASSERT(map.directionByGradient(1, 0, 3, 3, g.data(), &dx, &dy, true));
	CPPUNIT_ASSERT((dx == -1 && dy == 0) || (dx == 0 && dy == -1));
	// Fully surrounded by units: strict fails, and so does the sidestep (no free cell).
	for (int ddy = -1; ddy <= 1; ddy++)
		for (int ddx = -1; ddx <= 1; ddx++)
			if (ddx || ddy)
				map.putGroundUnit(3 + ddx, 3 + ddy);
	CPPUNIT_ASSERT(!map.directionByGradient(1, 0, 3, 3, g.data(), &dx, &dy, true));
	CPPUNIT_ASSERT(!map.directionByGradient(1, 0, 3, 3, g.data(), &dx, &dy, false));
}

void GradientTest::testSwimClassFromSpeeds()
{
	CPPUNIT_ASSERT_EQUAL(0, Map::swimClass(16, 0));
	// Equal speeds: water costs the same as land.
	CPPUNIT_ASSERT_EQUAL(Map::SWIM_CLASS_EVEN, Map::swimClass(30, 30));
	// A slow walker that swims fast prefers water; a fast walker avoids it.
	CPPUNIT_ASSERT(Map::swimClass(16, 30) < Map::SWIM_CLASS_EVEN);
	CPPUNIT_ASSERT(Map::swimClass(30, 10) > Map::SWIM_CLASS_EVEN);
	CPPUNIT_ASSERT_EQUAL(SWIM_CLASS_COUNT - 1, Map::swimClass(30, 10));
}
