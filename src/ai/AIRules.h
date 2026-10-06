// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingCapabilities.h"
#include "BuildingType.h"
#include "Game.h"
#include <algorithm>

namespace AIRules
{
inline bool trainingBuilding(const BuildingType& type)
{
    return std::any_of(type.semantics.training.begin(),type.semantics.training.end(),
        [](const auto& training){return training.enabled;});
}
inline bool usefulWithoutTraining(const Game& game,int type)
{
    const auto& index=game.buildingCapabilities();
    for(unsigned value=0;value<static_cast<unsigned>(AIPlanning::BuildingIntent::Count);++value)
    {
        const auto intent=static_cast<AIPlanning::BuildingIntent>(value);
        if(AIPlanning::BuildingCapabilityIndex::trainingAbility(intent)>=0
            || intent==AIPlanning::BuildingIntent::TrainConstruction) continue;
        if(index.available(type,intent,game.gameHeader)) return true;
    }
    return false;
}
// A disabled operation does not make another service of the same building
// useless. Neutral structures, such as passive barriers, remain placeable.
inline bool usefulBuilding(const Game& game,int type)
{
    if(type<0 || size_t(type)>=game.buildingsTypes.size()) return false;
    const auto* placement=game.buildingsTypes.get(type);
    if(!placement->runtimeAvailable) return false;
    const int completed=placement->isBuildingSite ? placement->nextLevel : type;
    const auto& index=game.buildingCapabilities();
    bool recognized=false;
    for(unsigned value=0;value<static_cast<unsigned>(AIPlanning::BuildingIntent::Count);++value)
    {
        const auto intent=static_cast<AIPlanning::BuildingIntent>(value);
        if(!index.matches(completed,intent)) continue;
        recognized=true;
        if(index.available(completed,intent,game.gameHeader)) return true;
    }
    return !recognized;
}
}
