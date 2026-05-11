// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

#include <cppunit/extensions/HelperMacros.h>

// Characterization tests for the spatial-query predicates in MapQuery.cpp:
// isFreeFor*, isHardSpaceFor*. The fixture builds an 8x8 grass map and pokes
// individual tile state to exercise each (predicate × deny-reason) pair.
class MapQueryTest: public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE( MapQueryTest );
		// isFreeForGroundUnit(x, y, canSwim, teamMask)
		CPPUNIT_TEST( testFreeForGroundUnit_CleanGrassPasses );
		CPPUNIT_TEST( testFreeForGroundUnit_RessourceFails );
		CPPUNIT_TEST( testFreeForGroundUnit_BuildingFails );
		CPPUNIT_TEST( testFreeForGroundUnit_UnitFails );
		CPPUNIT_TEST( testFreeForGroundUnit_WaterFailsWhenNotSwim );
		CPPUNIT_TEST( testFreeForGroundUnit_WaterPassesWhenSwim );
		CPPUNIT_TEST( testFreeForGroundUnit_ForbiddenFailsWhenMaskMatches );
		CPPUNIT_TEST( testFreeForGroundUnit_ForbiddenPassesWhenMaskDoesNotMatch );

		// isFreeForGroundUnitNoForbidden(x, y, canSwim)
		CPPUNIT_TEST( testFreeForGroundUnitNoForbidden_IgnoresForbidden );
		CPPUNIT_TEST( testFreeForGroundUnitNoForbidden_StillBlocksBuilding );

		// isFreeForBuilding(x, y) and rect variants
		CPPUNIT_TEST( testFreeForBuilding_GrassPasses );
		CPPUNIT_TEST( testFreeForBuilding_RessourceFails );
		CPPUNIT_TEST( testFreeForBuilding_BuildingFails );
		CPPUNIT_TEST( testFreeForBuilding_UnitFails );
		CPPUNIT_TEST( testFreeForBuilding_WaterFails );
		CPPUNIT_TEST( testFreeForBuilding_SandFails );
		CPPUNIT_TEST( testFreeForBuilding_RectAllGrassPasses );
		CPPUNIT_TEST( testFreeForBuilding_RectOneBadTileFails );
		CPPUNIT_TEST( testFreeForBuilding_RectGidTolerantSameGidPasses );
		CPPUNIT_TEST( testFreeForBuilding_RectGidTolerantDifferentGidFails );

		// isHardSpaceForGroundUnit(x, y, canSwim, me)
		CPPUNIT_TEST( testHardSpaceForGroundUnit_IgnoresUnit );
		CPPUNIT_TEST( testHardSpaceForGroundUnit_RessourceStillFails );
		CPPUNIT_TEST( testHardSpaceForGroundUnit_BuildingStillFails );
		CPPUNIT_TEST( testHardSpaceForGroundUnit_WaterFailsWhenNotSwim );
		CPPUNIT_TEST( testHardSpaceForGroundUnit_ForbiddenStillFails );

		// isHardSpaceForBuilding family
		CPPUNIT_TEST( testHardSpaceForBuilding_IgnoresUnit );
		CPPUNIT_TEST( testHardSpaceForBuilding_RessourceFails );
		CPPUNIT_TEST( testHardSpaceForBuilding_BuildingFails );
		CPPUNIT_TEST( testHardSpaceForBuilding_NonGrassFails );
		CPPUNIT_TEST( testHardSpaceForBuilding_RectAllGrassPasses );
		CPPUNIT_TEST( testHardSpaceForBuilding_RectGidTolerantSameGidPasses );
		CPPUNIT_TEST( testHardSpaceForBuilding_RectGidTolerantDifferentGidFails );

		// Local-team mirror (CS-546): Map carries the locally-displayed team identity
		// so sim code can consult it without reaching into GameGUI.
		CPPUNIT_TEST( testLocalTeam_DefaultsToSentinel );
		CPPUNIT_TEST( testLocalTeam_SetAndGet );
		CPPUNIT_TEST( testLocalTeam_SentinelValueIsMinusOne );
	CPPUNIT_TEST_SUITE_END();

public:
	void testFreeForGroundUnit_CleanGrassPasses();
	void testFreeForGroundUnit_RessourceFails();
	void testFreeForGroundUnit_BuildingFails();
	void testFreeForGroundUnit_UnitFails();
	void testFreeForGroundUnit_WaterFailsWhenNotSwim();
	void testFreeForGroundUnit_WaterPassesWhenSwim();
	void testFreeForGroundUnit_ForbiddenFailsWhenMaskMatches();
	void testFreeForGroundUnit_ForbiddenPassesWhenMaskDoesNotMatch();

	void testFreeForGroundUnitNoForbidden_IgnoresForbidden();
	void testFreeForGroundUnitNoForbidden_StillBlocksBuilding();

	void testFreeForBuilding_GrassPasses();
	void testFreeForBuilding_RessourceFails();
	void testFreeForBuilding_BuildingFails();
	void testFreeForBuilding_UnitFails();
	void testFreeForBuilding_WaterFails();
	void testFreeForBuilding_SandFails();
	void testFreeForBuilding_RectAllGrassPasses();
	void testFreeForBuilding_RectOneBadTileFails();
	void testFreeForBuilding_RectGidTolerantSameGidPasses();
	void testFreeForBuilding_RectGidTolerantDifferentGidFails();

	void testHardSpaceForGroundUnit_IgnoresUnit();
	void testHardSpaceForGroundUnit_RessourceStillFails();
	void testHardSpaceForGroundUnit_BuildingStillFails();
	void testHardSpaceForGroundUnit_WaterFailsWhenNotSwim();
	void testHardSpaceForGroundUnit_ForbiddenStillFails();

	void testHardSpaceForBuilding_IgnoresUnit();
	void testHardSpaceForBuilding_RessourceFails();
	void testHardSpaceForBuilding_BuildingFails();
	void testHardSpaceForBuilding_NonGrassFails();
	void testHardSpaceForBuilding_RectAllGrassPasses();
	void testHardSpaceForBuilding_RectGidTolerantSameGidPasses();
	void testHardSpaceForBuilding_RectGidTolerantDifferentGidFails();

	void testLocalTeam_DefaultsToSentinel();
	void testLocalTeam_SetAndGet();
	void testLocalTeam_SentinelValueIsMinusOne();
};
