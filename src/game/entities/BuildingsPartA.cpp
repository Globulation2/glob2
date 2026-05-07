// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// First half of the static building-type table: swarm, inn, hospital,
// racetrack, swimmingpool (entries 0..25). See buildings.cpp for the
// umbrella file that stitches the two halves together and exposes the
// BuildingsTypes accessor surface, and for an explanation of the
// designated-initializer + defaults transcription.

#include "BuildingType.h"

BuildingType g_buildingsPartA[] = {
	// 0: swarm0c (level 0, under construction)
	{ .type = "swarm",
	  .gameSprite = "data/gfx/swarm0c", .miniSprite = "data/gfx/miniswarm0c",
	  .hueImage = 1,
	  .fillable = 1,
	  .width = 4, .height = 4, .decLeft = -2, .decTop = -2,
	  .maxRessource = { /*wood*/0, /*corn*/35 },
	  .maxUnitWorking = 1,
	  .hpInit = 1, .hpMax = 700, .hpInc = 20,
	  .level = 0, .shortTypeNum = 0, .isBuildingSite = 1 },

	// 1: swarm0 (level 0, completed)
	{ .type = "swarm",
	  .gameSprite = "data/gfx/swarm0b", .miniSprite = "data/gfx/miniswarm0b",
	  .hueImage = 1,
	  .fillable = 1,
	  .width = 4, .height = 4, .decLeft = -2, .decTop = -2,
	  .unitProductionTime = 150, .ressourceForOneUnit = 5,
	  .maxRessource = { /*wood*/0, /*corn*/20 },
	  .maxUnitWorking = 1,
	  .hpInit = 700, .hpMax = 700,
	  .level = 0, .shortTypeNum = 0,
	  .viewingRange = 4, .regenerationSpeed = 3 },

	// 2: inn0c (level 0, under construction)
	{ .type = "inn",
	  .gameSprite = "data/gfx/inn0c", .miniSprite = "data/gfx/miniinn0c",
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/3 },
	  .maxUnitWorking = 1,
	  .hpInit = 1, .hpMax = 200, .hpInc = 67,
	  .level = 0, .shortTypeNum = 1, .isBuildingSite = 1 },

	// 3: inn0 (level 0, completed)
	{ .type = "inn",
	  .gameSprite = "data/gfx/inn0b", .gameSpriteCount = 2, .miniSprite = "data/gfx/miniinn0b",
	  .foodable = 1,
	  .canFeedUnit = 1, .timeToFeedUnit = 24,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/0, /*corn*/10, /*papyrus*/0, /*stone*/0, /*algue*/0,
	                    /*fruit0*/40, /*fruit1*/40, /*fruit2*/40 },
	  .maxUnitInside = 4,
	  .maxUnitWorking = 1,
	  .hpInit = 200, .hpMax = 200,
	  .level = 0, .shortTypeNum = 1 },

	// 4: inn1c (level 1, under construction)
	{ .type = "inn",
	  .gameSprite = "data/gfx/inn1c", .miniSprite = "data/gfx/miniinn1c",
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/8 },
	  .maxUnitWorking = 1,
	  .hpInit = 200, .hpMax = 500, .hpInc = 38,
	  .level = 1, .shortTypeNum = 1, .isBuildingSite = 1 },

	// 5: inn1 (level 1, completed)
	{ .type = "inn",
	  .gameSprite = "data/gfx/inn1b", .gameSpriteCount = 2, .miniSprite = "data/gfx/miniinn1b",
	  .foodable = 1,
	  .canFeedUnit = 1, .timeToFeedUnit = 15,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/0, /*corn*/30, /*papyrus*/0, /*stone*/0, /*algue*/0,
	                    /*fruit0*/80, /*fruit1*/80, /*fruit2*/80 },
	  .maxUnitInside = 7,
	  .maxUnitWorking = 1,
	  .hpInit = 500, .hpMax = 500,
	  .armor = 5,
	  .level = 1, .shortTypeNum = 1 },

	// 6: inn2c (level 2, under construction)
	{ .type = "inn",
	  .gameSprite = "data/gfx/inn2c", .miniSprite = "data/gfx/miniinn2c",
	  .fillable = 1,
	  .width = 3, .height = 3, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/7, /*corn*/0, /*papyrus*/0, /*stone*/5 },
	  .maxUnitWorking = 1,
	  .hpInit = 500, .hpMax = 700, .hpInc = 17,
	  .armor = 5,
	  .level = 2, .shortTypeNum = 1, .isBuildingSite = 1 },

	// 7: inn2 (level 2, completed)
	{ .type = "inn",
	  .gameSprite = "data/gfx/inn2b", .miniSprite = "data/gfx/miniinn2b",
	  .foodable = 1,
	  .canFeedUnit = 1, .timeToFeedUnit = 9,
	  .width = 3, .height = 3, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/0, /*corn*/50, /*papyrus*/0, /*stone*/0, /*algue*/0,
	                    /*fruit0*/200, /*fruit1*/200, /*fruit2*/200 },
	  .maxUnitInside = 17,
	  .maxUnitWorking = 1,
	  .hpInit = 700, .hpMax = 700,
	  .armor = 10,
	  .level = 2, .shortTypeNum = 1 },

	// 8: hospital0c (level 0, under construction)
	{ .type = "hospital",
	  .gameSprite = "data/gfx/hosp0c", .miniSprite = "data/gfx/minihosp0c",
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/3 },
	  .maxUnitWorking = 1,
	  .hpInit = 1, .hpMax = 260, .hpInc = 87,
	  .level = 0, .shortTypeNum = 2, .isBuildingSite = 1 },

	// 9: hospital0 (level 0, completed)
	{ .type = "hospital",
	  .gameSprite = "data/gfx/hosp0b", .gameSpriteCount = 2, .miniSprite = "data/gfx/minihosp0b",
	  .canHealUnit = 1, .timeToHealUnit = 30,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxUnitInside = 2,
	  .hpInit = 260, .hpMax = 260,
	  .armor = 5,
	  .level = 0, .shortTypeNum = 2 },

	// 10: hospital1c (level 1, under construction)
	{ .type = "hospital",
	  .gameSprite = "data/gfx/hosp1c", .miniSprite = "data/gfx/minihosp1c",
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/8 },
	  .maxUnitWorking = 1,
	  .hpInit = 260, .hpMax = 500, .hpInc = 30,
	  .level = 1, .shortTypeNum = 2, .isBuildingSite = 1 },

	// 11: hospital1 (level 1, completed)
	{ .type = "hospital",
	  .gameSprite = "data/gfx/hosp1b", .miniSprite = "data/gfx/minihosp1b",
	  .canHealUnit = 1, .timeToHealUnit = 18,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxUnitInside = 5,
	  .hpInit = 500, .hpMax = 500,
	  .armor = 5,
	  .level = 1, .shortTypeNum = 2 },

	// 12: hospital2c (level 2, under construction)
	{ .type = "hospital",
	  .gameSprite = "data/gfx/hosp2c", .miniSprite = "data/gfx/minihosp2c",
	  .fillable = 1,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxRessource = { /*wood*/3, /*corn*/0, /*papyrus*/0, /*stone*/5 },
	  .maxUnitWorking = 1,
	  .hpInit = 500, .hpMax = 700, .hpInc = 25,
	  .armor = 5,
	  .level = 2, .shortTypeNum = 2, .isBuildingSite = 1 },

	// 13: hospital2 (level 2, completed)
	{ .type = "hospital",
	  .gameSprite = "data/gfx/hosp2b", .miniSprite = "data/gfx/minihosp2b",
	  .canHealUnit = 1, .timeToHealUnit = 6,
	  .width = 2, .height = 2, .decLeft = -1, .decTop = -1,
	  .maxUnitInside = 7,
	  .hpInit = 700, .hpMax = 700,
	  .armor = 10,
	  .level = 2, .shortTypeNum = 2 },

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
};

extern const std::size_t g_buildingsPartACount =
	sizeof(g_buildingsPartA) / sizeof(g_buildingsPartA[0]);
