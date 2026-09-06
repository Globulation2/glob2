// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "MapQueryTest.h"

#include "Map.h"
#include "TerrainType.h"

CPPUNIT_TEST_SUITE_REGISTRATION( MapQueryTest );

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
	// The destructor resets size fields before Map::~Map() runs, because Map's
	// clear() else-branch (the path taken when arraysBuilt==false) asserts
	// w==0 / h==0 / etc. — that's a real assert in production, but it expects
	// to see a constructed-then-clear()'d Map, not a manually-poked one.
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
			cases.assign(size, Case());   // Case() defaults: terrain=0 (grass), no building, no unit
		}
		~GrassMap()
		{
			w = h = 0;
			wMask = hMask = 0;
			wDec = hDec = 0;
			size = 0;
			// Map::~Map() will call clear() which asserts these are 0.
		}

		void putBuilding(int x, int y, Uint16 gbid = 0)
		{
			cases[coordToIndex(x, y)].building = gbid;
		}
		void putGroundUnit(int x, int y, Uint16 guid = 0)
		{
			cases[coordToIndex(x, y)].groundUnit = guid;
		}
		void putRessource(int x, int y, int type = 0)
		{
			Ressource &r = cases[coordToIndex(x, y)].ressource;
			r.type = type;
			r.amount = 1;
			r.variety = 0;
			r.animation = 0;
		}
		void setForbidden(int x, int y, Uint32 mask)
		{
			cases[coordToIndex(x, y)].forbidden = mask;
		}
		// Terrain encoding (see Map.h:336-361):
		//   grass : terrain <  16
		//   sand  : 128..143
		//   water : 256..271
		void makeWater(int x, int y)
		{
			cases[coordToIndex(x, y)].terrain = 256;
		}
		void makeSand(int x, int y)
		{
			cases[coordToIndex(x, y)].terrain = 128;
		}
	};

	constexpr Uint32 kTeam0 = 0x00000001u;
	constexpr Uint32 kTeam1 = 0x00000002u;
}

// ---------------- isFreeForGroundUnit ----------------

void MapQueryTest::testFreeForGroundUnit_CleanGrassPasses()
{
	GrassMap g;
	CPPUNIT_ASSERT( g.isFreeForGroundUnit(3, 3, false, kTeam0) );
}

void MapQueryTest::testFreeForGroundUnit_RessourceFails()
{
	GrassMap g; g.putRessource(3, 3);
	CPPUNIT_ASSERT( !g.isFreeForGroundUnit(3, 3, false, kTeam0) );
}

void MapQueryTest::testFreeForGroundUnit_BuildingFails()
{
	GrassMap g; g.putBuilding(3, 3);
	CPPUNIT_ASSERT( !g.isFreeForGroundUnit(3, 3, false, kTeam0) );
}

void MapQueryTest::testFreeForGroundUnit_UnitFails()
{
	GrassMap g; g.putGroundUnit(3, 3);
	CPPUNIT_ASSERT( !g.isFreeForGroundUnit(3, 3, false, kTeam0) );
}

void MapQueryTest::testFreeForGroundUnit_WaterFailsWhenNotSwim()
{
	GrassMap g; g.makeWater(3, 3);
	CPPUNIT_ASSERT( !g.isFreeForGroundUnit(3, 3, false, kTeam0) );
}

void MapQueryTest::testFreeForGroundUnit_WaterPassesWhenSwim()
{
	GrassMap g; g.makeWater(3, 3);
	CPPUNIT_ASSERT( g.isFreeForGroundUnit(3, 3, true, kTeam0) );
}

void MapQueryTest::testFreeForGroundUnit_ForbiddenFailsWhenMaskMatches()
{
	GrassMap g; g.setForbidden(3, 3, kTeam0);
	CPPUNIT_ASSERT( !g.isFreeForGroundUnit(3, 3, false, kTeam0) );
}

void MapQueryTest::testFreeForGroundUnit_ForbiddenPassesWhenMaskDoesNotMatch()
{
	GrassMap g; g.setForbidden(3, 3, kTeam1);
	CPPUNIT_ASSERT( g.isFreeForGroundUnit(3, 3, false, kTeam0) );
}

// ---------------- isFreeForGroundUnitNoForbidden ----------------

void MapQueryTest::testFreeForGroundUnitNoForbidden_IgnoresForbidden()
{
	GrassMap g; g.setForbidden(3, 3, kTeam0);
	// Forbidden bit is set for our team — but the NoForbidden variant ignores it.
	CPPUNIT_ASSERT( g.isFreeForGroundUnitNoForbidden(3, 3, false) );
}

void MapQueryTest::testFreeForGroundUnitNoForbidden_StillBlocksBuilding()
{
	GrassMap g; g.putBuilding(3, 3);
	CPPUNIT_ASSERT( !g.isFreeForGroundUnitNoForbidden(3, 3, false) );
}

// ---------------- isFreeForBuilding ----------------

void MapQueryTest::testFreeForBuilding_GrassPasses()
{
	GrassMap g;
	CPPUNIT_ASSERT( g.isFreeForBuilding(3, 3) );
}

void MapQueryTest::testFreeForBuilding_RessourceFails()
{
	GrassMap g; g.putRessource(3, 3);
	CPPUNIT_ASSERT( !g.isFreeForBuilding(3, 3) );
}

void MapQueryTest::testFreeForBuilding_BuildingFails()
{
	GrassMap g; g.putBuilding(3, 3);
	CPPUNIT_ASSERT( !g.isFreeForBuilding(3, 3) );
}

void MapQueryTest::testFreeForBuilding_UnitFails()
{
	GrassMap g; g.putGroundUnit(3, 3);
	CPPUNIT_ASSERT( !g.isFreeForBuilding(3, 3) );
}

void MapQueryTest::testFreeForBuilding_WaterFails()
{
	GrassMap g; g.makeWater(3, 3);
	// Buildings can never be placed on non-grass — canSwim is irrelevant here.
	CPPUNIT_ASSERT( !g.isFreeForBuilding(3, 3) );
}

void MapQueryTest::testFreeForBuilding_SandFails()
{
	GrassMap g; g.makeSand(3, 3);
	CPPUNIT_ASSERT( !g.isFreeForBuilding(3, 3) );
}

void MapQueryTest::testFreeForBuilding_RectAllGrassPasses()
{
	GrassMap g;
	CPPUNIT_ASSERT( g.isFreeForBuilding(2, 2, 3, 3) );
}

void MapQueryTest::testFreeForBuilding_RectOneBadTileFails()
{
	GrassMap g; g.putBuilding(3, 3);
	// 3x3 starting at (2,2) covers (3,3) — single bad tile fails the whole rect.
	CPPUNIT_ASSERT( !g.isFreeForBuilding(2, 2, 3, 3) );
}

void MapQueryTest::testFreeForBuilding_RectGidTolerantSameGidPasses()
{
	GrassMap g; g.putBuilding(3, 3, /*gbid=*/42);
	// gid-tolerant overload accepts tiles already occupied by gid=42.
	CPPUNIT_ASSERT( g.isFreeForBuilding(2, 2, 3, 3, /*gid=*/42) );
}

void MapQueryTest::testFreeForBuilding_RectGidTolerantDifferentGidFails()
{
	GrassMap g; g.putBuilding(3, 3, /*gbid=*/42);
	CPPUNIT_ASSERT( !g.isFreeForBuilding(2, 2, 3, 3, /*gid=*/99) );
}

// ---------------- isHardSpaceForGroundUnit ----------------

void MapQueryTest::testHardSpaceForGroundUnit_IgnoresUnit()
{
	GrassMap g; g.putGroundUnit(3, 3);
	// HardSpace is "would be free if no unit were here" — so unit presence is OK.
	CPPUNIT_ASSERT( g.isHardSpaceForGroundUnit(3, 3, false, kTeam0) );
	// Sanity: the Free variant rejects the same tile.
	CPPUNIT_ASSERT( !g.isFreeForGroundUnit(3, 3, false, kTeam0) );
}

void MapQueryTest::testHardSpaceForGroundUnit_RessourceStillFails()
{
	GrassMap g; g.putRessource(3, 3);
	CPPUNIT_ASSERT( !g.isHardSpaceForGroundUnit(3, 3, false, kTeam0) );
}

void MapQueryTest::testHardSpaceForGroundUnit_BuildingStillFails()
{
	GrassMap g; g.putBuilding(3, 3);
	CPPUNIT_ASSERT( !g.isHardSpaceForGroundUnit(3, 3, false, kTeam0) );
}

void MapQueryTest::testHardSpaceForGroundUnit_WaterFailsWhenNotSwim()
{
	GrassMap g; g.makeWater(3, 3);
	CPPUNIT_ASSERT( !g.isHardSpaceForGroundUnit(3, 3, false, kTeam0) );
}

void MapQueryTest::testHardSpaceForGroundUnit_ForbiddenStillFails()
{
	GrassMap g; g.setForbidden(3, 3, kTeam0);
	CPPUNIT_ASSERT( !g.isHardSpaceForGroundUnit(3, 3, false, kTeam0) );
}

// ---------------- isHardSpaceForBuilding ----------------

void MapQueryTest::testHardSpaceForBuilding_IgnoresUnit()
{
	GrassMap g; g.putGroundUnit(3, 3);
	CPPUNIT_ASSERT( g.isHardSpaceForBuilding(3, 3) );
	CPPUNIT_ASSERT( !g.isFreeForBuilding(3, 3) );
}

void MapQueryTest::testHardSpaceForBuilding_RessourceFails()
{
	GrassMap g; g.putRessource(3, 3);
	CPPUNIT_ASSERT( !g.isHardSpaceForBuilding(3, 3) );
}

void MapQueryTest::testHardSpaceForBuilding_BuildingFails()
{
	GrassMap g; g.putBuilding(3, 3);
	CPPUNIT_ASSERT( !g.isHardSpaceForBuilding(3, 3) );
}

void MapQueryTest::testHardSpaceForBuilding_NonGrassFails()
{
	GrassMap g; g.makeSand(3, 3);
	CPPUNIT_ASSERT( !g.isHardSpaceForBuilding(3, 3) );
}

void MapQueryTest::testHardSpaceForBuilding_RectAllGrassPasses()
{
	GrassMap g;
	CPPUNIT_ASSERT( g.isHardSpaceForBuilding(2, 2, 3, 3) );
}

void MapQueryTest::testHardSpaceForBuilding_RectGidTolerantSameGidPasses()
{
	GrassMap g; g.putBuilding(3, 3, /*gbid=*/42);
	CPPUNIT_ASSERT( g.isHardSpaceForBuilding(2, 2, 3, 3, /*gid=*/42) );
}

void MapQueryTest::testHardSpaceForBuilding_RectGidTolerantDifferentGidFails()
{
	GrassMap g; g.putBuilding(3, 3, /*gbid=*/42);
	CPPUNIT_ASSERT( !g.isHardSpaceForBuilding(2, 2, 3, 3, /*gid=*/99) );
}

// ---------------- local-team mirror (CS-546) ----------------

void MapQueryTest::testLocalTeam_DefaultsToSentinel()
{
	GrassMap g;
	CPPUNIT_ASSERT_EQUAL( Map::NO_DISPLAYED_TEAM, g.getDisplayedTeam() );
}

void MapQueryTest::testLocalTeam_SetAndGet()
{
	GrassMap g;
	g.setDisplayedTeam(3);
	CPPUNIT_ASSERT_EQUAL( static_cast<Sint32>(3), g.getDisplayedTeam() );
	g.setDisplayedTeam(0);
	CPPUNIT_ASSERT_EQUAL( static_cast<Sint32>(0), g.getDisplayedTeam() );
}

void MapQueryTest::testLocalTeam_SentinelValueIsMinusOne()
{
	// Pinned: sim sites that consult getDisplayedTeam() compare against teamNumber (>=0),
	// so the sentinel must never collide with a real team index. -1 is the convention
	// used elsewhere for "no team" (see Game::syncStep's localTeam parameter).
	CPPUNIT_ASSERT_EQUAL( static_cast<Sint32>(-1), Map::NO_DISPLAYED_TEAM );
}
