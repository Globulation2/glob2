// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

#include "Glob2Test.h"

// Characterization tests for the spatial-query predicates in MapQuery.cpp:
// isFreeFor*, isHardSpaceFor*. The fixture builds an 8x8 grass map and pokes
// individual tile state to exercise each (predicate × deny-reason) pair.
class MapQueryTest
{

public:
	void testFreeForGroundUnit_CleanGrassPasses();
	void testFreeForGroundUnit_ResourceFails();
	void testFreeForGroundUnit_BuildingFails();
	void testFreeForGroundUnit_UnitFails();
	void testFreeForGroundUnit_WaterFailsWhenNotSwim();
	void testFreeForGroundUnit_WaterPassesWhenSwim();
	void testFreeForGroundUnit_ForbiddenFailsWhenMaskMatches();
	void testFreeForGroundUnit_ForbiddenPassesWhenMaskDoesNotMatch();

	void testFreeForGroundUnitNoForbidden_IgnoresForbidden();
	void testFreeForGroundUnitNoForbidden_StillBlocksBuilding();

	void testFreeForBuilding_GrassPasses();
	void testFreeForBuilding_ResourceFails();
	void testFreeForBuilding_BuildingFails();
	void testFreeForBuilding_UnitFails();
	void testFreeForBuilding_WaterFails();
	void testFreeForBuilding_SandFails();
	void testFreeForBuilding_RectAllGrassPasses();
	void testFreeForBuilding_RectOneBadTileFails();
	void testFreeForBuilding_RectGidTolerantSameGidPasses();
	void testFreeForBuilding_RectGidTolerantDifferentGidFails();

	void testHardSpaceForGroundUnit_IgnoresUnit();
	void testHardSpaceForGroundUnit_ResourceStillFails();
	void testHardSpaceForGroundUnit_BuildingStillFails();
	void testHardSpaceForGroundUnit_WaterFailsWhenNotSwim();
	void testHardSpaceForGroundUnit_ForbiddenStillFails();

	void testHardSpaceForBuilding_IgnoresUnit();
	void testHardSpaceForBuilding_ResourceFails();
	void testHardSpaceForBuilding_BuildingFails();
	void testHardSpaceForBuilding_NonGrassFails();
	void testHardSpaceForBuilding_RectAllGrassPasses();
	void testHardSpaceForBuilding_RectGidTolerantSameGidPasses();
	void testHardSpaceForBuilding_RectGidTolerantDifferentGidFails();

	void testLocalTeam_DefaultsToSentinel();
	void testLocalTeam_SetAndGet();
	void testLocalTeam_SentinelValueIsMinusOne();
};
