// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GameHeader.h"
#include "IntBuildingType.h"

// Shared capability gates, not a variant-specific tuning profile. Apply these
// before planning, staffing and prerequisites; rejecting the final order alone
// leaves controllers waiting for impossible work. Extend these and the controller
// decision tests when adding custom rules. Healing and production remain separate.
namespace AIRules
{
inline bool trainingBuilding(int type)
{
    return type == IntBuildingType::SCIENCE_BUILDING
        || type == IntBuildingType::WALKSPEED_BUILDING
        || type == IntBuildingType::SWIMSPEED_BUILDING;
}
inline bool usefulBuilding(const GameHeader& rules, int type)
{
    // New barracks are a training investment in these controllers. Hospitals
    // still provide healing; existing barracks retain their healing service.
    if (rules.isUnitUpgradesDisabled() && (trainingBuilding(type) || type==IntBuildingType::ATTACK_BUILDING)) return false;
    if (rules.isPeacefulModeEnabled()
        && (type == IntBuildingType::ATTACK_BUILDING
            || type == IntBuildingType::DEFENSE_BUILDING
            || type == IntBuildingType::WAR_FLAG)) return false;
    return true;
}
}
