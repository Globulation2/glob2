// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CortexSnapshotQueries.h"

#include "CortexConstants.h"
#include "BuildingCapabilities.h"
#include "BuildingType.h"
#include "Game.h"
#include "Team.h"
#include <algorithm>
#include <limits>

namespace Cortex
{
// Fixed model/policy channels describe demands, never catalog family IDs. A
// mixed building contributes to every role it serves. The unused wall channel
// remains reserved so existing bounded policy arrays keep their shape.
inline unsigned buildingRoles(const AIEngine::AIWorldView& game, const BuildingType& type)
{
 const BuildingType* b = &type;
 if (b->isBuildingSite && b->nextLevel >= 0) b = catalogType(game,b->nextLevel);
 if (!b || !b->runtimeAvailable) return 0;
 const auto& s = b->semantics;
 unsigned roles = 0;
 auto add = [&](int role, bool yes) { if (yes) roles |= 1u << role; };
 add(CORTEX_BUILD_SWARM, s.production.enabledUnitMask != 0);
 add(CORTEX_BUILD_FOOD, b->maxUnitInside > 0 && s.feeding.enabled && (s.feeding.unitMask & s.admittedUnitMask));
 add(CORTEX_BUILD_HEAL, b->maxUnitInside > 0 && s.healing.enabled && (s.healing.unitMask & s.admittedUnitMask));
 auto trains = [&](int ability) { return b->maxUnitInside > 0 && s.training[ability].enabled && (s.training[ability].unitMask & s.admittedUnitMask); };
 add(CORTEX_BUILD_WALKSPEED, trains(WALK));
 add(CORTEX_BUILD_SWIMSPEED, trains(SWIM));
 add(CORTEX_BUILD_ATTACK, trains(ATTACK_SPEED) || trains(ATTACK_STRENGTH));
 bool construction = false;
 for (const auto& training : s.training)
  construction |= b->maxUnitInside > 0 && training.enabled && training.constructionLevel > 0 && (training.unitMask & s.admittedUnitMask & (1u << WORKER));
 add(CORTEX_BUILD_SCIENCE, construction || trains(BUILD) || trains(HARVEST));
 add(CORTEX_BUILD_DEFENSE, b->shootingRange > 0 && b->shootRhythm > 0 && std::any_of(s.projectileDamage.begin(),s.projectileDamage.end(),[](int damage){return damage>0;}));
 add(CORTEX_BUILD_EXPLORATION, b->zonable[EXPLORER] != 0);
 add(CORTEX_BUILD_WAR, b->zonable[WARRIOR] != 0);
 add(CORTEX_BUILD_CLEARING, b->zonable[WORKER] != 0);
 add(CORTEX_BUILD_EXCHANGE, (s.market.interTeamFruitExchange || b->runtimeSuppliesDirectStock) || b->runtimeSuppliesStock);
 return roles;
}
inline bool servesRole(const AIEngine::AIWorldView& game, const BuildingType& type, int role)
{
 return role >= 0 && role < CORTEX_BUILDING_TYPES && (buildingRoles(game, type) & (1u << role));
}

inline int primaryResource(const BuildingResourceCost& cost)
{
 int selected = -1;
 for (int r = 0; r < MAX_NB_RESOURCES; ++r)
  if (cost[r] > 0 && (selected < 0 || cost[r] > cost[selected])) selected = r;
 return selected;
}
// Resolve once before a placement search; never enumerate the catalog per tile.
// Cost then footprint then numeric ID give deterministic choices for unfamiliar
// providers while preserving the policy's existing strategic role demands.
inline AIPlanning::BuildingCandidate selectBuilding(const AIEngine::AIWorldView& game, const AIEngine::TeamView& team, int role, int productionClass = WORKER, int maxWorkerQualification = -1)
{
 using I = AIPlanning::BuildingIntent;
 AIPlanning::BuildingCandidate best;
 long long bestCost = std::numeric_limits<long long>::max();
 int bestArea = std::numeric_limits<int>::max();
 const int qualification = maxWorkerQualification >= 0 ? maxWorkerQualification : maxBuildLevel(game,team);
 auto consider = [&](I intent) {
  for (const auto& candidate : game.capabilities().placements(intent))
  {
   const auto& kind = game.catalog->at(candidate.placementType);
   if (!kind.available || !(kind.capabilityMask & (Uint64(1) << unsigned(intent)))) continue;
   const auto* placement = catalogType(game,candidate.placementType);
   const auto* completed = catalogType(game,candidate.completedType);
   if (!placement || !completed || placement->semantics.requiredWorkerLevel > qualification) continue;
   long long cost = 0;
   if (placement->isBuildingSite) for (int amount : placement->semantics.constructionCost) cost += amount;
   const int area = placement->width * placement->height;
   if (cost < bestCost || (cost == bestCost && (area < bestArea || (area == bestArea && candidate.placementType < best.placementType))))
   { best = candidate; bestCost = cost; bestArea = area; }
  }
 };
 switch (role)
 {
 case CORTEX_BUILD_SWARM: consider(productionClass == EXPLORER ? I::ProduceExplorer : productionClass == WARRIOR ? I::ProduceWarrior : I::ProduceWorker); break;
 case CORTEX_BUILD_FOOD: consider(I::Feed); break;
 case CORTEX_BUILD_HEAL: consider(I::Heal); break;
 case CORTEX_BUILD_WALKSPEED: consider(I::TrainWalk); break;
 case CORTEX_BUILD_SWIMSPEED: consider(I::TrainSwim); break;
 case CORTEX_BUILD_ATTACK: consider(I::TrainAttackSpeed); consider(I::TrainAttackStrength); break;
 case CORTEX_BUILD_SCIENCE: consider(I::TrainConstruction); consider(I::TrainBuild); consider(I::TrainHarvest); break;
 case CORTEX_BUILD_DEFENSE: consider(I::ProjectileDefense); break;
 case CORTEX_BUILD_EXPLORATION: consider(I::AttractExplorers); break;
 case CORTEX_BUILD_WAR: consider(I::AttractWarriors); break;
 case CORTEX_BUILD_CLEARING: consider(I::AttractWorkers); break;
 case CORTEX_BUILD_EXCHANGE: consider(I::ExchangeResources); break;
 default: break;
 }
 return best;
}
}

namespace Cortex {
inline AIPlanning::BuildingCandidate selectBuilding(::Game& game,::Team& team,int role,int productionClass=WORKER,int qualification=-1)
{
    const auto view=AIEngine::AIWorldView::capture(game, AIEngine::AIWorldView::captureCatalog(game));
    return selectBuilding(*view,view->teams[team.teamNumber],role,productionClass,qualification);
}
}
