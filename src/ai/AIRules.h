// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingCapabilities.h"
#include "BuildingType.h"
class Game;
#include <algorithm>

namespace AIRules
{
inline bool trainingBuilding(const BuildingType& type)
{
    return std::any_of(type.semantics.training.begin(),type.semantics.training.end(),
        [](const auto& training){return training.enabled;});
}
bool usefulWithoutTraining(const Game& game,int type);
// A disabled operation does not make another service of the same building
// useless. Neutral structures, such as passive barriers, remain placeable.
bool usefulBuilding(const Game& game,int type);
}
