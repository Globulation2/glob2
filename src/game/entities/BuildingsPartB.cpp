// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Second half of the static building-type table: barracks, school,
// defencetower, flags (exploration/war/clearing), stonewall, market
// (entries 26..50). See buildings.cpp for the umbrella file that
// stitches the two halves together.

#include "BuildingType.h"

BuildingType g_buildingsPartB[] = {
	// 26: barracks0c (level 0, under construction)
	{ .type = "barracks",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 3,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 3,
	  .fillable = 1,
	  .width = 4, .height = 4, .decLeft = -2, .decTop = -2,
	  .maxRessource = { /*wood*/7 },
	  .maxUnitWorking = 1,
	  .hpInit = 1, .hpMax = 440, .hpInc = 63,
	  .armor = 0,
	  .level = 0, .shortTypeNum = 5, .isBuildingSite = 1 },

	// 27: barracks0 (level 0, completed) — upgrade[8]=AttackSpeed, upgrade[9]=AttackStrength
	{ .type = "barracks",
	  .gameSprite = "data/gfx/barracks0b", .miniSprite = "data/gfx/minibarracks0b",
	  .upgrade =     { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 },
	  .upgradeTime = { 0, 0, 0, 0, 0, 0, 0, 0, 21, 21 },
	  .upgradeInParallel = 1,
	  .width = 4, .height = 4, .decLeft = -2, .decTop = -2,
	  .maxUnitInside = 2,
	  .hpInit = 440, .hpMax = 440,
	  .armor = 5,
	  .level = 0, .shortTypeNum = 5 },

	// 28: barracks1c (level 1, under construction)
	{ .type = "barracks",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 3,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 3,
	  .fillable = 1,
	  .width = 4, .height = 4, .decLeft = -2, .decTop = -2,
	  .maxRessource = { /*wood*/3, /*corn*/0, /*papyrus*/0, /*stone*/10 },
	  .maxUnitWorking = 1,
	  .hpInit = 440, .hpMax = 800, .hpInc = 28,
	  .armor = 5,
	  .level = 1, .shortTypeNum = 5, .isBuildingSite = 1 },

	// 29: barracks1 (level 1, completed)
	{ .type = "barracks",
	  .gameSprite = "data/gfx/barracks1b", .miniSprite = "data/gfx/minibarracks1b",
	  .upgrade =     { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 },
	  .upgradeTime = { 0, 0, 0, 0, 0, 0, 0, 0, 30, 30 },
	  .upgradeInParallel = 1,
	  .width = 4, .height = 4, .decLeft = -2, .decTop = -2,
	  .maxUnitInside = 4,
	  .hpInit = 800, .hpMax = 800,
	  .armor = 10,
	  .level = 1, .shortTypeNum = 5 },

	// 30: barracks2c (level 2, under construction)
	{ .type = "barracks",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 3,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 3,
	  .fillable = 1,
	  .width = 4, .height = 4, .decLeft = -2, .decTop = -2,
	  .maxRessource = { /*wood*/10, /*corn*/0, /*papyrus*/0, /*stone*/10 },
	  .maxUnitWorking = 1,
	  .hpInit = 800, .hpMax = 1300, .hpInc = 25,
	  .armor = 10,
	  .level = 2, .shortTypeNum = 5, .isBuildingSite = 1 },

	// 31: barracks2 (level 2, completed)
	{ .type = "barracks",
	  .gameSprite = "data/gfx/barracks2b", .miniSprite = "data/gfx/minibarracks2b",
	  .upgrade =     { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 },
	  .upgradeTime = { 0, 0, 0, 0, 0, 0, 0, 0, 42, 42 },
	  .upgradeInParallel = 1,
	  .width = 4, .height = 4, .decLeft = -2, .decTop = -2,
	  .maxUnitInside = 5,
	  .hpInit = 1300, .hpMax = 1300,
	  .armor = 12,
	  .level = 2, .shortTypeNum = 5 },

	// 32: school0c (level 0, under construction)
	{ .type = "school",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 1,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 1,
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/7, /*corn*/0, /*papyrus*/0, /*stone*/0, /*algue*/2 },
	  .maxUnitWorking = 1,
	  .hpInit = 1, .hpMax = 360, .hpInc = 40,
	  .armor = 0,
	  .level = 0, .shortTypeNum = 6, .isBuildingSite = 1 },

	// 33: school0 (level 0, completed) — upgrade[6]=Build, upgrade[7]=Harvest
	{ .type = "school",
	  .gameSprite = "data/gfx/school0b", .gameSpriteCount = 2, .miniSprite = "data/gfx/minischool0b",
	  .upgrade =     { 0, 0, 0, 0, 0, 0, 1, 1 },
	  .upgradeTime = { 0, 0, 0, 0, 0, 0, 21, 21 },
	  .upgradeInParallel = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxUnitInside = 4,
	  .hpInit = 360, .hpMax = 360,
	  .armor = 3,
	  .level = 0, .shortTypeNum = 6 },

	// 34: school1c (level 1, under construction)
	{ .type = "school",
	  .gameSprite = "data/gfx/school1c", .miniSprite = "data/gfx/minischool1c",
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/5, /*corn*/0, /*papyrus*/0, /*stone*/5, /*algue*/12 },
	  .maxUnitWorking = 1,
	  .hpInit = 360, .hpMax = 520, .hpInc = 8,
	  .armor = 3,
	  .level = 1, .shortTypeNum = 6, .isBuildingSite = 1 },

	// 35: school1 (level 1, completed)
	{ .type = "school",
	  .gameSprite = "data/gfx/school1b", .gameSpriteCount = 3, .miniSprite = "data/gfx/minischool1b",
	  .upgrade =     { 0, 0, 0, 0, 0, 0, 1, 1 },
	  .upgradeTime = { 0, 0, 0, 0, 0, 0, 33, 33 },
	  .upgradeInParallel = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxUnitInside = 7,
	  .hpInit = 520, .hpMax = 520,
	  .armor = 8,
	  .level = 1, .shortTypeNum = 6 },

	// 36: school2c (level 2, under construction)
	{ .type = "school",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 1,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 1,
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/7, /*corn*/4, /*papyrus*/0, /*stone*/12, /*algue*/10 },
	  .maxUnitWorking = 1,
	  .hpInit = 520, .hpMax = 700, .hpInc = 6,
	  .armor = 8,
	  .level = 2, .shortTypeNum = 6, .isBuildingSite = 1 },

	// 37: school2 (level 2, completed) — adds upgrade[11]=MagicAttackGround
	{ .type = "school",
	  .gameSprite = "data/gfx/school2b", .miniSprite = "data/gfx/minischool2b",
	  .upgrade =     { 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1 },
	  .upgradeTime = { 0, 0, 0, 0, 0, 0, 42, 42, 0, 0, 0, 42 },
	  .upgradeInParallel = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxUnitInside = 9,
	  .hpInit = 700, .hpMax = 700,
	  .armor = 12,
	  .level = 2, .shortTypeNum = 6,
	  .prestige = 50 },

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

	// 44: explorationflag0
	// Virtual flag — gameSprite override only (no miniSprite override). The data
	// file says "miniSpriteImage -1", which leaves miniSprite at the default
	// "ERROR_NO_MINI_SPRITE_DEFINED"; the -1 image suppresses miniSpritePtr load.
	{ .type = "explorationflag",
	  .gameSprite = "data/gfx/explorationflag",
	  .miniSpriteImage = -1,
	  .hueImage = 1,
	  .zonable = { /*worker*/0, /*explorer*/1 },
	  .width = 1, .height = 1,
	  .isVirtual = 1, .isCloacked = 1,
	  .maxUnitWorking = 1,
	  .shortTypeNum = 8,
	  .defaultUnitStayRange = 10, .maxUnitStayRange = 20 },

	// 45: warflag0
	{ .type = "warflag",
	  .gameSprite = "data/gfx/warflag",
	  .miniSpriteImage = -1,
	  .hueImage = 1,
	  .zonable = { /*worker*/0, /*explorer*/0, /*warrior*/1 },
	  .width = 1, .height = 1,
	  .isVirtual = 1, .isCloacked = 1,
	  .maxUnitWorking = 1,
	  .shortTypeNum = 9,
	  .defaultUnitStayRange = 4, .maxUnitStayRange = 8 },

	// 46: clearingflag0
	{ .type = "clearingflag",
	  .gameSprite = "data/gfx/clearingflag",
	  .miniSpriteImage = -1,
	  .hueImage = 1,
	  .zonable = { /*worker*/1 },
	  .width = 1, .height = 1,
	  .isVirtual = 1, .isCloacked = 1,
	  .maxUnitWorking = 1,
	  .shortTypeNum = 10,
	  .defaultUnitStayRange = 3, .maxUnitStayRange = 14 },

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

	// 49: market0c (level 0, under construction)
	{ .type = "market",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 2,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 2,
	  .fillable = 1,
	  .width = 3, .height = 3, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/4, /*corn*/0, /*papyrus*/0, /*stone*/4 },
	  .maxUnitWorking = 1,
	  .hpInit = 1, .hpMax = 400, .hpInc = 50,
	  .level = 0, .shortTypeNum = 12, .isBuildingSite = 1 },

	// 50: market0 (level 0, completed) — exchanges fruit between teams
	{ .type = "market",
	  .gameSprite = "data/gfx/market0b", .miniSprite = "data/gfx/minimarket0b",
	  .fillable = 1,
	  .canExchange = 1, .useTeamRessources = 1,
	  .width = 3, .height = 3, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/0, /*corn*/0, /*papyrus*/0, /*stone*/0, /*algue*/0,
	                    /*fruit0*/200, /*fruit1*/200, /*fruit2*/200 },
	  .maxUnitWorking = 1,
	  .hpInit = 400, .hpMax = 400,
	  .armor = 6,
	  .level = 0, .shortTypeNum = 12 },
};

extern const std::size_t g_buildingsPartBCount =
	sizeof(g_buildingsPartB) / sizeof(g_buildingsPartB[0]);
