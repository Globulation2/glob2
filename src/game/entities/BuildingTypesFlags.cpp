// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

// Flags: virtual buildings that mark an area for unit assignment rather than
// occupying map squares — exploration, war and clearing. See Buildings.cpp
// for the ID-order registry.

#include "BuildingType.h"

// Flat table entries 44..46.
BuildingType g_buildingTypesFlags[] = {
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
};

extern const std::size_t g_buildingTypesFlagsCount =
	sizeof(g_buildingTypesFlags) / sizeof(g_buildingTypesFlags[0]);
