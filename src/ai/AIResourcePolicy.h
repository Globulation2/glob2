// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Map.h"
#include "ResourceRegistry.h"

namespace AIResourcePolicy
{
// Propagation is distinct from local regrowth: a source on a growth-blocked
// cell can still produce offspring on an eligible neighbor.
inline bool canPropagate(const Map& map,int x,int y,MaterialId material)
{
    const auto index=map.coordToIndex(x,y);
    return map.materialAmountAt(index,material)>0
        && map.materialExpansionRateAtSlot(index,materialIndex(material))>0;
}

// Whole-tile forbidden areas protect finite spreading donors. Infinite yields
// survive harvesting; local-only regrowth is managed by per-stock farm reserves,
// since permanently forbidding its sole producer would yield no materials.
inline bool needsSeedReserve(const Map& map,int x,int y,MaterialId material)
{
    if (!canPropagate(map,x,y,material)) return false;
    const auto id=static_cast<ResourceId>(map.getResource(x,y).type);
    return map.resourceRegistry().yields(id)[materialIndex(material)].consumption
        !=ResourceConsumption::Infinite;
}

inline bool emptyGrowthCell(const Map& map,int x,int y)
{
    const auto& cell=map.getTile(x,y);
    return map.canResourcesGrow(x,y) && cell.resource.type==NO_RES_TYPE
        && cell.building==NOGBID;
}

// A future growing area follows this donor's habitat, not the union of every
// catalog entry yielding the same material. Transient unit occupancy does not
// invalidate a long-lived farming reservation.
inline bool canSpreadTo(const Map& map,int sourceX,int sourceY,
    int targetX,int targetY,MaterialId material)
{
    return canPropagate(map,sourceX,sourceY,material)
        && emptyGrowthCell(map,targetX,targetY)
        && map.terrainSupportsResourceAt(map.coordToIndex(targetX,targetY),
            static_cast<ResourceId>(map.getResource(sourceX,sourceY).type));
}

// Waiting for extraction to replenish a tile is useful only if that exact
// stock can grow locally, or a neighboring donor can colonize the empty tile.
inline bool canRecoverAt(const Map& map,int x,int y,MaterialId material)
{
    if (map.materialGrowthRateAtSlot(map.coordToIndex(x,y),materialIndex(material)))
        return true;
    if (!emptyGrowthCell(map,x,y)) return false;
    for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx)
        if ((dx || dy) && canSpreadTo(map,x+dx,y+dy,x,y,material)) return true;
    return false;
}
}
