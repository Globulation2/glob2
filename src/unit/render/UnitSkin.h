// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <GAGSys.h>
#include "UnitConsts.h"

namespace GAGCore
{
	class Sprite;
}

struct UnitSkin
{
	GAGCore::Sprite *sprite;
	Uint32 startImage[NB_MOVE];
};

// Per-unit-type skin table, indexed by WORKER/EXPLORER/WARRIOR.
// Sprite pointer is null until initUnitSkins() runs (skipped in headless mode).
extern UnitSkin g_unitSkins[NB_UNIT_TYPE];

// Loads the shared unit sprite and fills g_unitSkins. Call once at startup.
void initUnitSkins();
