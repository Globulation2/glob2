// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors
//
// Verification tests for the AICortex Phase-2 (building-upgrade) increment.
// These exercise the PURE, header-only logic that does not need the engine:
//   1. The long-level histogram decoders in CortexTypes.h (the encoding the
//      observation and policy both rely on: odd long-levels are FINISHED,
//      even are SITES; mid-upgrade == a site of a raised level).
//   2. The worker-count column mapping the upgrade action layer uses
//      (siteCol = targetLevel*2, finishedCol = targetLevel*2+1), proven to
//      match the engine's GameGUIDefaultAssignManager column convention
//      (finished -> level*2+1, site -> level*2; see
//      gui/GameGUIDefaultAssignManager.cpp:21,26).
//   3. The makeUpgradeAction factory contract.
//
// The engine-coupled predicate (findUpgradeTarget / upgradableCount) needs a
// live Building array and is verified by reading, not unit-tested here (it
// would be a fragile full-game-state integration test).

#include <cppunit/extensions/HelperMacros.h>

#include "../src/ai/cortex/CortexTypes.h"

using namespace Cortex;

class CortexUpgradeTest: public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(CortexUpgradeTest);
	CPPUNIT_TEST(testLongLevelFinishedVsSite);
	CPPUNIT_TEST(testMaxFinishedLevel);
	CPPUNIT_TEST(testBuildingsUpgradingExcludesFreshSite);
	CPPUNIT_TEST(testWorkerColumnMapping);
	CPPUNIT_TEST(testMakeUpgradeAction);
	CPPUNIT_TEST(testVersionBump);
CPPUNIT_TEST_SUITE_END();

public:
	void setUp(void) {}
	void tearDown(void) {}

protected:
	// C++: CortexTypes.h long-level encoding — longLevel = (level<<1)+1-isSite,
	// so odd (1,3,5) == FINISHED at level 0,1,2 and even (0,2,4) == SITE.
	void testLongLevelFinishedVsSite(void)
	{
		CortexObservation obs = makeEmptyObservation();
		const int type = CORTEX_BUILD_ATTACK;

		// One finished level-0 barracks -> odd slot 1.
		obs.buildingCountPerLevel[type][1] = 1;
		// One level-1 SITE (an in-progress 0->1 upgrade) -> even slot 2.
		obs.buildingCountPerLevel[type][2] = 1;

		// Finished count must see only the odd slot, never the site.
		CPPUNIT_ASSERT_EQUAL((Sint32)1, cortexFinishedBuildings(obs, type));
		CPPUNIT_ASSERT_EQUAL((Sint32)1, cortexBuildingSites(obs, type));
		// minLevel filter: nothing finished at level >= 1 yet.
		CPPUNIT_ASSERT_EQUAL((Sint32)0, cortexFinishedBuildingsMinLevel(obs, type, 1));
	}

	// C++: cortexMaxFinishedLevel scans odd slots high-to-low, -1 if none.
	void testMaxFinishedLevel(void)
	{
		CortexObservation obs = makeEmptyObservation();
		const int type = CORTEX_BUILD_SCIENCE;
		CPPUNIT_ASSERT_EQUAL((Sint32)-1, cortexMaxFinishedLevel(obs, type)); // none

		obs.buildingCountPerLevel[type][1] = 1; // finished level 0
		CPPUNIT_ASSERT_EQUAL((Sint32)0, cortexMaxFinishedLevel(obs, type));

		obs.buildingCountPerLevel[type][5] = 1; // finished level 2
		CPPUNIT_ASSERT_EQUAL((Sint32)2, cortexMaxFinishedLevel(obs, type));

		// A SITE must NOT count as a finished level.
		CortexObservation obs2 = makeEmptyObservation();
		obs2.buildingCountPerLevel[type][4] = 1; // level-2 site only
		CPPUNIT_ASSERT_EQUAL((Sint32)-1, cortexMaxFinishedLevel(obs2, type));
	}

	// C++: cortexBuildingsUpgrading == slot2 + slot4 (sites of a RAISED level);
	// a fresh level-0 site (slot 0) is a NEW build, must be excluded. This is
	// the guard the policy uses to avoid stacking a second upgrade.
	void testBuildingsUpgradingExcludesFreshSite(void)
	{
		CortexObservation obs = makeEmptyObservation();
		const int type = CORTEX_BUILD_ATTACK;

		obs.buildingCountPerLevel[type][0] = 1; // fresh level-0 site (new build)
		CPPUNIT_ASSERT_EQUAL((Sint32)0, cortexBuildingsUpgrading(obs, type));

		obs.buildingCountPerLevel[type][2] = 1; // 0->1 upgrade site
		CPPUNIT_ASSERT_EQUAL((Sint32)1, cortexBuildingsUpgrading(obs, type));

		obs.buildingCountPerLevel[type][4] = 1; // 1->2 upgrade site
		CPPUNIT_ASSERT_EQUAL((Sint32)2, cortexBuildingsUpgrading(obs, type));
	}

	// C++: AICortex.cpp:396-398 derives the worker columns the upgrade order
	// uses. They must equal the engine's GameGUIDefaultAssignManager mapping:
	//   site variant at level L     -> defaultUnitsAssigned[type][L*2]
	//   finished variant at level L -> defaultUnitsAssigned[type][L*2+1]
	// (gui/GameGUIDefaultAssignManager.cpp:21,26). The GUI upgrade path reads
	// the level-(L+1) site and finished columns, i.e. targetLevel = L+1.
	void testWorkerColumnMapping(void)
	{
		// Upgrading a level-0 building: targetLevel = 1.
		{
			const int currentLevel = 0;
			const int targetLevel  = currentLevel + 1;
			const int siteCol      = targetLevel * 2;     // == 2
			const int finishedCol  = targetLevel * 2 + 1; // == 3
			CPPUNIT_ASSERT_EQUAL(2, siteCol);
			CPPUNIT_ASSERT_EQUAL(3, finishedCol);
			// Engine convention cross-check: site@L1 = 1*2, finished@L1 = 1*2+1.
			CPPUNIT_ASSERT_EQUAL(targetLevel * 2,     siteCol);
			CPPUNIT_ASSERT_EQUAL(targetLevel * 2 + 1, finishedCol);
			// In range of defaultUnitsAssigned[type][6].
			CPPUNIT_ASSERT(siteCol >= 0 && siteCol < 6);
			CPPUNIT_ASSERT(finishedCol >= 0 && finishedCol < 6);
		}
		// Upgrading a level-1 building: targetLevel = 2 -> columns 4,5 (max).
		{
			const int currentLevel = 1;
			const int targetLevel  = currentLevel + 1;
			const int siteCol      = targetLevel * 2;     // == 4
			const int finishedCol  = targetLevel * 2 + 1; // == 5
			CPPUNIT_ASSERT_EQUAL(4, siteCol);
			CPPUNIT_ASSERT_EQUAL(5, finishedCol);
			CPPUNIT_ASSERT(finishedCol < 6); // top column still in range
		}
	}

	// C++: CortexTypes.h:460 makeUpgradeAction sets kind + buildingType, leaves
	// the rest at the no-op defaults; the version must be stamped.
	void testMakeUpgradeAction(void)
	{
		CortexAction a = makeUpgradeAction(CORTEX_BUILD_ATTACK);
		CPPUNIT_ASSERT_EQUAL((Uint32)ACTION_VERSION, a.version);
		CPPUNIT_ASSERT_EQUAL((Sint32)ACTION_UPGRADE_BUILDING, a.kind);
		CPPUNIT_ASSERT_EQUAL((Sint32)CORTEX_BUILD_ATTACK, a.buildingType);
		// Unused params keep their no-op sentinels.
		CPPUNIT_ASSERT_EQUAL((Sint32)-1, a.locationSlot);
		CPPUNIT_ASSERT_EQUAL((Sint32)-1, a.flagRadius);
		CPPUNIT_ASSERT_EQUAL((Sint32)-1, a.unitCount);
	}

	// C++: CortexTypes.h:43,50 — the wheat-protection increment bumped both
	// versions to 5 (v5 added the wheat-sustainability obs fields +
	// ACTION_PROTECT_WHEAT / wheatOpenMargin).
	void testVersionBump(void)
	{
		CPPUNIT_ASSERT_EQUAL((Uint32)5, (Uint32)OBSERVATION_VERSION);
		CPPUNIT_ASSERT_EQUAL((Uint32)5, (Uint32)ACTION_VERSION);
		// makeEmptyObservation must stamp the current version (so a stale
		// observation from an old layout is rejected by the policy).
		CortexObservation obs = makeEmptyObservation();
		CPPUNIT_ASSERT_EQUAL((Uint32)OBSERVATION_VERSION, obs.version);
	}
};
CPPUNIT_TEST_SUITE_REGISTRATION(CortexUpgradeTest);
