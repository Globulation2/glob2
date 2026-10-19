// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientFieldIdentity.h"
#include "UnitConsts.h"
#include "TeamCapacity.h"
#include <array>
#include <cstdint>

// A single tile's seed inputs, copied before a mutation. This is stack state,
// not a per-field or per-unit cache. Supplier masks describe the shared sparse
// footprint union and are independent of the movement class using that union.
struct GradientTargetCell
{
    std::uint32_t materials=0,visibleTeams=0,forbidden=0,guard=0,clear=0,farm=0;
    std::uint32_t guardBuildingBlockedTeams=0;
    std::array<std::uint32_t,TEAM_MAX_COUNT> occupyingSupplies{},overlaySupplies{};
    std::uint16_t openClasses=0;
    std::uint8_t buildingTeam=255;
    bool groundBlocked=false,airBlocked=false,immobile=false,clearable=false,farmable=false,farming=false;
    // Persistent route inputs only: ordinary moving-unit occupancy is absent.
    std::array<std::uint32_t,4> terrainCosts{};
    bool goal(GradientFieldKey key) const {
        const auto mask=std::uint32_t(1)<<key.team;
        const bool air=key.swim==AIR_MOVEMENT_CLASS;
        const bool denied=!air && (forbidden&mask);
        const bool mobile=air || !immobile;
        const bool blocked=air ? airBlocked : groundBlocked;
        const bool open=openClasses&(std::uint16_t(1)<<key.swim);
        switch(key.family) {
        case GradientFieldFamily::Materials:
        case GradientFieldFamily::Markets:
            if(denied || !mobile) return false;
            if((materials&(std::uint32_t(1)<<key.material)) && (visibleTeams&mask)) return true;
            if(key.family==GradientFieldFamily::Materials) return false;
            return (overlaySupplies[key.team]&(std::uint32_t(1)<<key.material))
                || (!blocked && buildingTeam==key.team
                    && (occupyingSupplies[key.team]&(std::uint32_t(1)<<key.material)));
        case GradientFieldFamily::Guard:
            return !denied && mobile && !blocked && open && (guard&mask)
                && (air || !(guardBuildingBlockedTeams&mask));
        case GradientFieldFamily::Clear:
            // Same goal-first precedence as the generated clearSeed predicate.
            return !denied && clearable && ((clear&mask) || (farming && (farm&mask) && !farmable));
        case GradientFieldFamily::Forbidden:
            return !denied && mobile && !blocked && open && (air || buildingTeam==255);
        }
        return false;
    }
    bool sameRoutes(const GradientTargetCell& other) const {
        return forbidden==other.forbidden && openClasses==other.openClasses
            && groundBlocked==other.groundBlocked && airBlocked==other.airBlocked
            && immobile==other.immobile && buildingTeam==other.buildingTeam
            && guardBuildingBlockedTeams==other.guardBuildingBlockedTeams
            && terrainCosts==other.terrainCosts;
    }
};
