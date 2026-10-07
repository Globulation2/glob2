// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include "ResourceProperties.h"

namespace AIResourceSources
{
// Planning potential under harvesting, using the common ecology denominator.
// Natural-growth rules affect finite renewal, not an inexhaustible live stock.
inline std::uint64_t renewablePotential(const MapState::View& world,std::size_t index,MaterialId material)
{
    const auto type=world.resources[index].resource.type;
    if(type==NO_RES_TYPE) return 0;
    const auto& yield=world.resourceRegistry->yields(static_cast<ResourceId>(type))[materialIndex(material)];
    if(!yield.capacity || !MapState::materialAmountAt(world,index,material)) return 0;
    if(yield.consumption==ResourceConsumption::Infinite && !yield.destroysDeposit) return ResourceRateScale;
    return MapState::materialRenewalPotential(world,index,material)/(1u<<world.resourceScarcityLevel);
}
inline bool produces(const MapState::View& world,std::size_t index,MaterialId material)
{ return MapState::materialAmountAt(world,index,material)>0; }
}
