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

inline const BuildingType& completed(const BuildingsTypes& catalog,const BuildingType& type)
{
    return type.isBuildingSite && type.nextLevel>=0 ? *catalog.get(type.nextLevel) : type;
}
inline bool trainsWarriorCombat(const BuildingType& type)
{
    if(type.maxUnitInside<=0 || !(type.semantics.admittedUnitMask&(1u<<WARRIOR)))return false;
    for(int ability:{ATTACK_SPEED,ATTACK_STRENGTH}) {
        const auto& training=type.semantics.training[ability];
        if(training.enabled && (training.unitMask&(1u<<WARRIOR)))return true;
    }
    return false;
}
inline int channel(const BuildingsTypes& catalog,const BuildingType& input)
{
    const auto& type=completed(catalog,input);const auto& s=type.semantics;
    const bool seats=type.maxUnitInside>0;
    auto training=[&](int ability){return seats && s.training[ability].enabled && (s.training[ability].unitMask&s.admittedUnitMask);};
    if(s.production.enabledUnitMask)return Production;
    if(seats && s.feeding.enabled && (s.feeding.unitMask&s.admittedUnitMask))return Feeding;
    if(seats && s.healing.enabled && (s.healing.unitMask&s.admittedUnitMask))return Healing;
    if(training(WALK))return Walking;
    if(training(SWIM))return Swimming;
    if(training(ATTACK_SPEED)||training(ATTACK_STRENGTH))return CombatTraining;
    for(int ability=0;ability<NB_ABILITY;++ability)if(training(ability))return Education;
    if(type.shootingRange>0 && type.shootRhythm>0 &&
        std::any_of(s.projectileDamage.begin(),s.projectileDamage.end(),[](int n){return n>0;}))return ProjectileDefense;
    if(type.zonable[EXPLORER])return Exploration;
    if(type.zonable[WARRIOR])return WarriorAttraction;
    if(type.zonable[WORKER])return WorkerAttraction;
    if((s.market.interTeamFruitExchange || type.runtimeSuppliesDirectStock) || type.runtimeSuppliesStock)return Exchange;
    return s.occupiesGround ? PassiveGround : -1;
}
inline std::vector<int> channels(const BuildingsTypes& catalog)
{
    std::vector<int> result;result.reserve(catalog.size());
    for(size_t i=0;i<catalog.size();++i)result.push_back(channel(catalog,*catalog.get(i)));
    return result;
}
}
