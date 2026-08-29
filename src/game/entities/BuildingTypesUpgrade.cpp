// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Upgrade buildings: the four structures that train units and raise their
// ability levels — racetrack (Walk), swimmingpool (Swim), barracks (Attack
// Speed / Strength), school (Build / Harvest, plus Magic Attack Ground at
// level 2). See Buildings.cpp for the ID-order registry.

#include "BuildingType.h"

// Flat table entries 14..37.
BuildingType g_buildingTypesUpgrade[] = {
	// 14: racetrack0c (level 0, under construction)
	{ .type = "racetrack",
	  .gameSprite = "data/gfx/racetrack0c", .miniSprite = "data/gfx/miniracetrack0c",
	  .fillable = 1,
	  .width = 4, .height = 4, .decLeft = -2, .decTop = -2,
	  .maxRessource = { /*wood*/6, /*corn*/0, /*papyrus*/0, /*stone*/1 },
	  .maxUnitWorking = 1,
	  .hpInit = 1, .hpMax = 675, .hpInc = 97,
	  .armor = 0,
	  .level = 0, .shortTypeNum = 3, .isBuildingSite = 1 },

	// 15: racetrack0 (level 0, completed) — upgrade[3]=Walk, upgradeTime[3]=21
	{ .type = "racetrack",
	  .gameSprite = "data/gfx/racetrack0b", .gameSpriteCount = 3, .miniSprite = "data/gfx/miniracetrack0b",
	  .upgrade =     { 0, 0, 0, 1 },
	  .upgradeTime = { 0, 0, 0, 21 },
	  .width = 4, .height = 4, .decLeft = -2, .decTop = -2,
	  .maxUnitInside = 2,
	  .hpInit = 675, .hpMax = 675,
	  .armor = 5,
	  .level = 0, .shortTypeNum = 3 },

	// 16: racetrack1c (level 1, under construction) — generic buildingsite sprite
	{ .type = "racetrack",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 5,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 5,
	  .fillable = 1,
	  .width = 6, .height = 6, .decLeft = -3, .decTop = -3,
	  .maxRessource = { /*wood*/10, /*corn*/0, /*papyrus*/0, /*stone*/5 },
	  .maxUnitWorking = 1,
	  .hpInit = 675, .hpMax = 1000, .hpInc = 22,
	  .armor = 5,
	  .level = 1, .shortTypeNum = 3, .isBuildingSite = 1 },

	// 17: racetrack1 (level 1, completed)
	{ .type = "racetrack",
	  .gameSprite = "data/gfx/racetrack1b", .gameSpriteCount = 3, .miniSprite = "data/gfx/miniracetrack1b",
	  .upgrade =     { 0, 0, 0, 1 },
	  .upgradeTime = { 0, 0, 0, 21 },
	  .width = 6, .height = 6, .decLeft = -3, .decTop = -3,
	  .maxUnitInside = 4,
	  .hpInit = 1000, .hpMax = 1000,
	  .armor = 10,
	  .level = 1, .shortTypeNum = 3 },

	// 18: racetrack2c (level 2, under construction)
	{ .type = "racetrack",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 5,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 5,
	  .fillable = 1,
	  .width = 6, .height = 6, .decLeft = -3, .decTop = -3,
	  .maxRessource = { /*wood*/15, /*corn*/0, /*papyrus*/0, /*stone*/5 },
	  .maxUnitWorking = 1,
	  .hpInit = 1000, .hpMax = 1500, .hpInc = 25,
	  .armor = 10,
	  .level = 2, .shortTypeNum = 3, .isBuildingSite = 1 },

	// 19: racetrack2 (level 2, completed)
	{ .type = "racetrack",
	  .gameSprite = "data/gfx/racetrack2b", .miniSprite = "data/gfx/miniracetrack2b",
	  .upgrade =     { 0, 0, 0, 1 },
	  .upgradeTime = { 0, 0, 0, 24 },
	  .width = 6, .height = 6, .decLeft = -3, .decTop = -3,
	  .maxUnitInside = 6,
	  .hpInit = 1500, .hpMax = 1500,
	  .armor = 12,
	  .level = 2, .shortTypeNum = 3 },

	// 20: swimmingpool0c (level 0, under construction)
	{ .type = "swimmingpool",
	  .gameSprite = "data/gfx/pool0c", .miniSprite = "data/gfx/minipool0c",
	  .fillable = 1,
	  .width = 4, .height = 4, .decLeft = -2, .decTop = -2,
	  .maxRessource = { /*wood*/8 },
	  .maxUnitWorking = 1,
	  .hpInit = 1, .hpMax = 675, .hpInc = 97,
	  .armor = 0,
	  .level = 0, .shortTypeNum = 4, .isBuildingSite = 1 },

	// 21: swimmingpool0 (level 0, completed) — upgrade[4]=Swim
	{ .type = "swimmingpool",
	  .gameSprite = "data/gfx/pool0b", .gameSpriteCount = 2, .miniSprite = "data/gfx/minipool0b",
	  .upgrade =     { 0, 0, 0, 0, 1 },
	  .upgradeTime = { 0, 0, 0, 0, 21 },
	  .width = 4, .height = 4, .decLeft = -2, .decTop = -2,
	  .maxUnitInside = 2,
	  .hpInit = 675, .hpMax = 675,
	  .armor = 5,
	  .level = 0, .shortTypeNum = 4 },

	// 22: swimmingpool1c (level 1, under construction)
	{ .type = "swimmingpool",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 5,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 5,
	  .fillable = 1,
	  .width = 6, .height = 6, .decLeft = -3, .decTop = -3,
	  .maxRessource = { /*wood*/12, /*corn*/6 },
	  .maxUnitWorking = 1,
	  .hpInit = 675, .hpMax = 1000, .hpInc = 19,
	  .armor = 5,
	  .level = 1, .shortTypeNum = 4, .isBuildingSite = 1 },

	// 23: swimmingpool1 (level 1, completed)
	{ .type = "swimmingpool",
	  .gameSprite = "data/gfx/pool1b", .miniSprite = "data/gfx/minipool1b",
	  .upgrade =     { 0, 0, 0, 0, 1 },
	  .upgradeTime = { 0, 0, 0, 0, 21 },
	  .width = 6, .height = 6, .decLeft = -3, .decTop = -3,
	  .maxUnitInside = 4,
	  .hpInit = 1000, .hpMax = 1000,
	  .armor = 8,
	  .level = 1, .shortTypeNum = 4 },

	// 24: swimmingpool2c (level 2, under construction)
	{ .type = "swimmingpool",
	  .gameSprite = "data/gfx/buildingsite", .gameSpriteImage = 5,
	  .miniSprite = "data/gfx/minibuildingsite", .miniSpriteImage = 5,
	  .fillable = 1,
	  .width = 6, .height = 6, .decLeft = -3, .decTop = -3,
	  .maxRessource = { /*wood*/8, /*corn*/4, /*papyrus*/0, /*stone*/6, /*algue*/8 },
	  .maxUnitWorking = 1,
	  .hpInit = 1000, .hpMax = 1500, .hpInc = 20,
	  .armor = 8,
	  .level = 2, .shortTypeNum = 4, .isBuildingSite = 1 },

	// 25: swimmingpool2 (level 2, completed)
	{ .type = "swimmingpool",
	  .gameSprite = "data/gfx/pool2b", .miniSprite = "data/gfx/minipool2b",
	  .upgrade =     { 0, 0, 0, 0, 1 },
	  .upgradeTime = { 0, 0, 0, 0, 24 },
	  .width = 6, .height = 6, .decLeft = -3, .decTop = -3,
	  .maxUnitInside = 6,
	  .hpInit = 1500, .hpMax = 1500,
	  .armor = 12,
	  .level = 2, .shortTypeNum = 4 },

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
};

extern const std::size_t g_buildingTypesUpgradeCount =
	sizeof(g_buildingTypesUpgrade) / sizeof(g_buildingTypesUpgrade[0]);
