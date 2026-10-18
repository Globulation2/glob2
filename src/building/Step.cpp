// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Material.h"
#include <list>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <bit>
#include <climits>
#include <optional>

#include "Building.h"
#include "MaterialDemand.h"
#include "FetchApportionment.h"
#include "HiringPlanner.h"
#include "BuildingType.h"
#include "FixedPoint.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Order.h"
#include "field/TerrainMovementCosts.h"

void Building::step(void)
{
	if (type->isBuildingSite && (siteCompletionPending || type->useTeamMaterials)) updateBuildingSite();
	{
		// Site completion above can change the type and stock. Reuse demand only
		// for this operation, before the later production and worker updates.
		const auto demand = materialDemand();
		computeWishedMaterials(wishedMaterials, demand);
		if (((owner->game->stepCounter + gid) & 255) == 0)
			freeIdleGradients();
		updateCallLists(demand);
	}
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
	// Unavailable suggestions are counted only when admission examines them.
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

namespace
{
// Healthy stock hunger deltas fit in 32 bits. Preserve widened subtraction and
// exact signed division for extreme cached or authored values.
int remainingFoodTime(const Unit& unit)
{
    if (unit.hungriness<=0) return INT_MAX;
    const Sint64 delta=Sint64(unit.hungry)-unit.trigHungry;
    if (delta>=INT_MIN && delta<=INT_MAX)
        return int(delta)/unit.hungriness;
    return int(std::clamp<Sint64>(delta/unit.hungriness,INT_MIN,INT_MAX));
}

// Return the already-computed food budget to the material and carrying scans.
// Keep the original member entry point below for existing callers and fixtures.
bool considerBuildingCandidate(Building& building, Unit* unit, int* distBuilding,
                               int airDistance, int& timeLeft)
{
	if(unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		building.noteUnitFailing(unit, Building::UnitNotAvailable);
		return false;
	}
	if(!building.canUnitWorkHere(unit))
	{
		building.noteUnitFailing(unit, Building::UnitTooLowLevel);
		return false;
	}

	timeLeft=remainingFoodTime(*unit);
    bool accessible=true;
    Map* map=building.owner->map;
    if (unit->performance[FLY] && !map->hasAirRoutingConstraints()) {
        *distBuilding=airDistance>=0 ? airDistance : map->warpDistMax(unit->posX,unit->posY,building.posX,building.posY);
        accessible=*distBuilding!=INT_MAX;
    } else {
        const auto route=map->queryBuildingDistance(&building,unit->movementClass(),unit->posX,unit->posY,distBuilding,BuildingRoute::Footprint);
        if (route==RouteResult::Pending) return false;
        accessible=route==RouteResult::Reachable;
    }
    if(!accessible)
	{
		building.noteUnitFailing(unit, Building::UnitCantAccessBuilding);
		return false;
	}
	if((!unit->performance[FLY] && !unit->performance[WALK] && !unit->performance[SWIM] && *distBuilding>1) || *distBuilding >= timeLeft)
	{
		building.noteUnitFailing(unit, Building::UnitTooFarFromBuilding);
		return false;
	}
	return true;
}

}

bool Building::considerUnitForBuilding(Unit* unit, int* distBuilding, int airDistance)
{
    int timeLeft;
    return considerBuildingCandidate(*this,unit,distBuilding,airDistance,timeLeft);
}

bool Building::considerUnitForMaterial(Unit* unit, int wantedMaterial, int* dist, int airDistance)
{
	if (unit->hasCapability(UnitRuntimeTraits::ExtendedCargo)
		&& !unit->hasDeliverableCargo(*this,wantedMaterial) && !unit->canCarryMaterial(wantedMaterial))
		return false;
	int distBuilding=0, timeLeft;
	if(!considerBuildingCandidate(*this,unit,&distBuilding,airDistance,timeLeft))
		return false;

	int distMaterial = 0;
	const auto materialRoute=unit->queryMaterialDestination(wantedMaterial,nullptr,nullptr,&distMaterial,fetchesFromMarkets(),this);
	if (materialRoute==RouteResult::Pending) return false;
	if (materialRoute!=RouteResult::Reachable)

	{
		if(wantedMaterial<HAPPINESS_BASE || wantedMaterial>=HAPPINESS_BASE+HAPPINESS_COUNT)
			noteUnitFailing(unit, UnitCantAccessResource);
		else
			noteUnitFailing(unit, UnitCantAccessFruit);
		return false;
	}
	if((!unit->performance[FLY] && !unit->performance[WALK] && !unit->performance[SWIM] && distMaterial>1) || distMaterial >= timeLeft)
	{
		if(wantedMaterial<HAPPINESS_BASE || wantedMaterial>=HAPPINESS_BASE+HAPPINESS_COUNT)
			noteUnitFailing(unit, UnitTooFarFromResource);
		else
			noteUnitFailing(unit, UnitTooFarFromFruit);
		return false;
	}

	// Score by the whole job, estimating the carry leg rather than reaching
	// for the building distance alone: a unit standing at the building carries
	// as far as it walked out, and one standing at the resource carries the
	// building distance.
	const int wholeTrip = distMaterial + std::max(distBuilding, distMaterial);
	*dist = wholeTrip<<Q8_FIXED_POINT_SHIFT;
	return true;
}

namespace
{
BuildingMaterialDemand collectApportionment(const Building& building, int* targets, int* served)
{
	BuildingMaterialDemand demand;
	std::fill_n(targets, MaterialSlotCount, 0);
	std::fill_n(served, MaterialSlotCount, 0);
	// Sites can have live budgets in any slot, including reserved slots.
	unsigned candidates = building.type->isBuildingSite ? (1u<<MaterialSlotCount)-1 : building.runtime->replenishMaterialMask;
	while (candidates)
	{
		const unsigned r = std::countr_zero(candidates);
		candidates &= candidates-1;
		const int multiplier = building.type->materialMultiplier[r];
		targets[r] = (building.materialDeliveryTarget(r)+multiplier-1)/multiplier;
		const int deliveries = (building.materialDeliveryNeed(r)+multiplier-1)/multiplier;
		served[r] = targets[r]-deliveries;
		if (deliveries)
		{
			demand.full = false;
			demand.deliveries[r] = deliveries;
			if (r < MaterialCount) demand.totalDeliveries += deliveries;
		}
	}
	for (const Unit* unit : building.unitsWorking)
	{
		const int purpose = unit->destinationPurpose;
		if (purpose>=0 && purpose<MaterialSlotCount)
			served[purpose]++;
	}
	return demand;
}

std::array<unsigned, 4> workRoleTargets(const Building& building)
{
	bool delivery = false;
	for (int material = 0; material < MaterialCount; ++material)
		if (building.materialDeliveryNeed(material) > 0) { delivery = true; break; }
	return Hiring::roleTargets(
		unsigned(std::max(0, building.desiredMaxUnitWorking)), delivery,
		{building.runtime->attractsRole(0), building.runtime->attractsRole(1), building.runtime->attractsRole(2)});
}
}

void Building::fetchApportionment(int targets[MaterialSlotCount], int served[MaterialSlotCount]) const
{
	collectApportionment(*this, targets, served);
}

bool Building::wantsAnotherDelivery(int r, const int* targets, const int* served)
{
	// neededMaterial covers the physical room for one more delivery, which the
	// delivery counts round away from when maxMaterials is not a whole number of
	// deliveries. served covers the units already on their way.
	return neededMaterial(r)>0 && served[r]<targets[r];
}

int Building::workRoleTarget(int role) const
{
	return workRoleTargets(*this)[role + 1];
}

bool Building::subscribeWorkStep()
{
	const bool attracts=runtime->attractionRoles!=0;
	if (!attracts) return subscribeToBringMaterialsStep();
	const bool recruitmentRound=subscriptionWorkingTimer>=32;
	bool hired=subscribeToBringMaterialsStep();
	// Fill fair delivery quota before the attraction round can borrow it.
	if (recruitmentRound) while (subscribeToBringMaterialsStep()) hired=true;
	hired=subscribeForFlagingStep() || hired;
	// Attraction exhausted its eligible candidates; unused seats may deliver.
	if (recruitmentRound && !subscriptionWorkingTimer) while (subscribeToBringMaterialsStep(true)) hired=true;
	return hired;
}

bool Building::subscribeToBringMaterialsStep(bool borrowUnused)
{
	resetFailureTallies();
	if (buildingState == DEAD)
		return false;
	if (verbose)
		printf("bgid=%d, subscribeToBringMaterialsStep()...\n", gid);

	bool hired = false;
	std::optional<BuildingMaterialDemand> demand;
	int delivering = 0;
	for (const Unit *unit : unitsWorking)
		delivering += unit->activity == Unit::ACT_FILLING;
	if ((Sint32)unitsWorking.size() < desiredMaxUnitWorking &&
		(borrowUnused || delivering < workRoleTarget(-1)))
	{
		int targets[MaterialSlotCount];
		int served[MaterialSlotCount];
		demand = collectApportionment(*this, targets, served);

		hired = hireMaterialUnit(targets, served);
	}

	// Failed admission only queries routes and records failure tallies. A hire
	// can change worker assignments, so discard this operation's demand then.
	if (!hired && demand) updateCallLists(*demand);
	else updateCallLists();

	if (verbose)
		printf(" ...done\n");
	return hired;
}

// Lists are suggestions, not reservations. Every attempt restarts at the
// front; temporary unavailability never permanently consumes a candidate.
bool Building::hireMaterialUnit(const int *targets, const int *served)
{
	const auto *lists = owner->game->hiringCandidates(*this);
	if (!lists)
		return false;

	const auto choose = [&](Hiring::Job job, int material) -> Unit *
	{
		if (!wantsAnotherDelivery(material, targets, served))
			return nullptr;
		for (const auto &list : lists->jobs)
			if (list.key == Hiring::Key{job, material})
				for (const auto &suggestion : list.candidates)
				{
					auto *u = owner->game->resolveUnit(suggestion.unit);
					if (!u || u->isDead || u->owner != owner ||
						!u->hasCapability(UnitRuntimeTraits::Transport) || !u->performance[BUILD] ||
						u->activity != Unit::ACT_RANDOM || u->medical != Unit::MED_FREE)
						continue;
					int airDistance = -1;

					int distance = 0;
					if (job == Hiring::Job::Delivery)
					{
						if (!u->hasDeliverableCargo(*this, material) ||
							!considerUnitForBuilding(u, &distance, airDistance))
							continue;
					}
					else
					{
						if (!u->performance[HARVEST] || u->hasCarriedMaterial(material) ||
							!considerUnitForMaterial(u, material, &distance, airDistance))
							continue;
						Sint32 x, y;
						if (u->queryMaterialDestination(material, &x, &y, nullptr,
                                                        fetchesFromMarkets(), this)!=RouteResult::Reachable)
							continue;
					}
					u->destinationPurpose = material;
					return u;
				}
		return nullptr;
	};
	Unit *selected = nullptr;
	for (int material = 0; material < MaterialCount && !selected; ++material)
		selected = choose(Hiring::Job::Delivery, material);
	if (!selected)
	{
		int order[MaterialSlotCount];
		const int count = FetchApportionment::rank(targets, served, MaterialSlotCount, order);
		for (int i = 0; i < count && !selected; ++i)
			selected = choose(Hiring::Job::Fetch, order[i]);
	}
	if (!selected)
		return false;
	unitsWorking.push_back(selected);
	selected->subscriptionSuccess(this, false);
	owner->swapTask(selected);
	return true;
}

bool Building::considerUnitForExplorerFlag(Unit* unit, int* dist, int terrainDistance)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		noteUnitFailing(unit, UnitNotAvailable);
		return false;
	}
	if (!runtime->interaction(unit->typeNum).recruits(1) || !canUnitWorkHere(unit,true,1))
	{
		noteUnitFailing(unit, UnitTooLowLevel);
		return false;
	}
	int timeLeft = remainingFoodTime(*unit);
    if (!unit->performance[FLY] || owner->map->hasAirRoutingConstraints())
    {
        int distance=0;
        const auto route=owner->map->queryBuildingDistance(this,unit->movementClass(),unit->posX,unit->posY,&distance,BuildingRoute::Combat);
        if (route==RouteResult::Pending) return false;
        if (route!=RouteResult::Reachable) {
            noteUnitFailing(unit,UnitCantAccessBuilding);return false;
        }
        terrainDistance=distance;
    }

	if (terrainDistance == INT_MAX)
	{
		noteUnitFailing(unit, UnitCantAccessBuilding);
		return false;
	}
	if (terrainDistance >= 0)
	{
		if ((!unit->performance[FLY] && !unit->performance[WALK] && !unit->performance[SWIM] && terrainDistance>0) || terrainDistance > timeLeft)
		{
			noteUnitFailing(unit, UnitTooFarFromBuilding);
			return false;
		}
		*dist = int(std::min(std::int64_t(INT_MAX),std::int64_t(terrainDistance)*terrainDistance));
		return true;
	}
	// warpDistSquare returns squared Euclidean distance, so timeLeft is
	// squared here to keep the comparison in the same units. Worker/warrior
	// flags compare against Map::buildingAvailable (linear gradient
	// distance) and must NOT square — see considerUnitForWorkerFlag.
	const Sint64 timeLeftSquared = Sint64(timeLeft)*timeLeft;
	int directdist = owner->map->warpDistSquare(unit->posX, unit->posY, posX, posY);
	if (timeLeftSquared < directdist)
	{
		noteUnitFailing(unit, UnitTooFarFromBuilding);
		return false;
	}
	*dist = directdist;
	return true;
}

bool Building::considerUnitForWorkerFlag(Unit* unit, int* dist)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		noteUnitFailing(unit, UnitNotAvailable);
		return false;
	}
	if (!runtime->interaction(unit->typeNum).recruits(0) || !canUnitWorkHere(unit,true,0))
	{
		noteUnitFailing(unit, UnitTooLowLevel);
		return false;
	}


	int distBuilding = 0;
	// timeLeft and distBuilding are both linear (in ticks-remaining and
	// linear gradient steps respectively); compare as-is. The corresponding
	// check in subscribeToBringMaterialsStep uses the same pairing.
	int timeLeft = remainingFoodTime(*unit);
	bool canSwim = unit->performance[SWIM];
	const auto route=owner->map->queryBuildingDistance(this, unit->movementClass(), unit->posX, unit->posY, &distBuilding, BuildingRoute::Clearing);
	if (route==RouteResult::Pending) return false;
	if (route!=RouteResult::Reachable)
	{
		noteUnitFailing(unit, UnitCantAccessBuilding);
		return false;
	}
	if ((!unit->performance[FLY] && !unit->performance[WALK] && !unit->performance[SWIM] && distBuilding>0) || distBuilding >= timeLeft)
	{
		noteUnitFailing(unit, UnitTooFarFromBuilding);
		return false;
	}
	if (anyResourceToClear[swimAccessVariant(unit->movementClass())] == 2)
	{
		noteUnitFailing(unit, UnitCantAccessResource);
		return false;
	}
	*dist = distBuilding;
	return true;
}

bool Building::considerUnitForWarriorFlag(Unit* unit, int* dist, int terrainDistance)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		noteUnitFailing(unit, UnitNotAvailable);
		return false;
	}
	if (!runtime->interaction(unit->typeNum).recruits(2) || !canUnitWorkHere(unit,true,2))
	{
		noteUnitFailing(unit, UnitTooLowLevel);
		return false;
	}
	if (unit->movement == Unit::MOV_ATTACKING_TARGET)
	{
		noteUnitFailing(unit, UnitNotAvailable);
		return false;
	}
	int distBuilding = 0;
	// timeLeft and distBuilding are both linear (in ticks-remaining and
	// linear gradient steps respectively); compare as-is. The corresponding
	// check in subscribeToBringMaterialsStep uses the same pairing.
	int timeLeft = remainingFoodTime(*unit);
	if (unit->performance[FLY] && !owner->map->hasAirRoutingConstraints()) distBuilding=terrainDistance>=0 ? terrainDistance : owner->map->warpDistMax(unit->posX,unit->posY,posX,posY);
    else {
        const auto route=owner->map->queryBuildingDistance(this,unit->movementClass(),unit->posX,unit->posY,&distBuilding,BuildingRoute::Combat);
        if (route==RouteResult::Pending) return false;
        if (route!=RouteResult::Reachable) { noteUnitFailing(unit,UnitCantAccessBuilding); return false; }
    }
	if ((!unit->performance[FLY] && !unit->performance[WALK] && !unit->performance[SWIM] && distBuilding>0) || distBuilding >= timeLeft)
	{
		noteUnitFailing(unit, UnitTooFarFromBuilding);
		return false;
	}
	*dist = distBuilding;
	return true;
}

bool Building::subscribeForFlagingStep()
{
	if (buildingState==DEAD)
	{
		resetFailureTallies();
		return false;
	}

	bool hired=false;
	subscriptionWorkingTimer++;
	if (subscriptionWorkingTimer>32)
	{
		// Reset stale failure counts for the case where the while loop below
		// doesn't run (building already fully staffed). When the loop does run,
		// this is overwritten by the per-iteration reset on iteration 1.
		resetFailureTallies();
		if (Sint32(unitsWorking.size()) < desiredMaxUnitWorking)
			hired = hireFlagUnits();
		// Suggestions may be missing or stale, so an owed round retries briefly
		// rather than waiting another full clock cycle.
		bool owed=false;
		const auto quotas = workRoleTargets(*this);
		for (int role=0; role<3; ++role)
		{
			int assigned=0;
			for (const auto* unit : unitsWorking)
				assigned += unit->activity==Unit::ACT_FLAG && int(unit->jobPurpose)==int(UnitJobPurpose::Clear)+role;
			owed |= assigned<int(quotas[role + 1]);
		}
		if (!owed || subscriptionWorkingTimer>32+int(Hiring::RecruitmentRetryTicks))
			subscriptionWorkingTimer=0;
		updateCallLists();
	}
	return hired;
}

bool Building::hireFlagUnits()
{
	const auto *lists = owner->game->hiringCandidates(*this);
	if (!lists)
		return false;

	bool hired = false;
	while (Sint32(unitsWorking.size()) < desiredMaxUnitWorking)
	{
		resetFailureTallies();
		int assigned[3]{};
		for (const auto *u : unitsWorking)
			if (u->activity == Unit::ACT_FLAG)
			{
				const int role = int(u->jobPurpose) - int(UnitJobPurpose::Clear);
				if (role >= 0 && role < 3)
					++assigned[role];
			}
		// Recompute after each successful hire, before the next candidate scan.
		const auto quotas = workRoleTargets(*this);
		Unit *selected = nullptr;
		int selectedRole = -1, count = INT_MAX;
		for (int pass = 0; pass < 2 && !selected; ++pass)
			for (int role = 0; role < 3; ++role)
			{
				if (!runtime->attractsRole(role) ||
					(!pass && assigned[role] >= int(quotas[role + 1])))
					continue;
				Unit *candidate = nullptr;
				for (const auto &list : lists->jobs)
					if (list.key.job == Hiring::Job(unsigned(Hiring::Job::Clear) + role))
						for (const auto &entry : list.candidates)
						{
							auto *u = owner->game->resolveUnit(entry.unit);
							if (!u || u->isDead || u->owner != owner || u->attachedBuilding == this)
								continue;
							int travel = -1, distance = 0;

							const bool eligible =
								role == 0   ? considerUnitForWorkerFlag(u, &distance)
								: role == 1 ? considerUnitForExplorerFlag(u, &distance, travel)
											: considerUnitForWarriorFlag(u, &distance, travel);
							if (eligible)
							{
								candidate = u;
								break;
							}
						}
				if (candidate && assigned[role] < count)
				{
					selected = candidate;
					selectedRole = role;
					count = assigned[role];
				}
			}
		if (!selected)
		{
			break;
		}
		unitsWorking.push_back(selected);
		selected->subscriptionSuccess(this, false, true,
									  UnitJobPurpose(int(UnitJobPurpose::Clear) + selectedRole));
		hired = true;
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
