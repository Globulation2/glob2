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

bool Building::considerUnitForBuilding(Unit* unit, int* distBuilding)
{
	if(unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		noteUnitFailing(unit, UnitNotAvailable);
		return false;
	}
	if(!canUnitWorkHere(unit))
	{
		noteUnitFailing(unit, UnitTooLowLevel);
		return false;
	}

	int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungriness;
	if(!owner->map->buildingAvailable(this, unit->swimClass(), unit->posX, unit->posY, distBuilding, BuildingRoute::Footprint))
	{
		noteUnitFailing(unit, UnitCantAccessBuilding);
		return false;
	}
	if(*distBuilding >= timeLeft)
	{
		noteUnitFailing(unit, UnitTooFarFromBuilding);
		return false;
	}
	return true;
}


bool Building::considerUnitForResource(Unit* unit, int wantedResource, int* dist)
{
	int distBuilding=0;
	if(!considerUnitForBuilding(unit, &distBuilding))
		return false;

	int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungriness;
	int distResource = 0;
	if(!owner->map->resourceAvailable(owner->teamNumber, wantedResource, unit->swimClass(),
	                                  unit->posX, unit->posY, &distResource, fetchesFromMarkets()))
	{
		if(wantedResource<BASIC_COUNT)
			noteUnitFailing(unit, UnitCantAccessResource);
		else
			noteUnitFailing(unit, UnitCantAccessFruit);
		return false;
	}
	if(distResource >= timeLeft)
	{
		if(wantedResource<BASIC_COUNT)
			noteUnitFailing(unit, UnitTooFarFromResource);
		else
			noteUnitFailing(unit, UnitTooFarFromFruit);
		return false;
	}

	// Score by the whole job: the round-trip field when a fetcher has already
	// built one. Without one, estimate the carry leg rather than reach for the
	// building distance alone: a unit standing at the building carries as far
	// as it walked out, and one standing at the resource carries the building
	// distance. Building a field here instead would cost one per resource of
	// every hiring building, nearly all of them never fetched.
	int roundTrip = 0;
	if(!owner->map->roundTripDistance(this, wantedResource, unit->swimClass(), unit->posX, unit->posY, &roundTrip))
		roundTrip = distResource + std::max(distBuilding, distResource);
	*dist = roundTrip<<Q8_FIXED_POINT_SHIFT;
	return true;
}

int Building::gatherBringResourcesCandidates(BringResourcesCandidate* candidates, int wantedResource)
{
	owner->map->advanceHiringGradients(this);
	// The tallies count units, and the same unit is offered every resource the
	// building tries to staff, so start each scan from zero: what the info panel
	// ends up showing is one coherent pass, for the last resource attempted.
	resetFailureTallies();

	int count=0;
	for(int n=0; n<Unit::MAX_COUNT; ++n)
	{
		Unit* unit=owner->myUnits[n];
		if(!unit)
			continue;
		if(!unit->performance[HARVEST])
			continue;
		if(unit->attachedBuilding == this && unit->activity == Unit::ACT_FILLING)
			continue;

		int dist;
		if(considerUnitForResource(unit, wantedResource, &dist))
		{
			candidates[count].unit = unit;
			candidates[count++].distance = dist;
		}
	}
	return count;
}


void Building::fetchApportionment(int targets[MAX_NB_RESOURCES], int served[MAX_NB_RESOURCES]) const
{
	for(int r=0; r<MAX_NB_RESOURCES; ++r)
	{
		int multiplier = type->multiplierResource[r];
		targets[r] = (resourceDeliveryTarget(r)+multiplier-1)/multiplier;
		const int missing=resourceDeliveryNeed(r);
		served[r] = targets[r]-(missing+multiplier-1)/multiplier;
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

void Building::selectUnitCarryingWantedResource(const int* targets, const int* served, BringResourcesSelection& sel)
{
	for(int n=0; n<Unit::MAX_COUNT; ++n)
	{
		Unit* unit=owner->myUnits[n];
		if(!unit)
			continue;
		if(!unit->performance[HARVEST])
			continue;
		if(unit->attachedBuilding == this && unit->activity == Unit::ACT_FILLING)
			continue;

		int r=unit->carriedResource;
		if(r<0 || !wantsAnotherDelivery(r, targets, served))
			continue;
		int distBuilding;
		if(!considerUnitForBuilding(unit, &distBuilding))
			continue;

		int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungriness;
		int value=distBuilding-(timeLeft>>1);
		int level = bringResourcesLevel(unit);
		// Every carrying candidate has its destinationPurpose set to the
		// resource it carries, not only the one finally chosen.
		unit->destinationPurpose=r;
		if ((level>sel.maxLevel) || (level==sel.maxLevel && value<sel.minValue))
		{
			sel.minValue=value;
			sel.maxLevel=level;
			sel.choosen=unit;
		}
	}
}

void Building::selectFetcher(const BringResourcesCandidate* candidates, int count, int wantedResource, BringResourcesSelection& sel)
{
	for(int n=0; n<count; ++n)
	{
		Unit* unit=candidates[n].unit;

		// A unit already carrying what is wanted is a delivery, not a fetch, and
		// selectUnitCarryingWantedResource has first refusal on it.
		int carried=unit->carriedResource;
		if(carried==wantedResource)
			continue;

		int value=candidates[n].distance;
		if(carried>=0)
			value += CARRIED_RESOURCE_PENALTY_TILES<<Q8_FIXED_POINT_SHIFT;
		int level = bringResourcesLevel(unit);
		if ((level>sel.maxLevel) || (level==sel.maxLevel && value<sel.minValue))
		{
			sel.minValue=value;
			sel.maxLevel=level;
			sel.choosen=unit;
			unit->destinationPurpose=wantedResource;
		}
	}
}


int Building::workRoleTarget(int role) const
{
	bool active[NB_UNIT_TYPE+1]{};
	for (int r=0; r<MAX_RESOURCES; ++r)
		active[0] |= resourceDeliveryNeed(r)>0;
	int count=active[0];
	for (int unit=0; unit<NB_UNIT_TYPE; ++unit) count += active[unit+1]=type->zonable[unit];
	if (!count || !active[role+1]) return 0;
	int rank=0;
	for (int i=0; i<role+1; ++i) rank+=active[i];
	return desiredMaxUnitWorking/count + (rank < desiredMaxUnitWorking%count);
}

bool Building::subscribeWorkStep()
{
	const bool attracts=type->zonable[WORKER] || type->zonable[EXPLORER] || type->zonable[WARRIOR];
	if (!attracts) return subscribeToBringResourcesStep();
	bool hired=subscribeToBringResourcesStep();
	return subscribeForFlagingStep() || hired;
}

bool Building::subscribeToBringResourcesStep()
{
	resetFailureTallies();
	if (buildingState==DEAD)
		return false;
	if (verbose)
		printf("bgid=%d, subscribeToBringResourcesStep()...\n", gid);

	bool hired=false;
	int delivering=0;
	for (const Unit* unit : unitsWorking) delivering += unit->activity == Unit::ACT_FILLING;
	if ((Sint32)unitsWorking.size()<desiredMaxUnitWorking && delivering<workRoleTarget(-1))
	{
		int targets[MAX_NB_RESOURCES];
		int served[MAX_NB_RESOURCES];
		fetchApportionment(targets, served);

		BringResourcesSelection sel;
		sel.maxLevel = -1;
		sel.minValue = INT_MAX;
		sel.choosen = NULL;

		// A unit already holding something we want delivers without a fetch trip,
		// so it is taken ahead of the apportionment, which only directs the units
		// we still have to send out. It is subscription-aware in its own right, so
		// it cannot oversubscribe a resource either.
		selectUnitCarryingWantedResource(targets, served, sel);

		// Otherwise staff the resource whose subscriptions sit furthest below its
		// share of the building's targets, falling to the next one whenever no
		// unit can actually be hired for it.
		if (sel.choosen==NULL)
		{
			int order[MAX_NB_RESOURCES];
			int wanted = FetchApportionment::rank(targets, served, MAX_NB_RESOURCES, order);
			for(int i=0; i<wanted && sel.choosen==NULL; ++i)
			{
				int r = order[i];
				if(!wantsAnotherDelivery(r, targets, served))
					continue;
				BringResourcesCandidate candidates[Unit::MAX_COUNT];
				const int count=gatherBringResourcesCandidates(candidates, r);
				selectFetcher(candidates, count, r, sel);
			}
		}

		if (sel.choosen)
		{
			unitsWorking.push_back(sel.choosen);
			sel.choosen->subscriptionSuccess(this, false);
			owner->swapTask(sel.choosen);
			hired=true;
		}
	}

	updateCallLists();

	if (verbose)
		printf(" ...done\n");
	return hired;
}

bool Building::considerUnitForExplorerFlag(Unit* unit, int* dist, int terrainDistance)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		noteUnitFailing(unit, UnitNotAvailable);
		return false;
	}
	if (!canUnitWorkHere(unit, true))
	{
		noteUnitFailing(unit, UnitTooLowLevel);
		return false;
	}
	int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungriness;
	if (terrainDistance == INT_MAX)
	{
		noteUnitFailing(unit, UnitCantAccessBuilding);
		return false;
	}
	if (terrainDistance >= 0)
	{
		if (terrainDistance > timeLeft)
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
	int timeLeftSquared = timeLeft * timeLeft;
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
	if (!canUnitWorkHere(unit, true))
	{
		noteUnitFailing(unit, UnitTooLowLevel);
		return false;
	}
	int distBuilding = 0;
	// timeLeft and distBuilding are both linear (in ticks-remaining and
	// linear gradient steps respectively); compare as-is. The corresponding
	// check in subscribeToBringResourcesStep uses the same pairing.
	int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungriness;
	bool canSwim = unit->performance[SWIM];
	if (!owner->map->buildingAvailable(this, unit->swimClass(), unit->posX, unit->posY, &distBuilding, BuildingRoute::Clearing))
	{
		noteUnitFailing(unit, UnitCantAccessBuilding);
		return false;
	}
	if (distBuilding >= timeLeft)
	{
		noteUnitFailing(unit, UnitTooFarFromBuilding);
		return false;
	}
	if (anyResourceToClear[canSwim] == 2)
	{
		noteUnitFailing(unit, UnitCantAccessResource);
		return false;
	}
	*dist = distBuilding;
	return true;
}

bool Building::considerUnitForWarriorFlag(Unit* unit, int* dist)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		noteUnitFailing(unit, UnitNotAvailable);
		return false;
	}
	if (!canUnitWorkHere(unit, true))
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
	// check in subscribeToBringResourcesStep uses the same pairing.
	int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungriness;
	if (!owner->map->buildingAvailable(this, unit->swimClass(), unit->posX, unit->posY, &distBuilding, BuildingRoute::Combat))
	{
		noteUnitFailing(unit, UnitCantAccessBuilding);
		return false;
	}
	if (distBuilding >= timeLeft)
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
		// One reverse search serves every explorer candidate and all hiring
		// iterations. Ignore temporary flyer occupancy, as building selection
		// does; individual steering resolves it. Uniform maps allocate nothing.
		const Map& map = *owner->map;
		field::AirDistanceField airRoutes(map.getW(),map.getH(),posX,posY,
			[&map](int x,int y) { return map.terrainPropertiesAt(x,y).flyable; },
			[&map](int x,int y) { return gradient_kernel::scaledTerrainStep(GRADIENT_STEP,map.terrainPropertiesAt(x,y).airSpeedQ8); },
			type->zonable[EXPLORER] && Sint32(unitsWorking.size())<desiredMaxUnitWorking && map.hasAirTerrainConstraints(),
			field::AirDistanceDirection::ToDestination);
		while (((Sint32)unitsWorking.size()<desiredMaxUnitWorking))
		{
			// Per-iteration reset: the same Unit::MAX_COUNT array is rescanned
			// each iteration (already-hired units are filtered via
			// attachedBuilding==this); without this, the same failing units
			// would be counted N times across N iterations.
			resetFailureTallies();

			//Generate the list of possible units
			Unit* possibleUnits[Unit::MAX_COUNT];
			int distances[Unit::MAX_COUNT];
			for(int n=0; n<Unit::MAX_COUNT; ++n)
			{
				possibleUnits[n]=NULL;
				distances[n] = 0;
				Unit* unit=owner->myUnits[n];
				if(!unit)
					continue;
				if(unit->attachedBuilding == this)
					continue;
				if(unit->typeNum == EXPLORER && type->zonable[EXPLORER])
				{
					if(unit->typeNum != EXPLORER)
						continue;
					int travelDistance = -1;
					if (airRoutes.enabled() && unit->activity==Unit::ACT_RANDOM && unit->medical==Unit::MED_FREE && canUnitWorkHere(unit, true))
					{
						const unsigned cost=airRoutes.costTo(unit->posX,unit->posY);
						travelDistance = cost==decltype(airRoutes)::unreachable ? INT_MAX : int((cost+GRADIENT_STEP-1)/GRADIENT_STEP);
					}
					if(considerUnitForExplorerFlag(unit, &distances[n],travelDistance))
						possibleUnits[n]=unit;
				}
				else if(unit->typeNum == WORKER && type->zonable[WORKER])
				{
					if(unit->typeNum != WORKER)
						continue;
					if(considerUnitForWorkerFlag(unit, &distances[n]))
						possibleUnits[n]=unit;
				}
				else if(unit->typeNum == WARRIOR && type->zonable[WARRIOR])
				{
					if(unit->typeNum != WARRIOR)
						continue;
					if(considerUnitForWarriorFlag(unit, &distances[n]))
						possibleUnits[n]=unit;
				}
			}

			int assigned[NB_UNIT_TYPE]{};
			for (const Unit* unit : unitsWorking)
				if (unit->activity == Unit::ACT_FLAG) ++assigned[unit->typeNum];
			Unit* choosen=nullptr;
			int chosenCount=INT_MAX;
			// Choose the least staffed eligible attraction role, then use that
			// role's established ranking among its candidate units.
			for (int role=0; role<NB_UNIT_TYPE; ++role)
			{
				if (!type->zonable[role] || assigned[role] >= workRoleTarget(role)) continue;
				Unit* best=nullptr;
				int bestLevel=role == WARRIOR ? INT_MIN : INT_MAX;
				Sint64 bestValue=INT64_MAX;
				for (int n=0; n<Unit::MAX_COUNT; ++n)
				{
					Unit* unit=possibleUnits[n];
					if (!unit || unit->typeNum != role) continue;
					Sint64 timeLeft=(unit->hungry-(role == WORKER ? unit->trigHungry : 0))/unit->race->hungriness;
					Sint64 hp=(unit->hp*16)/unit->race->unitTypes[0][0].performance[HP];
					if (role == EXPLORER) { timeLeft*=timeLeft; hp*=hp; }
					const Sint64 value=distances[n]-(role == WORKER ? 1 : 2)*(timeLeft+hp);
					const int level=role == WORKER ? unit->workerLevel() : role == EXPLORER ? unit->level[MAGIC_ATTACK_GROUND] : unit->performance[ATTACK_SPEED]*unit->getRealAttackStrength();
					if ((role == WARRIOR ? level>bestLevel : level<bestLevel) || (level==bestLevel && value<bestValue))
					{ best=unit; bestLevel=level; bestValue=value; }
				}
				if (best && assigned[role]<chosenCount)
				{ choosen=best; chosenCount=assigned[role]; }
			}

			if (choosen)
			{
				unitsWorking.push_back(choosen);
				choosen->subscriptionSuccess(this, false, true);
				hired=true;
			}
			else
				break;
		}

		updateCallLists();

		subscriptionWorkingTimer=0;
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


