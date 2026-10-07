// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AIMaximaBuildingRoles.h"
#include "BuildingCapabilities.h"
#include "BuildingType.h"
#include "Game.h"
#include "Team.h"
#include "UnitConsts.h"
#include "ai/observation/ObservationQueries.h"
#include <algorithm>
#include <limits>
namespace AIMaximaBuildings
{
inline const BuildingType* completed(const AIEngine::AIWorldView& world,const BuildingType& type)
{ return type.isBuildingSite && type.nextLevel>=0 ? &world.catalog->at(type.nextLevel).resolvedType : &type; }
inline int lineageRoot(const AIEngine::AIWorldView& world,int type) {return world.capabilities().lineageRoot(type);}
inline int lineagePosition(const AIEngine::AIWorldView& world,int type) {return world.capabilities().lineagePosition(type);}
template<class World>
inline const BuildingType* completed(const World& game, const BuildingType& type)
{ return type.isBuildingSite && type.nextLevel>=0 ? game.buildingsTypes.get(type.nextLevel) : &type; }
// A service timeout counts unit actions. The final action performs completion;
// insideSpeed advances the unit's fixed-point action clock each simulation tick.
inline int serviceTicks(const BuildingType& type,int duration)
{
 // Nominal cardinal cadence; the engine never completes two actions in one tick.
 const int advance=std::clamp(type.insideSpeed,1,UNIT_DELTA_QUANTUM);
 return int(((static_cast<long long>(std::max(0,duration))+1)*UNIT_DELTA_QUANTUM+advance-1)/advance);
}
template<class World>
inline unsigned capabilities(const World& game, const BuildingType& type)
{
 const auto* b=completed(game,type); if(!b || !b->runtimeAvailable)return 0;
 const auto& s=b->semantics; unsigned result=0;
 auto add=[&](int role,bool yes){if(yes)result|=roleBit(role);};
 auto trains=[&](int ability){const auto& t=s.training[ability];return b->maxUnitInside>0 && t.enabled && (t.unitMask&s.admittedUnitMask);};
 add(Production,s.production.enabledUnitMask);
 add(Feeding,b->maxUnitInside>0&&s.feeding.enabled&&(s.feeding.unitMask&s.admittedUnitMask));
 add(Healing,b->maxUnitInside>0&&s.healing.enabled&&(s.healing.unitMask&s.admittedUnitMask));
 add(WalkTraining,trains(WALK));add(SwimTraining,trains(SWIM));
 add(CombatTraining,trains(ATTACK_SPEED)||trains(ATTACK_STRENGTH));
 for(const auto& t:s.training) add(ConstructionTraining,b->maxUnitInside>0&&t.enabled&&t.constructionLevel>0&&(t.unitMask&s.admittedUnitMask&(1u<<WORKER)));
 add(ProjectileDefense,b->shootingRange>0&&b->shootRhythm>0&&std::any_of(s.projectileDamage.begin(),s.projectileDamage.end(),[](int damage){return damage>0;}));
 add(ExploreAttraction,b->zonable[EXPLORER]);add(WarriorAttraction,b->zonable[WARRIOR]);add(WorkerAttraction,b->zonable[WORKER]);
 add(ResourceExchange,(s.market.interTeamFruitExchange || b->runtimeSuppliesDirectStock)||b->runtimeSuppliesStock);
 return result;
}
template<class World>
inline bool serves(const World& game,const BuildingType& type,int role)
{return (capabilities(game,type)&roleBit(role))!=0;}
template<class World>
inline int lineageRoot(const World& game,int type)
{return game.buildingCapabilities().lineageRoot(type);}
template<class World>
inline int lineagePosition(const World& game,int type)
{return game.buildingCapabilities().lineagePosition(type);}
inline AIPlanning::BuildingCandidate choose(const AIEngine::AIWorldView& game,const AIEngine::TeamView& team,int role)
{
 using I=AIPlanning::BuildingIntent;AIPlanning::BuildingCandidate best;long long score=std::numeric_limits<long long>::max();
 const int qualification=AIEngine::ObservationQueries::maxBuildLevel(game,team.number);
 auto consider=[&](I intent){for(const auto& c:game.capabilities().placements(intent)){
  if(!AIEngine::ObservationQueries::available(game,c,intent))continue;
  const auto* p=&game.catalog->at(c.placementType).resolvedType;if(p->semantics.requiredWorkerLevel>qualification)continue;
  long long cost=p->width*p->height;for(int r:p->semantics.constructionCost)if(p->isBuildingSite)cost+=r;
  if(cost<score||(cost==score&&c.placementType<best.placementType)){score=cost;best=c;}
 }};
 switch(role){case Production:consider(I::ProduceWorker);break;case Feeding:consider(I::Feed);break;case Healing:consider(I::Heal);break;case WalkTraining:consider(I::TrainWalk);break;case SwimTraining:consider(I::TrainSwim);break;case CombatTraining:consider(I::TrainAttackSpeed);consider(I::TrainAttackStrength);break;case ConstructionTraining:consider(I::TrainConstruction);break;case ProjectileDefense:consider(I::ProjectileDefense);break;case ExploreAttraction:consider(I::AttractExplorers);break;case WarriorAttraction:consider(I::AttractWarriors);break;case WorkerAttraction:consider(I::AttractWorkers);break;case ResourceExchange:consider(I::ExchangeResources);break;default:break;}return best;
}
}
