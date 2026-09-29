// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "Building.h"
#include "BuildingType.h"
#include "Map.h"
#include "Player.h"
#include "Unit.h"
#include <algorithm>

namespace AIMaxima
{
namespace WorldHelpers
{
	inline bool building_currently_visible(Player* player, const Building* building)
	{
		if(!player || !player->map || !building)
			return false;
		for(int dy=0; dy<building->type->height; ++dy)
			for(int dx=0; dx<building->type->width; ++dx)
				if(player->map->isFOWDiscovered(building->posX+dx,
					building->posY+dy, player->team->me))
					return true;
		return false;
	}

	inline int warrior_power(const Unit* warrior)
	{
		return std::max(1, warrior->getRealAttackStrength()
			*warrior->performance[ATTACK_SPEED]*warrior->hp
			/std::max(1, warrior->performance[HP]));
	}

	inline void add_preemptive_hash(Uint32& signature, Uint32 value)
	{
		signature^=value;
		signature*=16777619u;
	}

}
}
