// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Map.h"

namespace MapGeneration
{
// Future-connectivity checks may ignore removable obstructions, but must not
// assume that every permanent barrier has the historical stone identity.
inline bool permanentResourceBarrier(const Map& map, size_t index, bool forBuilding=false)
{
    if (forBuilding ? !map.resourceBlocksBuilding(index) : !map.resourceBlocksGround(index)) return false;
    const auto id=static_cast<ResourceId>(map.getResource(index).type);
    const auto& properties=map.resourceRegistry().properties(id);
    if (properties.clearable) return false;
    bool infinite=false;
    for (unsigned material=0;material<MaterialCount;++material)
    {
        const auto& yield=map.resourceRegistry().yields(id)[material];
        if (!yield.capacity) continue;
        if ((yield.consumption==ResourceConsumption::All || yield.destroysDeposit)
            && (map.materialAmountAtSlot(index,material) || yield.growthRate)) return false;
        infinite |= yield.consumption==ResourceConsumption::Infinite
            && (map.materialAmountAtSlot(index,material) || yield.growthRate);
    }
    return properties.persistsWhenEmpty || infinite;
}
}
