// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "UnitSkin.h"
#include <Toolkit.h>

UnitSkin g_unitSkins[NB_UNIT_TYPE];

namespace
{
	// Default editor-palette frame offsets for each presentation slot, indexed by Abilities enum
	// values STOP_WALK..ATTACK_SPEED (which are 0..NB_MOVE-1 in UnitConsts.h).
	// Replaces the legacy data/unitsSkins.txt. Live units use their catalog's
	// animation offsets; the fixed built-in palette retains these stock icons.
	constexpr Uint32 SKIN_OFFSETS[NB_UNIT_TYPE][NB_MOVE] = {
		// STOP_WALK, STOP_SWIM, STOP_FLY,  WALK, SWIM, FLY,  BUILD, HARVEST, ATTACK_SPEED
		/* WORKER   */ {  64, 128, 0,   64, 128, 0,  192, 192,   0 },
		/* EXPLORER */ {   0,   0, 0,    0,   0, 0,    0,   0,   0 },
		/* WARRIOR  */ { 256, 320, 0,  256, 320, 0,    0,   0, 384 },
	};
}

void initUnitSkins()
{
	GAGCore::Sprite *sprite = GAGCore::Toolkit::getSprite("data/gfx/unit");
	for (int type = 0; type < NB_UNIT_TYPE; ++type)
	{
		g_unitSkins[type].sprite = sprite;
		for (int move = 0; move < NB_MOVE; ++move)
			g_unitSkins[type].startImage[move] = SKIN_OFFSETS[type][move];
	}
}
