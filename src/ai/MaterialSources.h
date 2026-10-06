// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Map.h"
#include "ResourceProperties.h"
#include "Game.h"
#include <algorithm>

namespace AIResourceSources
{
// Planning potential under harvesting, using the common ecology denominator.
// Natural-growth rules affect finite renewal, not an inexhaustible live stock.
inline std::uint64_t renewablePotential(const Map& map,std::size_t index,int material)
{
    const auto& resource=map.getResource(index);
    if(resource.type==NO_RES_TYPE || !validMaterial(material)) return 0;
    const auto& yield=map.resourceRegistry().yields(static_cast<ResourceId>(resource.type))[material];
    if(!yield.capacity || !map.materialAmountAtSlot(index,material)) return 0;
    if(yield.consumption==ResourceConsumption::Infinite && !yield.destroysDeposit) return ResourceRateScale;
    auto potential=std::uint64_t(map.materialRenewalPotentialAtSlot(index,material))
        +map.materialExpansionRateAtSlot(index,material);
    if(map.game) potential/=1u<<map.game->gameHeader.getResourceScarcityLevel();
    return potential;
}
inline bool produces(const Map& map,std::size_t index,int material)
{ return map.materialAmountAtSlot(index,material)>0; }
}
