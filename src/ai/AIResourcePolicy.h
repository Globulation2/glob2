// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"

// Shared controller policy over captured world records.
namespace AIResourcePolicy
{
// Propagation is distinct from local regrowth: a source on a growth-blocked
// cell can still produce offspring on an eligible neighbor.
inline bool canPropagate(const MapState::View& world,std::size_t index,MaterialId material)
{
    return MapState::materialAmountAt(world,index,material)>0 && MapState::materialExpansionRate(world,index,material)>0;
}

// Whole-tile forbidden areas protect finite spreading donors. Infinite yields
// survive harvesting; local-only regrowth is managed by per-stock farm reserves,
// since permanently forbidding its sole producer would yield no materials.
inline bool needsSeedReserve(const MapState::View& world,std::size_t index,MaterialId material)
{
    if (!canPropagate(world,index,material)) return false;
    return world.resourceRegistry->yields(static_cast<ResourceId>(world.resources[index].resource.type))[materialIndex(material)].consumption
        !=ResourceConsumption::Infinite;
}

inline bool emptyGrowthCell(const MapState::View& world,std::size_t index)
{
    return MapState::resourcesMayGrow(world,index) && world.resources[index].resource.type==NO_RES_TYPE
        && world.occupancy[index].building==NOGBID;
}

// A future growing area follows this donor's habitat, not the union of every
// catalog entry yielding the same material. Transient unit occupancy does not
// invalidate a long-lived farming reservation.
inline bool canSpreadTo(const MapState::View& world,std::size_t source,std::size_t target,MaterialId material)
{
    return canPropagate(world,source,material)
        && emptyGrowthCell(world,target)
        && MapState::terrainSupportsResourceSlot(world,target,world.resources[source].resource.type);
}

// Waiting for extraction to replenish a tile is useful only if that exact
// stock can grow locally, or a neighboring donor can colonize the empty tile.
inline bool canRecoverAt(const MapState::View& world,int x,int y,MaterialId material)
{
    const auto index=world.index(x,y);
    if (MapState::materialGrowthRate(world,index,material)) return true;
    if (!emptyGrowthCell(world,index)) return false;
    for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx)
        if ((dx || dy) && canSpreadTo(world,world.index(x+dx,y+dy),index,material)) return true;
    return false;
}
}
