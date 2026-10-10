// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "Building.h"
#include "BuildingType.h"
#include "ai/observation/ObservationQueries.h"
#include "Unit.h"
#include <algorithm>

namespace AIMaxima
{
namespace WorldHelpers
{
	inline bool building_currently_visible(const AIEngine::AIWorldView& world, Uint32 teamMask, const AIEngine::BuildingView* building)
	{
		if(!building) return false;
		const auto& type=AIEngine::ObservationQueries::buildingType(world,*building);
		for(int dy=0; dy<type.height; ++dy)
			for(int dx=0; dx<type.width; ++dx)
				if(AIEngine::ObservationQueries::visible(world,building->posX+dx,
					building->posY+dy,teamMask)) return true;
		return false;
	}

	inline int warrior_power(const AIEngine::AIWorldView& world,const AIEngine::UnitView* warrior)
	{
		if (warrior->hp<=0) return 1;
		const Sint64 rate=Sint64(AIEngine::ObservationQueries::realAttackStrength(world,*warrior))
			*warrior->performance[ATTACK_SPEED];
		if (rate<=0) return 1;
		const Sint64 maximumHealth=std::max(1,warrior->performance[HP]);
		// Avoid overflowing even the wide numerator when importing old cached
		// performances. Ordinary units take only the original division below.
		if (rate>INT_MAX && rate>Sint64(INT_MAX)*maximumHealth/warrior->hp) return INT_MAX;
		return int(std::clamp<Sint64>(rate*warrior->hp/maximumHealth,1,INT_MAX));
	}

	inline void add_preemptive_hash(Uint32& signature, Uint32 value)
	{
		signature^=value;
		signature*=16777619u;
	}

}
}
