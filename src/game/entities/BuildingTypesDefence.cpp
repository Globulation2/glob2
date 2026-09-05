// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Defensive structures: defencetower (shoots at hostile units in range) and
// stonewall (blocks movement).
//
// The two are not adjacent in the flat table — the flags sit between them —
// so they are separate arrays; Buildings.cpp splices both in at their
// original positions.

#include "BuildingType.h"

// Flat table entries 38..43.
BuildingType g_buildingTypesDefenceTower[] = {
	// 38: defencetower0c (level 0, under construction)
	{ .type = "defencetower",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 1,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 1,
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/6 },
	  .maxUnitWorking = 1,
	  .hpInit = 1, .hpMax = 480, .hpInc = 80,
	  .armor = 0,
	  .level = 0, .shortTypeNum = 7, .isBuildingSite = 1 },

	// 39: defencetower0 (level 0, completed) — short-range stone tower
	{ .type = "defencetower",
	  .gameSprite = "data/gfx/defencetower0b", .miniSprite = "data/gfx/minidefencetower0b",
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .shootingRange = 5, .shootDamage = 30, .shootSpeed = 5000, .shootRythme = 1700,
	  .maxBullets = 12, .multiplierStoneToBullets = 3,
	  .maxRessource = { /*wood*/0, /*corn*/0, /*papyrus*/0, /*stone*/4 },
	  .maxUnitWorking = 1,
	  .hpInit = 480, .hpMax = 480,
	  .armor = 8,
	  .level = 0, .shortTypeNum = 7,
	  .viewingRange = 6 },

	// 40: defencetower1c (level 1, under construction)
	{ .type = "defencetower",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 1,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 1,
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/10, /*corn*/0, /*papyrus*/0, /*stone*/14 },
	  .maxUnitWorking = 1,
	  .hpInit = 480, .hpMax = 1440, .hpInc = 40,
	  .armor = 8,
	  .level = 1, .shortTypeNum = 7, .isBuildingSite = 1,
	  .viewingRange = 5 },

	// 41: defencetower1 (level 1, completed)
	{ .type = "defencetower",
	  .gameSprite = "data/gfx/defencetower1b", .gameSpriteCount = 3, .miniSprite = "data/gfx/minidefencetower1b",
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .shootingRange = 7, .shootDamage = 40, .shootSpeed = 5700, .shootRythme = 1800,
	  .maxBullets = 16, .multiplierStoneToBullets = 4,
	  .maxRessource = { /*wood*/0, /*corn*/0, /*papyrus*/0, /*stone*/4 },
	  .maxUnitWorking = 1,
	  .hpInit = 1440, .hpMax = 1440,
	  .armor = 12,
	  .level = 1, .shortTypeNum = 7,
	  .viewingRange = 7 },

	// 42: defencetower2c (level 2, under construction)
	{ .type = "defencetower",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 1,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 1,
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/8, /*corn*/0, /*papyrus*/0, /*stone*/14, /*algue*/2 },
	  .maxUnitWorking = 1,
	  .hpInit = 1440, .hpMax = 2000, .hpInc = 24,
	  .armor = 12,
	  .level = 2, .shortTypeNum = 7, .isBuildingSite = 1,
	  .viewingRange = 6 },

	// 43: defencetower2 (level 2, completed) — long-range stone tower
	{ .type = "defencetower",
	  .gameSprite = "data/gfx/defencetower2b", .miniSprite = "data/gfx/minidefencetower2b",
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .shootingRange = 9, .shootDamage = 50, .shootSpeed = 7000, .shootRythme = 1900,
	  .maxBullets = 20, .multiplierStoneToBullets = 7,
	  .maxRessource = { /*wood*/0, /*corn*/0, /*papyrus*/0, /*stone*/4 },
	  .maxUnitWorking = 1,
	  .hpInit = 2000, .hpMax = 2000,
	  .armor = 15,
	  .level = 2, .shortTypeNum = 7,
	  .viewingRange = 8 },
};

extern const std::size_t g_buildingTypesDefenceTowerCount =
	sizeof(g_buildingTypesDefenceTower) / sizeof(g_buildingTypesDefenceTower[0]);

// Flat table entries 47..48.
BuildingType g_buildingTypesStoneWall[] = {
	// 47: stonewall0c
	// In the data file the parser sees "hueImage 1;" — the trailing semicolon
	// is silently dropped by std::istringstream when reading int, so this is
	// equivalent to "hueImage 1". The duplicate "miniSpriteImage" line
	// overwrites the earlier value (final = -1).
	{ .type = "stonewall",
	  .gameSprite = "data/gfx/wallc",
	  .miniSpriteImage = -1,
	  .hueImage = 1,
	  .fillable = 1,
	  .width = 1, .height = 1,
	  .maxRessource = { /*wood*/0, /*corn*/0, /*papyrus*/0, /*stone*/1 },
	  .maxUnitWorking = 1,
	  .hpInit = 1, .hpMax = 180, .hpInc = 180,
	  .level = 0, .shortTypeNum = 11, .isBuildingSite = 1 },

	// 48: stonewall0
	// Same trailing-semicolon and duplicate-key behavior as stonewall0c.
	{ .type = "stonewall",
	  .gameSprite = "data/gfx/wall",
	  .miniSpriteImage = -1,
	  .hueImage = 1,
	  .crossConnectMultiImage = 1,
	  .width = 1, .height = 1,
	  .hpInit = 180, .hpMax = 180,
	  .armor = 10,
	  .level = 0, .shortTypeNum = 11 },
};

extern const std::size_t g_buildingTypesStoneWallCount =
	sizeof(g_buildingTypesStoneWall) / sizeof(g_buildingTypesStoneWall[0]);
