// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include "field/TerrainMovementCosts.h"

namespace ResourceClaims
{
enum class OccupancyPolicy { Static, FrozenUnits };
struct RouteProfile
{
    int movement = 0;
    Uint32 teamMask = 1;
    Uint16 unitGid = 0xffff;
    OccupancyPolicy occupancy = OccupancyPolicy::Static;
};

// Frozen-world passability shared by claim route searches and route following.
// Static routing ignores moving traffic but retains immobile ground blockers.
// FrozenUnits additionally blocks other units at their sampled positions.
inline bool routePassable(const MapState::View& view, std::size_t index, const RouteProfile& profile)
{
    const auto& tile = view.occupancy[index];
    if (!gradient_kernel::terrainAllowsMovement(view.terrainProperties(index), profile.movement)) return false;
    if (profile.movement == AIR_MOVEMENT_CLASS)
        return !MapState::resourceBlocksAir(view, index) &&
            (profile.occupancy == OccupancyPolicy::Static || tile.airUnit == 0xffff || tile.airUnit == profile.unitGid);
    if ((view.areas[index].forbidden & profile.teamMask) || tile.building != 0xffff ||
        MapState::resourceBlocksGround(view, index)) return false;
    const bool own = profile.unitGid != 0xffff && tile.groundUnit == profile.unitGid;
    if (tile.immobileUnit != 255 && !own) return false;
    return profile.occupancy == OccupancyPolicy::Static || tile.groundUnit == 0xffff || own;
}

inline unsigned routeEntryCost(const MapState::View& view, std::size_t destination,
                              const RouteProfile& profile, bool diagonal)
{
    const auto& steps = (*view.rules)[view.cellRules[destination]].ground[profile.movement];
    return diagonal ? steps.diagonal : steps.cardinal;
}
}
