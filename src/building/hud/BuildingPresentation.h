// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingType.h"
#include <Toolkit.h>
#include <StringTable.h>

inline std::string buildingDisplayName(const BuildingType& type)
{
    const auto& name=type.presentation.displayName;
    const std::string key="["+name+"]";
    const auto* strings=GAGCore::Toolkit::getStringTable();
    return strings->doesStringExist(key) ? strings->getString(key) : name;
}

// Uniform damage retains the compact stock display. Mixed damage gets one row
// per unit class, shared by drawing and hit testing so later controls stay aligned.
inline int buildingProjectileDamageRows(const BuildingType& type)
{
    if (type.shootRhythm <= 0 || type.shootingRange <= 0) return 0;
    const auto& damage=type.semantics.projectileDamage;
    return damage[0]==damage[1] && damage[0]==damage[2] ? 1 : NB_UNIT_TYPE;
}

inline int buildingProjectileStatsHeight(const BuildingType& type)
{
    const int rows=buildingProjectileDamageRows(type);
    return rows ? (rows+1)*11 : 0;
}

// Map status uses the actual recipes. A free meal is never resource-starved;
// optional fruit is deliberately excluded from the readiness indication.
inline bool buildingFeedingUnfunded(const BuildingType& type, const Sint32* stock)
{
    if (!type.semantics.feeding.enabled) return false;
    for (int resource=0; resource<MAX_RESOURCES; ++resource)
        if (stock[resource] < type.semantics.feeding.cost[resource]) return true;
    return false;
}

// The existing single stock bar shows the scarcest configured input, measured
// in recipe batches. Stock definitions have one wheat input, retaining that bar.
inline int buildingResourceBarResource(const BuildingType& type, const Sint32* stock)
{
    int selected=-1, selectedCost=1;
    const auto consider=[&](const BuildingResourceCost& cost) {
        for (int resource=0; resource<MAX_RESOURCES; ++resource)
            if (cost[resource]>0 && type.maxResource[resource]>0 &&
                (selected<0 || Sint64(stock[resource])*selectedCost < Sint64(stock[selected])*cost[resource]))
            { selected=resource; selectedCost=cost[resource]; }
    };
    if (type.semantics.feeding.enabled) consider(type.semantics.feeding.cost);
    for (const auto& recipe : type.semantics.production.recipes)
        if (recipe.enabled) consider(recipe.cost);
    return selected;
}
