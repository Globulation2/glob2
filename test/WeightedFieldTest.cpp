// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "WeightedFieldTest.h"

#include "Map.h"
#include "MapInternal.h"
#include "TerrainType.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

CPPUNIT_TEST_SUITE_REGISTRATION( WeightedFieldTest );

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
		void putWater(int x, int y) { cases[coordToIndex(x, y)].terrain = 256; }
		void putBuilding(int x, int y) { cases[coordToIndex(x, y)].building = 0; }
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
		return 14 * lo + 10 * (hi - lo);
	}

	struct Field
	{
		std::vector<Uint8> seed;
		std::vector<Uint16> cost;
		explicit Field(size_t size) : seed(size, GRADIENT_UNREACHABLE), cost(size, 0) {}
	};
}

void WeightedFieldTest::testOpenGridIsOctileOnTorus()
{
	GrassMap map;
	Field f(map.cells());
	const int gx = 1, gy = 2;
	f.seed[map.coordToIndex(gx, gy)] = GRADIENT_AT_GOAL;
	map.buildWeightedField(f.seed.data(), f.cost.data(), false);
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
		{
			int expected = octile(wrapDist(x, gx, 8), wrapDist(y, gy, 8));
			CPPUNIT_ASSERT_EQUAL(expected, (int)f.cost[map.coordToIndex(x, y)]);
		}
}

void WeightedFieldTest::testObstaclesForcePathAround()
{
	GrassMap map;
	Field f(map.cells());
	// Vertical wall at x=4 spanning all rows except y=7: the only way from
	// x>4 to the goal at (0,0) crosses the gap at (4,7) or wraps around.
	for (int y = 0; y < 7; y++)
		f.seed[map.coordToIndex(4, y)] = GRADIENT_FORBIDDEN;
	f.seed[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.buildWeightedField(f.seed.data(), f.cost.data(), false);
	// (3,0) is adjacent-ish: two cardinal steps west.
	CPPUNIT_ASSERT_EQUAL(30, (int)f.cost[map.coordToIndex(3, 0)]);
	// (5,0): wrap east is 3 cardinal steps (5->6->7->0).
	CPPUNIT_ASSERT_EQUAL(30, (int)f.cost[map.coordToIndex(5, 0)]);
	// Obstacles never get a cost.
	CPPUNIT_ASSERT_EQUAL((int)Map::COST_INFINITY, (int)f.cost[map.coordToIndex(4, 3)]);
	// (4,7) gap cell: diagonal to (3,0) wrapped = 14, then 30.
	CPPUNIT_ASSERT_EQUAL(44, (int)f.cost[map.coordToIndex(4, 7)]);
}

void WeightedFieldTest::testWaterCostsDoubleForSwimmers()
{
	GrassMap map;
	// Water column at x=1..2 across all rows; goal at (0,0). A swimmer at
	// (3,0) can cross two water cells (2*20 = 40) or walk around the torus
	// (5 cardinal steps = 50): crossing wins at 40 + 10 (entering (0,0)) = 50? No:
	// entering (0,0) is land (10); entering (1,0) and (2,0) are water (20 each).
	for (int y = 0; y < 8; y++)
	{
		map.putWater(1, y);
		map.putWater(2, y);
	}
	Field f(map.cells());
	f.seed[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.buildWeightedField(f.seed.data(), f.cost.data(), true);
	// (3,0) -> (2,0) water 20 -> (1,0) water 20 -> (0,0) land 10 = 50.
	// Around: (3,0)->(4,0)->...->(7,0)->(0,0) = 5 land steps = 50. Tie: 50.
	CPPUNIT_ASSERT_EQUAL(50, (int)f.cost[map.coordToIndex(3, 0)]);
	// (2,0) is in the water: leaving it costs entering (1,0)=20 then (0,0)=10.
	CPPUNIT_ASSERT_EQUAL(30, (int)f.cost[map.coordToIndex(2, 0)]);
	// (1,0): enter (0,0) = 10.
	CPPUNIT_ASSERT_EQUAL(10, (int)f.cost[map.coordToIndex(1, 0)]);

	// Non-swimmer: caller marks water forbidden in the seed. Then (3,0) must go around.
	Field g(map.cells());
	for (int y = 0; y < 8; y++)
	{
		g.seed[map.coordToIndex(1, y)] = GRADIENT_FORBIDDEN;
		g.seed[map.coordToIndex(2, y)] = GRADIENT_FORBIDDEN;
	}
	g.seed[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.buildWeightedField(g.seed.data(), g.cost.data(), false);
	CPPUNIT_ASSERT_EQUAL(50, (int)g.cost[map.coordToIndex(3, 0)]);
	CPPUNIT_ASSERT_EQUAL(40, (int)g.cost[map.coordToIndex(4, 0)]);
}

void WeightedFieldTest::testUnreachableCellsStayInfinite()
{
	GrassMap map;
	Field f(map.cells());
	// Enclose (6,6) with obstacles.
	for (int dy = -1; dy <= 1; dy++)
		for (int dx = -1; dx <= 1; dx++)
			if (dx || dy)
				f.seed[map.coordToIndex(6 + dx, 6 + dy)] = GRADIENT_FORBIDDEN;
	f.seed[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.buildWeightedField(f.seed.data(), f.cost.data(), false);
	CPPUNIT_ASSERT_EQUAL((int)Map::COST_INFINITY, (int)f.cost[map.coordToIndex(6, 6)]);
	CPPUNIT_ASSERT_EQUAL(0, (int)f.cost[map.coordToIndex(0, 0)]);
}

void WeightedFieldTest::testGradientWriteback()
{
	GrassMap map;
	Field f(map.cells());
	f.seed[map.coordToIndex(3, 3)] = GRADIENT_FORBIDDEN;
	for (int dy = -1; dy <= 1; dy++)
		for (int dx = -1; dx <= 1; dx++)
			if (dx || dy)
				f.seed[map.coordToIndex(6 + dx, 6 + dy)] = GRADIENT_FORBIDDEN;
	f.seed[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.buildWeightedField(f.seed.data(), f.cost.data(), false);
	std::vector<Uint8> gradient = f.seed;
	map.writeGradientFromCost(f.cost.data(), gradient.data());
	CPPUNIT_ASSERT_EQUAL((int)GRADIENT_AT_GOAL, (int)gradient[map.coordToIndex(0, 0)]);
	CPPUNIT_ASSERT_EQUAL((int)GRADIENT_FORBIDDEN, (int)gradient[map.coordToIndex(3, 3)]);
	CPPUNIT_ASSERT_EQUAL((int)GRADIENT_UNREACHABLE, (int)gradient[map.coordToIndex(6, 6)]);
	// (2,0): cost 20 -> 2 tiles -> 253.
	CPPUNIT_ASSERT_EQUAL(253, (int)gradient[map.coordToIndex(2, 0)]);
	// (1,1): diagonal 14 -> rounds to 1 tile -> 254.
	CPPUNIT_ASSERT_EQUAL(254, (int)gradient[map.coordToIndex(1, 1)]);
}

void WeightedFieldTest::testDirectionByCostPrefersCheapestTotal()
{
	GrassMap map;
	Field f(map.cells());
	f.seed[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.buildWeightedField(f.seed.data(), f.cost.data(), false);
	int dx = 9, dy = 9;
	// From (3,3) the cheapest neighbour is the diagonal (2,2).
	CPPUNIT_ASSERT(map.directionByCost(1, false, 3, 3, f.cost.data(), &dx, &dy, true));
	CPPUNIT_ASSERT_EQUAL(-1, dx);
	CPPUNIT_ASSERT_EQUAL(-1, dy);
	// From (3,0) it is straight west.
	CPPUNIT_ASSERT(map.directionByCost(1, false, 3, 0, f.cost.data(), &dx, &dy, true));
	CPPUNIT_ASSERT_EQUAL(-1, dx);
	CPPUNIT_ASSERT_EQUAL(0, dy);
	// At the goal: stay.
	CPPUNIT_ASSERT(map.directionByCost(1, false, 0, 0, f.cost.data(), &dx, &dy, true));
	CPPUNIT_ASSERT_EQUAL(0, dx);
	CPPUNIT_ASSERT_EQUAL(0, dy);
}

void WeightedFieldTest::testDirectionByCostBlockedNeighbour()
{
	GrassMap map;
	Field f(map.cells());
	f.seed[map.coordToIndex(0, 0)] = GRADIENT_AT_GOAL;
	map.buildWeightedField(f.seed.data(), f.cost.data(), false);
	// Another unit stands on (2,2): from (3,3) the strict move must pick a
	// neighbour that still makes progress, e.g. (2,3) or (3,2) (cost 20+... ),
	// never the occupied diagonal.
	map.putGroundUnit(2, 2);
	int dx = 9, dy = 9;
	CPPUNIT_ASSERT(map.directionByCost(1, false, 3, 3, f.cost.data(), &dx, &dy, true));
	CPPUNIT_ASSERT(!(dx == -1 && dy == -1));
	CPPUNIT_ASSERT((dx == -1 && dy == 0) || (dx == 0 && dy == -1));
	// Fully surrounded by units: strict fails, non-strict also fails (no free cell).
	for (int ddy = -1; ddy <= 1; ddy++)
		for (int ddx = -1; ddx <= 1; ddx++)
			if (ddx || ddy)
				map.putGroundUnit(3 + ddx, 3 + ddy);
	CPPUNIT_ASSERT(!map.directionByCost(1, false, 3, 3, f.cost.data(), &dx, &dy, true));
	CPPUNIT_ASSERT(!map.directionByCost(1, false, 3, 3, f.cost.data(), &dx, &dy, false));
}
