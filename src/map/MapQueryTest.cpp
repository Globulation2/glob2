// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "MapQueryTest.h"

#include "Map.h"
#include "TerrainType.h"

TEST_SUITE("MapQuery")
{
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForGroundUnit_CleanGrassPasses") { testFreeForGroundUnit_CleanGrassPasses(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForGroundUnit_ResourceFails") { testFreeForGroundUnit_ResourceFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForGroundUnit_BuildingFails") { testFreeForGroundUnit_BuildingFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForGroundUnit_UnitFails") { testFreeForGroundUnit_UnitFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForGroundUnit_WaterFailsWhenNotSwim") { testFreeForGroundUnit_WaterFailsWhenNotSwim(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForGroundUnit_WaterPassesWhenSwim") { testFreeForGroundUnit_WaterPassesWhenSwim(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForGroundUnit_ForbiddenFailsWhenMaskMatches") { testFreeForGroundUnit_ForbiddenFailsWhenMaskMatches(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForGroundUnit_ForbiddenPassesWhenMaskDoesNotMatch") { testFreeForGroundUnit_ForbiddenPassesWhenMaskDoesNotMatch(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForGroundUnitNoForbidden_IgnoresForbidden") { testFreeForGroundUnitNoForbidden_IgnoresForbidden(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForGroundUnitNoForbidden_StillBlocksBuilding") { testFreeForGroundUnitNoForbidden_StillBlocksBuilding(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForBuilding_GrassPasses") { testFreeForBuilding_GrassPasses(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForBuilding_ResourceFails") { testFreeForBuilding_ResourceFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForBuilding_BuildingFails") { testFreeForBuilding_BuildingFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForBuilding_UnitFails") { testFreeForBuilding_UnitFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForBuilding_WaterFails") { testFreeForBuilding_WaterFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForBuilding_SandFails") { testFreeForBuilding_SandFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForBuilding_RectAllGrassPasses") { testFreeForBuilding_RectAllGrassPasses(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForBuilding_RectOneBadTileFails") { testFreeForBuilding_RectOneBadTileFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForBuilding_RectGidTolerantSameGidPasses") { testFreeForBuilding_RectGidTolerantSameGidPasses(); }
	TEST_CASE_FIXTURE(MapQueryTest, "FreeForBuilding_RectGidTolerantDifferentGidFails") { testFreeForBuilding_RectGidTolerantDifferentGidFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "HardSpaceForGroundUnit_IgnoresUnit") { testHardSpaceForGroundUnit_IgnoresUnit(); }
	TEST_CASE_FIXTURE(MapQueryTest, "HardSpaceForGroundUnit_ResourceStillFails") { testHardSpaceForGroundUnit_ResourceStillFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "HardSpaceForGroundUnit_BuildingStillFails") { testHardSpaceForGroundUnit_BuildingStillFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "HardSpaceForGroundUnit_WaterFailsWhenNotSwim") { testHardSpaceForGroundUnit_WaterFailsWhenNotSwim(); }
	TEST_CASE_FIXTURE(MapQueryTest, "HardSpaceForGroundUnit_ForbiddenStillFails") { testHardSpaceForGroundUnit_ForbiddenStillFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "HardSpaceForBuilding_IgnoresUnit") { testHardSpaceForBuilding_IgnoresUnit(); }
	TEST_CASE_FIXTURE(MapQueryTest, "HardSpaceForBuilding_ResourceFails") { testHardSpaceForBuilding_ResourceFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "HardSpaceForBuilding_BuildingFails") { testHardSpaceForBuilding_BuildingFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "HardSpaceForBuilding_NonGrassFails") { testHardSpaceForBuilding_NonGrassFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "HardSpaceForBuilding_RectAllGrassPasses") { testHardSpaceForBuilding_RectAllGrassPasses(); }
	TEST_CASE_FIXTURE(MapQueryTest, "HardSpaceForBuilding_RectGidTolerantSameGidPasses") { testHardSpaceForBuilding_RectGidTolerantSameGidPasses(); }
	TEST_CASE_FIXTURE(MapQueryTest, "HardSpaceForBuilding_RectGidTolerantDifferentGidFails") { testHardSpaceForBuilding_RectGidTolerantDifferentGidFails(); }
	TEST_CASE_FIXTURE(MapQueryTest, "LocalTeam_DefaultsToSentinel") { testLocalTeam_DefaultsToSentinel(); }
	TEST_CASE_FIXTURE(MapQueryTest, "LocalTeam_SetAndGet") { testLocalTeam_SetAndGet(); }
	TEST_CASE_FIXTURE(MapQueryTest, "LocalTeam_SentinelValueIsMinusOne") { testLocalTeam_SentinelValueIsMinusOne(); }
}

namespace
{
	constexpr int kMapDec = 3;          // 8x8 = 1<<3

	// Minimal Map for predicate testing.
	//
	// We bypass Map::setSize() because it instantiates Sector[], which drags in
	// most of the game (Bullet, GameEvent, Building::kill, globalContainer, ...).
	// The isFreeFor*/isHardSpaceFor* predicates only need: a sized cases[] vector,
	// and w/h/wMask/hMask/wDec/hDec for coordToIndex(). We set those directly.
	//
	// The fixture resets its dimension metadata before base cleanup.
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
			tiles.assign(size, Tile());   // Tile() defaults: terrain=0 (grass), no building, no unit
		}
		~GrassMap()
		{
			w = h = 0;
			wMask = hMask = 0;
			wDec = hDec = 0;
			size = 0;
		}

		void putBuilding(int x, int y, Uint16 gbid = 0)
		{
			tiles[coordToIndex(x, y)].building = gbid;
		}
		void putGroundUnit(int x, int y, Uint16 guid = 0)
		{
			tiles[coordToIndex(x, y)].groundUnit = guid;
		}
		void putResource(int x, int y, int type = 0)
		{
			Resource &r = tiles[coordToIndex(x, y)].resource;
			r.type = type;
			r.amount = 1;
			r.variety = 0;
			r.animation = 0;
		}
		void setForbidden(int x, int y, Uint32 mask)
		{
			tiles[coordToIndex(x, y)].forbidden = mask;
		}
		// Terrain encoding (see Map.h:336-361):
		//   grass : terrain <  16
		//   sand  : 128..143
		//   water : 256..271
		void makeWater(int x, int y)
		{
			tiles[coordToIndex(x, y)].terrain = 256;
		}
		void makeSand(int x, int y)
		{
			tiles[coordToIndex(x, y)].terrain = 128;
		}
	};

	constexpr Uint32 kTeam0 = 0x00000001u;
	constexpr Uint32 kTeam1 = 0x00000002u;
}

// ---------------- isFreeForGroundUnit ----------------

void MapQueryTest::testFreeForGroundUnit_CleanGrassPasses()
{
	GrassMap g;
	CHECK(g.isFreeForGroundUnit(3, 3, false, kTeam0));
}

void MapQueryTest::testFreeForGroundUnit_ResourceFails()
{
	GrassMap g; g.putResource(3, 3);
	CHECK(!g.isFreeForGroundUnit(3, 3, false, kTeam0));
}

void MapQueryTest::testFreeForGroundUnit_BuildingFails()
{
	GrassMap g; g.putBuilding(3, 3);
	CHECK(!g.isFreeForGroundUnit(3, 3, false, kTeam0));
}

void MapQueryTest::testFreeForGroundUnit_UnitFails()
{
	GrassMap g; g.putGroundUnit(3, 3);
	CHECK(!g.isFreeForGroundUnit(3, 3, false, kTeam0));
}

void MapQueryTest::testFreeForGroundUnit_WaterFailsWhenNotSwim()
{
	GrassMap g; g.makeWater(3, 3);
	CHECK(!g.isFreeForGroundUnit(3, 3, false, kTeam0));
}

void MapQueryTest::testFreeForGroundUnit_WaterPassesWhenSwim()
{
	GrassMap g; g.makeWater(3, 3);
	CHECK(g.isFreeForGroundUnit(3, 3, true, kTeam0));
}

void MapQueryTest::testFreeForGroundUnit_ForbiddenFailsWhenMaskMatches()
{
	GrassMap g; g.setForbidden(3, 3, kTeam0);
	CHECK(!g.isFreeForGroundUnit(3, 3, false, kTeam0));
}

void MapQueryTest::testFreeForGroundUnit_ForbiddenPassesWhenMaskDoesNotMatch()
{
	GrassMap g; g.setForbidden(3, 3, kTeam1);
	CHECK(g.isFreeForGroundUnit(3, 3, false, kTeam0));
}

// ---------------- isFreeForGroundUnitNoForbidden ----------------

void MapQueryTest::testFreeForGroundUnitNoForbidden_IgnoresForbidden()
{
	GrassMap g; g.setForbidden(3, 3, kTeam0);
	// Forbidden bit is set for our team — but the NoForbidden variant ignores it.
	CHECK(g.isFreeForGroundUnitNoForbidden(3, 3, false));
}

void MapQueryTest::testFreeForGroundUnitNoForbidden_StillBlocksBuilding()
{
	GrassMap g; g.putBuilding(3, 3);
	CHECK(!g.isFreeForGroundUnitNoForbidden(3, 3, false));
}

// ---------------- isFreeForBuilding ----------------

void MapQueryTest::testFreeForBuilding_GrassPasses()
{
	GrassMap g;
	CHECK(g.isFreeForBuilding(3, 3));
}

void MapQueryTest::testFreeForBuilding_ResourceFails()
{
	GrassMap g; g.putResource(3, 3);
	CHECK(!g.isFreeForBuilding(3, 3));
}

void MapQueryTest::testFreeForBuilding_BuildingFails()
{
	GrassMap g; g.putBuilding(3, 3);
	CHECK(!g.isFreeForBuilding(3, 3));
}

void MapQueryTest::testFreeForBuilding_UnitFails()
{
	GrassMap g; g.putGroundUnit(3, 3);
	CHECK(!g.isFreeForBuilding(3, 3));
}

void MapQueryTest::testFreeForBuilding_WaterFails()
{
	GrassMap g; g.makeWater(3, 3);
	// Buildings can never be placed on non-grass — canSwim is irrelevant here.
	CHECK(!g.isFreeForBuilding(3, 3));
}

void MapQueryTest::testFreeForBuilding_SandFails()
{
	GrassMap g; g.makeSand(3, 3);
	CHECK(!g.isFreeForBuilding(3, 3));
}

void MapQueryTest::testFreeForBuilding_RectAllGrassPasses()
{
	GrassMap g;
	CHECK(g.isFreeForBuilding(2, 2, 3, 3));
}

void MapQueryTest::testFreeForBuilding_RectOneBadTileFails()
{
	GrassMap g; g.putBuilding(3, 3);
	// 3x3 starting at (2,2) covers (3,3) — single bad tile fails the whole rect.
	CHECK(!g.isFreeForBuilding(2, 2, 3, 3));
}

void MapQueryTest::testFreeForBuilding_RectGidTolerantSameGidPasses()
{
	GrassMap g; g.putBuilding(3, 3, /*gbid=*/42);
	// gid-tolerant overload accepts tiles already occupied by gid=42.
	CHECK(g.isFreeForBuilding(2, 2, 3, 3, /*gid=*/42));
}

void MapQueryTest::testFreeForBuilding_RectGidTolerantDifferentGidFails()
{
	GrassMap g; g.putBuilding(3, 3, /*gbid=*/42);
	CHECK(!g.isFreeForBuilding(2, 2, 3, 3, /*gid=*/99));
}

// ---------------- isHardSpaceForGroundUnit ----------------

void MapQueryTest::testHardSpaceForGroundUnit_IgnoresUnit()
{
	GrassMap g; g.putGroundUnit(3, 3);
	// HardSpace is "would be free if no unit were here" — so unit presence is OK.
	CHECK(g.isHardSpaceForGroundUnit(3, 3, false, kTeam0));
	// Sanity: the Free variant rejects the same tile.
	CHECK(!g.isFreeForGroundUnit(3, 3, false, kTeam0));
}

void MapQueryTest::testHardSpaceForGroundUnit_ResourceStillFails()
{
	GrassMap g; g.putResource(3, 3);
	CHECK(!g.isHardSpaceForGroundUnit(3, 3, false, kTeam0));
}

void MapQueryTest::testHardSpaceForGroundUnit_BuildingStillFails()
{
	GrassMap g; g.putBuilding(3, 3);
	CHECK(!g.isHardSpaceForGroundUnit(3, 3, false, kTeam0));
}

void MapQueryTest::testHardSpaceForGroundUnit_WaterFailsWhenNotSwim()
{
	GrassMap g; g.makeWater(3, 3);
	CHECK(!g.isHardSpaceForGroundUnit(3, 3, false, kTeam0));
}

void MapQueryTest::testHardSpaceForGroundUnit_ForbiddenStillFails()
{
	GrassMap g; g.setForbidden(3, 3, kTeam0);
	CHECK(!g.isHardSpaceForGroundUnit(3, 3, false, kTeam0));
}

// ---------------- isHardSpaceForBuilding ----------------

void MapQueryTest::testHardSpaceForBuilding_IgnoresUnit()
{
	GrassMap g; g.putGroundUnit(3, 3);
	CHECK(g.isHardSpaceForBuilding(3, 3));
	CHECK(!g.isFreeForBuilding(3, 3));
}

void MapQueryTest::testHardSpaceForBuilding_ResourceFails()
{
	GrassMap g; g.putResource(3, 3);
	CHECK(!g.isHardSpaceForBuilding(3, 3));
}

void MapQueryTest::testHardSpaceForBuilding_BuildingFails()
{
	GrassMap g; g.putBuilding(3, 3);
	CHECK(!g.isHardSpaceForBuilding(3, 3));
}

void MapQueryTest::testHardSpaceForBuilding_NonGrassFails()
{
	GrassMap g; g.makeSand(3, 3);
	CHECK(!g.isHardSpaceForBuilding(3, 3));
}

void MapQueryTest::testHardSpaceForBuilding_RectAllGrassPasses()
{
	GrassMap g;
	CHECK(g.isHardSpaceForBuilding(2, 2, 3, 3));
}

void MapQueryTest::testHardSpaceForBuilding_RectGidTolerantSameGidPasses()
{
	GrassMap g; g.putBuilding(3, 3, /*gbid=*/42);
	CHECK(g.isHardSpaceForBuilding(2, 2, 3, 3, /*gid=*/42));
}

void MapQueryTest::testHardSpaceForBuilding_RectGidTolerantDifferentGidFails()
{
	GrassMap g; g.putBuilding(3, 3, /*gbid=*/42);
	CHECK(!g.isHardSpaceForBuilding(2, 2, 3, 3, /*gid=*/99));
}

// ---------------- local-team mirror (CS-546) ----------------

void MapQueryTest::testLocalTeam_DefaultsToSentinel()
{
	GrassMap g;
	CHECK_EQ(Map::NO_DISPLAYED_TEAM, g.getDisplayedTeam());
}

void MapQueryTest::testLocalTeam_SetAndGet()
{
	GrassMap g;
	g.setDisplayedTeam(3);
	CHECK_EQ(static_cast<Sint32>(3), g.getDisplayedTeam());
	g.setDisplayedTeam(0);
	CHECK_EQ(static_cast<Sint32>(0), g.getDisplayedTeam());
}

void MapQueryTest::testLocalTeam_SentinelValueIsMinusOne()
{
	// Pinned: sim sites that consult getDisplayedTeam() compare against teamNumber (>=0),
	// so the sentinel must never collide with a real team index. -1 is the convention
	// used elsewhere for "no team" (see Game::syncStep's localTeam parameter).
	CHECK_EQ(static_cast<Sint32>(-1), Map::NO_DISPLAYED_TEAM);
}
