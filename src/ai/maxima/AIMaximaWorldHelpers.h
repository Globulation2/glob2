// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#pragma once

#include "Building.h"
#include "BuildingType.h"
#include "ai/observation/ObservationQueries.h"
#include "Unit.h"
#include "AIMaximaLabour.h"
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

    // This strategy deliberately estimates both attack abilities at their
    // lower level. Using mixed cached levels would change stock training decisions.
    namespace detail
    {
    // The caller has already established an effective melee role. Keep this
    // internal seam separate from the checked entry point used by other callers.
    inline int unlearned_eligible_damage_rate(const AIEngine::AIWorldView& world,
        const AIEngine::UnitView& unit, int referenceArmour)
    {
        const int level=std::clamp(std::min(unit.level[ATTACK_SPEED],unit.level[ATTACK_STRENGTH]),0,NB_UNIT_LEVELS-1);
        // Imported historical race tables used this private, fixed strategy
        // calibration even when their cached ability tables differed.
        if (unit.capabilityFlags&UnitRuntimeTraits::LegacyPerformancePolicies) {
            constexpr int historicalRates[]={36,64,110,168};
            return historicalRates[level];
        }
        const auto& performance=world.unitCatalog().levels(unit.typeNum)[level].performance;
        return Labour::combatDamageRate(performance[ATTACK_SPEED],performance[ATTACK_STRENGTH],referenceArmour);
    }
    }

    inline int unlearned_warrior_damage_rate(const AIEngine::AIWorldView& world,
        const AIEngine::UnitView& unit, int referenceArmour)
    {
        if (!AIEngine::ObservationQueries::matchesStrategyUnitRole(world,unit,WARRIOR)) return 0;
        return detail::unlearned_eligible_damage_rate(world,unit,referenceArmour);
    }

    inline bool unlearned_reference_candidate(const AIEngine::AIWorldView& world,unsigned id)
    {
        const auto& definition=world.unitCatalog().definition(id);
        if (!definition.runtime.has(UnitRuntimeTraits::Melee)
            || (!definition.requiredExperiment.empty()
                && !world.configuration->getExperiments().has(definition.requiredExperiment))) return false;
        const auto& levels=world.unitCatalog().levels(id);
        return (levels[1].performance[ATTACK_SPEED]>0 && levels[1].performance[ATTACK_STRENGTH]>0)
            || (levels[2].performance[ATTACK_SPEED]>0 && levels[2].performance[ATTACK_STRENGTH]>0);
    }

    inline int unlearned_reference_armour(const AIEngine::AIWorldView& world)
    {
        const auto armour=[&](unsigned id) {
            const auto& levels=world.unitCatalog().levels(id);
            return int((Sint64(std::max(0,levels[1].performance[ARMOR]))
                +std::max(0,levels[2].performance[ARMOR]))/2);
        };
        if (world.unitTraits(WARRIOR).has(UnitRuntimeTraits::LegacyPerformancePolicies)
            || unlearned_reference_candidate(world,WARRIOR)) return armour(WARRIOR);
        // Strategy still produces the built-in trio. When its warrior is
        // disabled, other enabled melee definitions can nevertheless be enemies.
        // Resolve their conservative reference once, outside the army scan.
        int result=0;
        for(unsigned id=0;id<world.unitTypeCount();++id)
            if(unlearned_reference_candidate(world,id))result=std::max(result,armour(id));
        return result;
    }

    inline int unlearned_reference_damage_rate(const AIEngine::AIWorldView& world,int referenceArmour)
    {
        if (world.unitTraits(WARRIOR).has(UnitRuntimeTraits::LegacyPerformancePolicies)) return 87;
        const auto rate=[&](unsigned id) {
            const auto& levels=world.unitCatalog().levels(id);
            const int first=Labour::combatDamageRate(levels[1].performance[ATTACK_SPEED],levels[1].performance[ATTACK_STRENGTH],referenceArmour);
            const int second=Labour::combatDamageRate(levels[2].performance[ATTACK_SPEED],levels[2].performance[ATTACK_STRENGTH],referenceArmour);
            return int((Sint64(first)+second)/2);
        };
        if(unlearned_reference_candidate(world,WARRIOR))return rate(WARRIOR);
        int result=0;
        for(unsigned id=0;id<world.unitTypeCount();++id)
            if(unlearned_reference_candidate(world,id))result=std::max(result,rate(id));
        return result;
    }

	inline void add_preemptive_hash(Uint32& signature, Uint32 value)
	{
		signature^=value;
		signature*=16777619u;
	}

}
}
