// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Map.h"
#include "ResourceProperties.h"
#include <algorithm>

namespace AIResourceSources
{
// Expected replenishment in the common ecology denominator. Infinite stocks are
// supply without ecological renewal; finite stocks count actual growth/spread.
inline std::uint64_t renewableRate(const Map& map,std::size_t index,int material)
{
    const auto& resource=map.getResource(index);
    if(resource.type==NO_RES_TYPE || !validMaterial(material)) return 0;
    const auto& yield=map.resourceRegistry().yields(static_cast<ResourceId>(resource.type))[material];
    if(!yield.capacity) return 0;
    if(yield.consumption==ResourceConsumption::Infinite) return ResourceRateScale;
    return map.materialGrowthRateAt(index,material)+map.materialExpansionRateAt(index,material);
}
inline bool produces(const Map& map,std::size_t index,int material)
{ return map.materialAmountAt(index,material)>0; }
}
