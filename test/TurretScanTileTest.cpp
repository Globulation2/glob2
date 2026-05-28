// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

/*********************************************************
 *
 * Regression test for BuildingUtils::turretScanTile — the pure octant geometry
 * extracted from Building::turretStep (smell CS-529). The eight-case switch is
 * the highest transcription-risk part of the turret target scan; these tests
 * pin the exact tile mapping so a port (or future refactor) can be checked
 * against the original C++ behaviour without running a full game.
 *
 * Links only BuildingUtils.cpp (already in TestsRunner's sources) — no Game,
 * Team, Map, or globalContainer needed, because turretScanTile is static and
 * touches no instance state.
 *
 ********************************************************/

#include <cppunit/extensions/HelperMacros.h>

#include "BuildingUtils.h"

class TurretScanTileTest: public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(TurretScanTileTest);
		CPPUNIT_TEST(testAllOctantsAtRing);
		CPPUNIT_TEST(testRingZeroOffsetZero);
		CPPUNIT_TEST(testRingCoversFullSquare);
	CPPUNIT_TEST_SUITE_END();

private:
	// Convenience wrapper returning the (x,y) for one octant call.
	static void scan(int posX, int posY, int ring, int offset, int octant,
	                 int& x, int& y)
	{
		BuildingUtils::turretScanTile(posX, posY, ring, offset, octant, x, y);
	}

	static void assertTile(int posX, int posY, int ring, int offset, int octant,
	                       int expectedX, int expectedY)
	{
		int x = -999, y = -999;
		scan(posX, posY, ring, offset, octant, x, y);
		CPPUNIT_ASSERT_EQUAL(expectedX, x);
		CPPUNIT_ASSERT_EQUAL(expectedY, y);
	}

protected:
	// Pin the exact coordinate produced by each of the eight octants. These
	// values mirror the original switch(k) in Building::turretStep verbatim.
	void testAllOctantsAtRing(void)
	{
		const int posX = 10, posY = 20, i = 3, j = 1;
		assertTile(posX, posY, i, j, 0, posX-j,   posY-i);
		assertTile(posX, posY, i, j, 1, posX+j+1, posY-i);
		assertTile(posX, posY, i, j, 2, posX-j,   posY+i+1);
		assertTile(posX, posY, i, j, 3, posX+j+1, posY+i+1);
		assertTile(posX, posY, i, j, 4, posX-i,   posY-j);
		assertTile(posX, posY, i, j, 5, posX+i+1, posY-j);
		assertTile(posX, posY, i, j, 6, posX-i,   posY+j+1);
		assertTile(posX, posY, i, j, 7, posX+i+1, posY+j+1);
	}

	// At ring 0 with offset 0 the eight octants address the four tiles of the
	// turret's own 2x2 footprint corners, each hit twice.
	void testRingZeroOffsetZero(void)
	{
		const int posX = 5, posY = 7;
		assertTile(posX, posY, 0, 0, 0, posX,   posY);
		assertTile(posX, posY, 0, 0, 1, posX+1, posY);
		assertTile(posX, posY, 0, 0, 2, posX,   posY+1);
		assertTile(posX, posY, 0, 0, 3, posX+1, posY+1);
		assertTile(posX, posY, 0, 0, 4, posX,   posY);
		assertTile(posX, posY, 0, 0, 5, posX+1, posY);
		assertTile(posX, posY, 0, 0, 6, posX,   posY+1);
		assertTile(posX, posY, 0, 0, 7, posX+1, posY+1);
	}

	// Every tile produced for a full ring must lie on the square boundary at
	// Chebyshev distance `ring` from the turret footprint (top-left at
	// posX-ring..posX+1+ring), confirming the scan tiles the ring without
	// straying inside or outside it.
	void testRingCoversFullSquare(void)
	{
		const int posX = 40, posY = 50, ring = 4;
		const int minX = posX - ring, maxX = posX + 1 + ring;
		const int minY = posY - ring, maxY = posY + 1 + ring;
		for (int j = 0; j <= ring; j++)
		{
			for (int k = 0; k < 8; k++)
			{
				int x = 0, y = 0;
				scan(posX, posY, ring, j, k, x, y);
				CPPUNIT_ASSERT(x >= minX && x <= maxX);
				CPPUNIT_ASSERT(y >= minY && y <= maxY);
				// must touch at least one of the four ring edges
				const bool onEdge = (x == minX) || (x == maxX) ||
				                    (y == minY) || (y == maxY);
				CPPUNIT_ASSERT(onEdge);
			}
		}
	}
};
CPPUNIT_TEST_SUITE_REGISTRATION(TurretScanTileTest);
