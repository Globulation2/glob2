// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingType.h"
#include "UnitConsts.h"
#include <algorithm>
#include <array>
#include <vector>

// A bounded, lossy observation adapter for existing learned models. These
// channels are not engine archetypes, catalog identities, or strategic demands.
namespace ModelBuildingProjection
{
enum Channel { Production, Feeding, Healing, Walking, Swimming, CombatTraining,
    Education, ProjectileDefense, Exploration, WarriorAttraction, WorkerAttraction,
    PassiveGround, Exchange, Count };

template<class Catalog> inline const BuildingType& completed(const Catalog& catalog,const BuildingType& type)
{
    return type.isBuildingSite && type.nextLevel>=0 ? *catalog.get(type.nextLevel) : type;
}
inline bool trainsWarriorCombat(const BuildingType& type)
{
    return type.maxUnitInside>0 && (type.runtimeTrainingAbilities&((1u<<ATTACK_SPEED)|(1u<<ATTACK_STRENGTH)));
}
inline int channelCompleted(const BuildingType& type)
{
    const auto& s=type.semantics;
    const bool seats=type.maxUnitInside>0;
    auto training=[&](int ability){return seats && (type.runtimeTrainingAbilities&(1u<<ability));};
    if(!s.production.enabledUnits.empty())return Production;
    if(seats && type.runtimeFeeds)return Feeding;
    if(seats && type.runtimeHeals)return Healing;
    if(training(WALK))return Walking;
    if(training(SWIM))return Swimming;
    if(training(ATTACK_SPEED)||training(ATTACK_STRENGTH))return CombatTraining;
    for(int ability=0;ability<NB_ABILITY;++ability)if(training(ability))return Education;
    if(type.shootingRange>0 && type.shootRhythm>0 &&
        (type.runtimeAnyProjectileDamage || s.projectileBuildingDamage>0))return ProjectileDefense;
    if(type.runtimeAttractionRoles&2)return Exploration;
    if(type.runtimeAttractionRoles&4)return WarriorAttraction;
    if(type.runtimeAttractionRoles&1)return WorkerAttraction;
    if((s.market.interTeamFruitExchange || type.runtimeSuppliesDirectStock) || type.runtimeSuppliesStock)return Exchange;
    return s.occupiesGround ? PassiveGround : -1;
}
template<class Catalog> inline int channel(const Catalog& catalog,const BuildingType& input)
{ return channelCompleted(completed(catalog,input)); }
inline std::vector<int> channels(const BuildingsTypes& catalog)
{
    std::vector<int> result;result.reserve(catalog.size());
    for(size_t i=0;i<catalog.size();++i)result.push_back(channel(catalog,*catalog.get(i)));
    return result;
}
}
