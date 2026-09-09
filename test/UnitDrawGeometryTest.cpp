// SPDX-License-Identifier: GPL-3.0-or-later
#include <cppunit/extensions/HelperMacros.h>
#include "UnitDrawGeometry.h"

// Regression cover for the enter-the-building animation: a unit entering a
// building keeps its map slot on the tile it is leaving, so the draw loop
// visits it one square behind its own position.
class UnitDrawGeometryTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(UnitDrawGeometryTest);
	CPPUNIT_TEST(testSlotOnUnitPosition);
	CPPUNIT_TEST(testSlotLagsWhileEnteringBuilding);
	CPPUNIT_TEST(testSeamOccurrencesStayDistinct);
	CPPUNIT_TEST(testSeamOccurrencesWhileEnteringBuilding);
	CPPUNIT_TEST_SUITE_END();

	static constexpr int MAP_SIZE = 64;

public:
	//! Every action but entering a building claims the destination map slot,
	//! so the visited tile is already the unit's tile and must be kept as-is.
	void testSlotOnUnitPosition()
	{
		for (int viewport = 0; viewport < MAP_SIZE; ++viewport)
			for (int slotTile = -1; slotTile <= 20; ++slotTile)
			{
				const int unitPos = (slotTile + viewport) & (MAP_SIZE - 1);
				CPPUNIT_ASSERT_EQUAL(slotTile,
					unitDrawTile(slotTile, viewport, unitPos, MAP_SIZE));
			}
	}

	//! Unit::handleActionEnteringBuilding advances posX/posY onto the building
	//! tile but leaves the map slot behind, in any of the eight directions.
	//! The anchor must follow the position, not the stale slot — otherwise the
	//! arrival interpolation runs a square early and the unit walks backwards.
	void testSlotLagsWhileEnteringBuilding()
	{
		for (int step = -1; step <= 1; ++step)
			for (int viewport = 0; viewport < MAP_SIZE; ++viewport)
				for (int slotTile = -1; slotTile <= 20; ++slotTile)
				{
					const int unitPos = (slotTile + viewport + step) & (MAP_SIZE - 1);
					CPPUNIT_ASSERT_EQUAL(slotTile + step,
						unitDrawTile(slotTile, viewport, unitPos, MAP_SIZE));
				}
	}

	//! A wrapped tile visible at both screen edges is visited twice; each
	//! occurrence must keep its own anchor so both copies of the unit are drawn.
	void testSeamOccurrencesStayDistinct()
	{
		// Map tile 63 shows up as viewport-relative -1 and as 63.
		CPPUNIT_ASSERT_EQUAL(-1, unitDrawTile(-1, 0, MAP_SIZE - 1, MAP_SIZE));
		CPPUNIT_ASSERT_EQUAL(MAP_SIZE - 1, unitDrawTile(MAP_SIZE - 1, 0, MAP_SIZE - 1, MAP_SIZE));
	}

	//! Both properties at once: the seam copies stay distinct while each still
	//! tracks the one-square lag of a unit entering a building across the seam.
	void testSeamOccurrencesWhileEnteringBuilding()
	{
		// Slot on tile 63, building tile 0: the left-edge occurrence walks into
		// tile 0 and the right-edge one walks off past tile 63.
		CPPUNIT_ASSERT_EQUAL(0, unitDrawTile(-1, 0, 0, MAP_SIZE));
		CPPUNIT_ASSERT_EQUAL(MAP_SIZE, unitDrawTile(MAP_SIZE - 1, 0, 0, MAP_SIZE));
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(UnitDrawGeometryTest);
