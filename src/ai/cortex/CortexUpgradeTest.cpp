// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Globulation 2 Authors
//
// Verification tests for the AICortex Phase-2 (building-upgrade) increment.
// These exercise the PURE, header-only logic that does not need the engine:
//   1. The long-level histogram decoders in CortexTypes.h (the encoding the
//      observation and policy both rely on: odd long-levels are FINISHED,
//      even are SITES; mid-upgrade == a site of a raised level).
//   2. The makeUpgradeAction factory contract.
//
// Engine-backed upgrade eligibility and emitted worker columns are exercised
// by CortexActionCoverageTest.cpp.

#include "Glob2Test.h"

#include "../src/ai/cortex/CortexTypes.h"

using namespace Cortex;

class CortexUpgradeTest
{

public:
	CortexUpgradeTest() {}
	~CortexUpgradeTest() {}

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
		CHECK_EQ((Sint32)1, cortexFinishedBuildings(obs, type));
		CHECK_EQ((Sint32)1, cortexBuildingSites(obs, type));
		// minLevel filter: nothing finished at level >= 1 yet.
		CHECK_EQ((Sint32)0, cortexFinishedBuildingsMinLevel(obs, type, 1));
	}

	// C++: cortexMaxFinishedLevel scans odd slots high-to-low, -1 if none.
	void testMaxFinishedLevel(void)
	{
		CortexObservation obs = makeEmptyObservation();
		const int type = CORTEX_BUILD_SCIENCE;
		CHECK_EQ((Sint32)-1, cortexMaxFinishedLevel(obs, type)); // none

		obs.buildingCountPerLevel[type][1] = 1; // finished level 0
		CHECK_EQ((Sint32)0, cortexMaxFinishedLevel(obs, type));

		obs.buildingCountPerLevel[type][5] = 1; // finished level 2
		CHECK_EQ((Sint32)2, cortexMaxFinishedLevel(obs, type));

		// A SITE must NOT count as a finished level.
		CortexObservation obs2 = makeEmptyObservation();
		obs2.buildingCountPerLevel[type][4] = 1; // level-2 site only
		CHECK_EQ((Sint32)-1, cortexMaxFinishedLevel(obs2, type));
	}

	// C++: cortexBuildingsUpgrading == slot2 + slot4 (sites of a RAISED level);
	// a fresh level-0 site (slot 0) is a NEW build, must be excluded. This is
	// the guard the policy uses to avoid stacking a second upgrade.
	void testBuildingsUpgradingExcludesFreshSite(void)
	{
		CortexObservation obs = makeEmptyObservation();
		const int type = CORTEX_BUILD_ATTACK;

		obs.buildingCountPerLevel[type][0] = 1; // fresh level-0 site (new build)
		CHECK_EQ((Sint32)0, cortexBuildingsUpgrading(obs, type));

		obs.buildingCountPerLevel[type][2] = 1; // 0->1 upgrade site
		CHECK_EQ((Sint32)1, cortexBuildingsUpgrading(obs, type));

		obs.buildingCountPerLevel[type][4] = 1; // 1->2 upgrade site
		CHECK_EQ((Sint32)2, cortexBuildingsUpgrading(obs, type));
	}

	// C++: CortexTypes.h:460 makeUpgradeAction sets kind + buildingType, leaves
	// the rest at the no-op defaults; the version must be stamped.
	void testMakeUpgradeAction(void)
	{
		CortexAction a = makeUpgradeAction(CORTEX_BUILD_ATTACK);
		CHECK_EQ((Uint32)ACTION_VERSION, a.version);
		CHECK_EQ((Sint32)ACTION_UPGRADE_BUILDING, a.kind);
		CHECK_EQ((Sint32)CORTEX_BUILD_ATTACK, a.buildingType);
		// Unused params keep their no-op sentinels.
		CHECK_EQ((Sint32)-1, a.locationSlot);
		CHECK_EQ((Sint32)-1, a.flagRadius);
		CHECK_EQ((Sint32)-1, a.unitCount);
	}

	// C++: CortexTypes.h — the observation layout is at v23 (per-class production policy bindings);
	// the action layout is at v14 (catalog-independent role requests). Bump these in lockstep with the OBSERVATION_VERSION /
	// ACTION_VERSION constants.
	void testVersionBump(void)
	{
		CHECK_EQ((Uint32)23, (Uint32)OBSERVATION_VERSION);
		CHECK_EQ((Uint32)14, (Uint32)ACTION_VERSION);
		// makeEmptyObservation must stamp the current version (so a stale
		// observation from an old layout is rejected by the policy).
		CortexObservation obs = makeEmptyObservation();
		CHECK_EQ((Uint32)OBSERVATION_VERSION, obs.version);
	}
};
TEST_SUITE("CortexUpgrade")
{
	TEST_CASE_FIXTURE(CortexUpgradeTest, "LongLevelFinishedVsSite") { testLongLevelFinishedVsSite(); }
	TEST_CASE_FIXTURE(CortexUpgradeTest, "MaxFinishedLevel") { testMaxFinishedLevel(); }
	TEST_CASE_FIXTURE(CortexUpgradeTest, "BuildingsUpgradingExcludesFreshSite") { testBuildingsUpgradingExcludesFreshSite(); }
	TEST_CASE_FIXTURE(CortexUpgradeTest, "MakeUpgradeAction") { testMakeUpgradeAction(); }
	TEST_CASE_FIXTURE(CortexUpgradeTest, "VersionBump") { testVersionBump(); }
}
