// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuildingCapabilities.h"
#include "BuildingType.h"
#include "Game.h"
#include "ai/observation/AIWorldView.h"

namespace AISharedRuntime
{
// Integer keys are retained by the runtime's serialized condition language.
// Values are shared semantic intents, never catalog IDs or exclusive families.
namespace BuildingDemand
{
#define AI_BUILDING_DEMAND(name) inline constexpr int name=static_cast<int>(AIPlanning::BuildingIntent::name)
AI_BUILDING_DEMAND(ProduceWorker); AI_BUILDING_DEMAND(Feed); AI_BUILDING_DEMAND(Heal);
AI_BUILDING_DEMAND(TrainWalk); AI_BUILDING_DEMAND(TrainSwim); AI_BUILDING_DEMAND(TrainAttackStrength);
AI_BUILDING_DEMAND(TrainConstruction); AI_BUILDING_DEMAND(ProjectileDefense);
AI_BUILDING_DEMAND(AttractExplorers); AI_BUILDING_DEMAND(AttractWarriors);
AI_BUILDING_DEMAND(ClearResources); AI_BUILDING_DEMAND(ExchangeResources); AI_BUILDING_DEMAND(Count);
#undef AI_BUILDING_DEMAND
}
inline AIPlanning::BuildingIntent buildingIntent(int demand)
{
 return static_cast<AIPlanning::BuildingIntent>(demand);
}
template<class World> inline bool buildingProvides(const World& game,int type,int demand)
{
 if(type<0 || size_t(type)>=game.buildingsTypes.size() || demand<0 || demand>=BuildingDemand::Count) return false;
 const auto* definition=game.buildingsTypes.get(type);
 return game.buildingCapabilities().matches(definition->isBuildingSite ? definition->nextLevel : type,buildingIntent(demand));
}
// Version-gated imports only. New strategy state never stores these family IDs.
inline int importLegacyBuildingDemand(int family)
{
 constexpr int demands[]={BuildingDemand::ProduceWorker,BuildingDemand::Feed,BuildingDemand::Heal,
  BuildingDemand::TrainWalk,BuildingDemand::TrainSwim,BuildingDemand::TrainAttackStrength,
  BuildingDemand::TrainConstruction,BuildingDemand::ProjectileDefense,BuildingDemand::AttractExplorers,
  BuildingDemand::AttractWarriors,BuildingDemand::ClearResources,BuildingDemand::Count,BuildingDemand::ExchangeResources};
 return family>=0 && family<int(std::size(demands)) ? demands[family] : BuildingDemand::Count;
}
inline int importLegacyBuildingId(Game& game,int family,int level=0,bool site=false)
{
 constexpr const char* names[]={"swarm","inn","hospital","racetrack","swimmingpool","barracks","school","defencetower","explorationflag","warflag","clearingflag","stonewall","market"};
 if(family<0 || family>=int(std::size(names))) return -1;
 int id=game.buildingsTypes.getTypeNum(names[family],level,site);
 if(id<0 && site) id=game.buildingsTypes.getTypeNum(names[family],level,false);
 return id;
}
}
