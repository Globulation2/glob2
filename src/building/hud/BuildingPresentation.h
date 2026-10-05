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
