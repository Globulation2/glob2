// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <list>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <climits>

#include "Building.h"
#include "FetchApportionment.h"
#include "BuildingType.h"
#include "FixedPoint.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Order.h"
#include "field/AirPathfind.h"
#include "field/TerrainMovementCosts.h"

namespace
{
	/// Weight of the harvest level in a resource candidate's ranking key. The
	/// harvest level dominates the comparison; the walk level breaks ties.
	constexpr int HARVEST_LEVEL_WEIGHT = 10;

	/// Tiles of detour a candidate pays for turning up with a resource this
	/// building cannot take. Whatever it carries is lost the moment it harvests
	/// again, so an empty-handed unit this much further away is the better hire,
	/// and a building that does want the cargo gets its chance at the unit. A
	/// price, not a veto: past this margin the loaded unit is still hired.
	constexpr int CARRIED_RESOURCE_PENALTY_TILES = 5;

	/// Composite "experience" key used to rank resource-carrying candidates;
	/// higher is preferred.
	int bringResourcesLevel(const Unit* unit)
	{
		return unit->workerLevel() * HARVEST_LEVEL_WEIGHT + unit->level[WALK];
	}
}

void Building::step(void)
{
	if (type->isBuildingSite && (siteCompletionPending || type->useTeamResources)) updateBuildingSite();
	computeWishedResources(wishedResources);
	if (((owner->game->stepCounter + gid) & 255) == 0)
		freeIdleGradients();

	updateCallLists();
	if(underAttackTimer>0)
		underAttackTimer--;
	if(canNotConvertUnitTimer>0)
		canNotConvertUnitTimer--;
	// NOTE : Unit needs to update itself when it is in a building
}


void Building::setRecordFailingUnits(bool on)
{
	recordFailingUnits=on;
	if(!on)
		for(int i=0; i<UnitCantWorkReasonSize; ++i)
			unitsFailingByReason[i].clear();
}

void Building::noteUnitFailing(Unit* unit, UnitCantWorkReason reason)
{
	unitsFailingRequirements[reason] += 1;
	// "Not available" is every busy unit of the colony: counted, never marked.
	if(recordFailingUnits && reason!=UnitNotAvailable)
		unitsFailingByReason[reason].push_back(unit->gid);
}

void Building::resetFailureTallies()
{
	for(int i=0; i<UnitCantWorkReasonSize; ++i)
	{
		unitsFailingRequirements[i]=0;
		unitsFailingByReason[i].clear();
	}
}

Building::CandidateEvaluation Building::evaluateHiringCandidate(Unit *unit, int resource,
																bool fresh, bool publishedOnly)
{
	CandidateEvaluation result;
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		result.reason = UnitNotAvailable;
		return result;
	}
	if (!canUnitWorkHere(unit))
	{
		result.reason = UnitTooLowLevel;
		return result;
	}
	const int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungriness;
	int buildingDistance = 0;
	if (!owner->map->buildingDecisionDistance(this, unit->swimClass(), -1, unit->posX, unit->posY,
											  &buildingDistance, fresh, publishedOnly, BuildingRoute::Footprint))
	{
		result.reason = UnitCantAccessBuilding;
		return result;
	}
	if (buildingDistance >= timeLeft)
	{
		result.reason = UnitTooFarFromBuilding;
		return result;
	}
	result.distance = buildingDistance;
	if (resource < 0)
		return result;
	int resourceDistance = 0;
	if (!owner->map->resourceDecisionDistance(owner->teamNumber, resource, unit->swimClass(),
											  unit->posX, unit->posY, &resourceDistance,
											  fresh || publishedOnly, fetchesFromMarkets(), this))
	{
		result.reason = resource < BASIC_COUNT ? UnitCantAccessResource : UnitCantAccessFruit;
		return result;
	}
	if (resourceDistance >= timeLeft)
	{
		result.reason = resource < BASIC_COUNT ? UnitTooFarFromResource : UnitTooFarFromFruit;
		return result;
	}
	int trip = 0;
	if (!owner->map->buildingDecisionDistance(this, unit->swimClass(), resource, unit->posX,
											  unit->posY, &trip, fresh, publishedOnly))
		trip = resourceDistance + std::max(buildingDistance, resourceDistance);
	result.distance = trip << Q8_FIXED_POINT_SHIFT;
	return result;
}
bool Building::considerUnitForBuilding(Unit *unit, int *distance)
{
	const auto result = evaluateHiringCandidate(unit, -1, false);
	if (!result.eligible())
		noteUnitFailing(unit, static_cast<UnitCantWorkReason>(result.reason));
	*distance = result.distance;
	return result.eligible();
}
bool Building::considerUnitForResource(Unit *unit, int resource, int *distance)
{
	const auto result = evaluateHiringCandidate(unit, resource, false);
	if (!result.eligible())
		noteUnitFailing(unit, static_cast<UnitCantWorkReason>(result.reason));
	*distance = result.distance;
	return result.eligible();
}

void Building::fetchApportionment(int targets[MAX_NB_RESOURCES], int served[MAX_NB_RESOURCES]) const
{
	for(int r=0; r<MAX_NB_RESOURCES; ++r)
	{
		int multiplier = type->multiplierResource[r];
		targets[r] = multiplier>0 ? type->maxResource[r]/multiplier : 0;
		served[r] = multiplier>0 ? resources[r]/multiplier : 0;
	}
	for(std::list<Unit *>::const_iterator ui=unitsWorking.begin(); ui!=unitsWorking.end(); ++ui)
	{
		int purpose = (*ui)->destinationPurpose;
		if(purpose>=0 && purpose<MAX_NB_RESOURCES)
			served[purpose]++;
	}
}


bool Building::wantsAnotherDelivery(int r, const int* targets, const int* served)
{
	// neededResource covers the physical room for one more delivery, which the
	// delivery counts round away from when maxResource is not a whole number of
	// deliveries. served covers the units already on their way.
	return neededResource(r)>0 && served[r]<targets[r];
}

Building::HiringDecision Building::evaluateHiring(bool fresh, bool borrowUnused)
{
	HiringDecision decision;
	if (buildingState == DEAD || Sint32(unitsWorking.size()) >= desiredMaxUnitWorking)
		return decision;
	int delivering=0; for(const Unit *unit:unitsWorking) delivering+=unit->activity==Unit::ACT_FILLING;
	if(!borrowUnused && delivering>=workRoleTarget(-1)) return decision;
	int targets[MAX_NB_RESOURCES], served[MAX_NB_RESOURCES];
	fetchApportionment(targets, served);
	auto candidate = [&](Unit *unit, int resource, int requestedResource = -1)
	{
		const auto result = evaluateHiringCandidate(unit, resource, fresh);
		if (fresh)
		{
			const auto live = evaluateHiringCandidate(unit, resource, false, true);
			owner->map->recordHiringCandidate(this, unit, resource >= 0 ? resource : requestedResource, live.reason, result.reason,
											  live.distance, result.distance);
		}
		if (!result.eligible())
			decision.failures.push_back({unit, static_cast<UnitCantWorkReason>(result.reason)});
		return result;
	};
	auto eligible = [&](Unit *unit)
	{
		return unit && unit->performance[HARVEST] &&
			   !(unit->attachedBuilding == this && unit->activity == Unit::ACT_FILLING);
	};
	auto rank = [&](Unit *unit, int r, int value)
	{
		const int level = bringResourcesLevel(unit);
		if (level > decision.level || (level == decision.level && value < decision.score))
		{
			decision.chosen = unit;
			decision.resource = r;
			decision.score = value;
			decision.level = level;
			return true;
		}
		return false;
	};
	for (int n = 0; n < Unit::MAX_COUNT; ++n)
	{
		auto *unit = owner->myUnits[n];
		if (!eligible(unit))
			continue;
		const int r = unit->carriedResource;
		if (r < 0 || !wantsAnotherDelivery(r, targets, served))
			continue;
		const auto result = candidate(unit, -1, r);
		if (!result.eligible())
			continue;
		const int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungriness;
		decision.assignments.push_back({unit, r});
		rank(unit, r, result.distance - (timeLeft >> 1));
	}
	if (!decision.chosen)
	{
		int order[MAX_NB_RESOURCES];
		const int wanted = FetchApportionment::rank(targets, served, MAX_NB_RESOURCES, order);
		for (int i = 0; i < wanted && !decision.chosen; ++i)
		{
			const int r = order[i];
			if (!wantsAnotherDelivery(r, targets, served))
				continue;
			if (!fresh)
				owner->map->advanceHiringGradients(this);
			decision.failures.clear();
			for (int n = 0; n < Unit::MAX_COUNT; ++n)
			{
				auto *unit = owner->myUnits[n];
				if (!eligible(unit))
					continue;
				const auto result = candidate(unit, r);
				if (!result.eligible() || unit->carriedResource == r)
					continue;
				int value = result.distance;
				if (unit->carriedResource >= 0)
					value += CARRIED_RESOURCE_PENALTY_TILES << Q8_FIXED_POINT_SHIFT;
				if (rank(unit, r, value))
					decision.assignments.push_back({unit, r});
			}
		}
	}
	return decision;
}

bool Building::freshHiringEligibility(Unit *unit, int requestedResource)
{
	if (unit->activity == Unit::ACT_FLAG || (requestedResource<0 && runtime->attracts(unit->typeNum)))
		return evaluateFlagCandidate(unit, true).eligible();
	if (!unit->performance[HARVEST])
		return false;
	const int resource = unit->carriedResource == requestedResource ? -1 : requestedResource;
	return evaluateHiringCandidate(unit, resource, true).eligible();
}

int Building::workRoleTarget(int role) const
{
	bool active[NB_UNIT_TYPE+1]{};
	for (int r=0; r<MAX_RESOURCES; ++r)
		active[0] |= resourceDeliveryNeed(r)>0;
	int count=active[0];
	for (int unit=0; unit<NB_UNIT_TYPE; ++unit) count += active[unit+1]=runtime->attracts(unit);
	if (!count || !active[role+1]) return 0;
	int rank=0;
	for (int i=0; i<role+1; ++i) rank+=active[i];
	return desiredMaxUnitWorking/count + (rank < desiredMaxUnitWorking%count);
}

bool Building::subscribeWorkStep()
{
	const bool attracts=runtime->attracts(WORKER) || runtime->attracts(EXPLORER) || runtime->attracts(WARRIOR);
	if (!attracts) return subscribeToBringResourcesStep();
	const bool recruitmentRound=subscriptionWorkingTimer>=32;
	bool hired=subscribeToBringResourcesStep();
	// Fill fair delivery quota before the attraction round can borrow it.
	if (recruitmentRound) while (subscribeToBringResourcesStep()) hired=true;
	hired=subscribeForFlagingStep() || hired;
	// Attraction exhausted its eligible candidates; unused seats may deliver.
	if (recruitmentRound) while (subscribeToBringResourcesStep(true)) hired=true;
	return hired;
}

bool Building::subscribeToBringResourcesStep(bool borrowUnused)
{
	resetFailureTallies();
	if (buildingState == DEAD)
		return false;
	auto *map = owner->map;
	if (map->buildingGradientImpactEnabled())
		map->beginGradientDecision("hiring", gid, -1);
	const auto decision = evaluateHiring(false,borrowUnused);
	if (map->buildingGradientImpactEnabled())
	{
		const auto fresh = evaluateHiring(true,borrowUnused);
		const Unit *observed = decision.chosen ? decision.chosen : fresh.chosen;
		map->recordGradientDecision(this, observed ? observed->swimClass() : 0,
									decision.chosen ? decision.chosen->gid : -1,
									fresh.chosen ? fresh.chosen->gid : -1, decision.resource,
									fresh.resource, decision.score, fresh.score);
	}
	for (const auto &failure : decision.failures)
		noteUnitFailing(failure.first, static_cast<UnitCantWorkReason>(failure.second));
	for (const auto &assignment : decision.assignments)
		assignment.first->destinationPurpose = assignment.second;
	if (decision.chosen)
	{
		unitsWorking.push_back(decision.chosen);
		decision.chosen->subscriptionSuccess(this, false);
		owner->swapTask(decision.chosen);
		map->gradientOutcome("hired", decision.chosen, this, decision.resource);
	}
	updateCallLists();
	return decision.chosen != nullptr;
}

Building::CandidateEvaluation Building::evaluateFlagCandidate(Unit *unit, bool fresh,
															  bool publishedOnly, int terrainDistance)
{
	CandidateEvaluation result;
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		result.reason = UnitNotAvailable;
		return result;
	}
	if (!canUnitWorkHere(unit,true))
	{
		result.reason = UnitTooLowLevel;
		return result;
	}
	const int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungriness;
	if (unit->typeNum==EXPLORER)
	{
		const Map &map = *owner->map;
		if (terrainDistance < 0 && map.hasAirTerrainConstraints())
		{
			field::AirDistanceField route(map.getW(), map.getH(), posX, posY,
				[&map](int x,int y) { return map.terrainPropertiesAt(x,y).flyable; },
				[&map](int x,int y) { return map.terrainRegistry().airCost(map.terrainTypeAt(x,y)); },
				true, field::AirDistanceDirection::ToDestination);
			const auto cost = route.costTo(unit->posX,unit->posY);
			terrainDistance = cost == decltype(route)::unreachable ? INT_MAX : int((cost+GRADIENT_STEP-1)/GRADIENT_STEP);
		}
		if (terrainDistance == INT_MAX)
		{
			result.reason = UnitCantAccessBuilding;
			return result;
		}
		if (terrainDistance >= 0)
		{
			if (terrainDistance > timeLeft) result.reason = UnitTooFarFromBuilding;
			result.distance = int(std::min(std::int64_t(INT_MAX),std::int64_t(terrainDistance)*terrainDistance));
			return result;
		}
		result.distance = owner->map->warpDistSquare(unit->posX, unit->posY, posX, posY);
		if (timeLeft * timeLeft < result.distance)
			result.reason = UnitTooFarFromBuilding;
		return result;
	}
	if (unit->typeNum==WARRIOR && unit->movement == Unit::MOV_ATTACKING_TARGET)
	{
		result.reason = UnitNotAvailable;
		return result;
	}
	if (!owner->map->buildingDecisionDistance(this, unit->swimClass(), -1, unit->posX, unit->posY,
											  &result.distance, fresh, publishedOnly, unit->typeNum==WORKER ? BuildingRoute::Clearing : BuildingRoute::Combat))
	{
		result.reason = UnitCantAccessBuilding;
		return result;
	}
	if (result.distance >= timeLeft)
	{
		result.reason = UnitTooFarFromBuilding;
		return result;
	}
	if (unit->typeNum==WORKER)
	{
		const int state =
			fresh ? owner->map->freshBuildingClearingState(this, unit->swimClass())
				  : anyResourceToClear[owner->map->buildingPipelineEnabled() ? routeAccess(unit->swimClass(),BuildingRoute::Clearing) : int(unit->swimClass()>0)];
		if (state == 2)
			result.reason = UnitCantAccessResource;
	}
	return result;
}

Building::HiringDecision Building::evaluateFlagHiring(bool fresh)
{
 HiringDecision decision;
 const Map &map=*owner->map;
 field::AirDistanceField airRoutes(map.getW(),map.getH(),posX,posY,
  [&map](int x,int y){return map.terrainPropertiesAt(x,y).flyable;},
  [&map](int x,int y){return map.terrainRegistry().airCost(map.terrainTypeAt(x,y));},
  runtime->attracts(EXPLORER) && map.hasAirTerrainConstraints(),field::AirDistanceDirection::ToDestination);
 std::array<Unit*,Unit::MAX_COUNT> possible{};
 std::array<int,Unit::MAX_COUNT> distances{};
 for(int n=0;n<Unit::MAX_COUNT;++n) {
  auto *unit=owner->myUnits[n];
  if(!unit || unit->attachedBuilding==this || !runtime->attracts(unit->typeNum)) continue;
  int terrainDistance=-1;
  if(unit->typeNum==EXPLORER && airRoutes.enabled() && unit->activity==Unit::ACT_RANDOM && unit->medical==Unit::MED_FREE && canUnitWorkHere(unit,true)) {
   const auto cost=airRoutes.costTo(unit->posX,unit->posY);
   terrainDistance=cost==decltype(airRoutes)::unreachable ? INT_MAX : int((cost+GRADIENT_STEP-1)/GRADIENT_STEP);
  }
  const auto result=evaluateFlagCandidate(unit,fresh,false,terrainDistance);
  if(fresh) {
   const auto live=evaluateFlagCandidate(unit,false,true,terrainDistance);
   owner->map->recordHiringCandidate(this,unit,-1,live.reason,result.reason,live.distance,result.distance);
  }
  if(!result.eligible()) { decision.failures.push_back({unit,result.reason}); continue; }
  possible[n]=unit; distances[n]=result.distance;
 }
 int assigned[NB_UNIT_TYPE]{};
 for(const Unit *unit:unitsWorking) if(unit->activity==Unit::ACT_FLAG) ++assigned[unit->typeNum];
 int chosenCount=INT_MAX;
 for(int pass=0;pass<2 && !decision.chosen;++pass) for(int role=0;role<NB_UNIT_TYPE;++role) {
  if(!runtime->attracts(role) || (!pass && assigned[role]>=workRoleTarget(role))) continue;
  Unit *best=nullptr; int bestLevel=role==WARRIOR ? INT_MIN : INT_MAX; Sint64 bestValue=INT64_MAX;
  for(int n=0;n<Unit::MAX_COUNT;++n) {
   Unit *unit=possible[n]; if(!unit || unit->typeNum!=role) continue;
   Sint64 timeLeft=(unit->hungry-(role==WORKER ? unit->trigHungry : 0))/unit->race->hungriness;
   Sint64 hp=(unit->hp*16)/unit->race->unitTypes[0][0].performance[HP];
   if(role==EXPLORER) { timeLeft*=timeLeft; hp*=hp; }
   const Sint64 value=distances[n]-(role==WORKER ? 1 : 2)*(timeLeft+hp);
   const int level=role==WORKER ? unit->workerLevel() : role==EXPLORER ? unit->level[MAGIC_ATTACK_GROUND] : unit->performance[ATTACK_SPEED]*unit->getRealAttackStrength();
   if((role==WARRIOR ? level>bestLevel : level<bestLevel) || (level==bestLevel && value<bestValue)) { best=unit; bestLevel=level; bestValue=value; }
  }
  if(best && assigned[role]<chosenCount) { decision.chosen=best; chosenCount=assigned[role]; decision.score=bestValue; decision.level=bestLevel; }
 }
 return decision;
}

bool Building::subscribeForFlagingStep()
{
	if (buildingState == DEAD)
	{
		resetFailureTallies();
		return false;
	}
	bool hired=false;
	if (++subscriptionWorkingTimer > 32)
	{
		resetFailureTallies();
		while (Sint32(unitsWorking.size()) < desiredMaxUnitWorking)
		{
			resetFailureTallies();
			auto *map = owner->map;
			if (map->buildingGradientImpactEnabled())
				map->beginGradientDecision("hiring", gid, -1);
			const auto decision = evaluateFlagHiring(false);
			if (map->buildingGradientImpactEnabled())
			{
				const auto fresh = evaluateFlagHiring(true);
				const Unit *observed = decision.chosen ? decision.chosen : fresh.chosen;
				const auto route = observed && observed->typeNum == WORKER
					? BuildingRoute::Clearing : BuildingRoute::Combat;
				map->recordGradientDecision(
					this, observed ? observed->swimClass() : 0,
					decision.chosen ? decision.chosen->gid : -1,
					fresh.chosen ? fresh.chosen->gid : -1, -1, -1, decision.score, fresh.score, 0, route);
			}
			for (const auto &failure : decision.failures)
				noteUnitFailing(failure.first, static_cast<UnitCantWorkReason>(failure.second));
			if (!decision.chosen)
				break;
			unitsWorking.push_back(decision.chosen);
			decision.chosen->subscriptionSuccess(this, false, true);
			map->gradientOutcome("hired", decision.chosen, this, -1);
			hired = true;
		}
		updateCallLists();
		subscriptionWorkingTimer = 0;
	}
	return hired;
}


void Building::subscribeUnitForInside(Unit* unit)
{
	if (!canOfferService(unit, unit->destinationPurpose)) { unit->standardRandomActivity(); return; }
	reserveService(unit);
	unitsInside.push_back(unit);
	unit->subscriptionSuccess(this, true);
	updateCallLists();
}

