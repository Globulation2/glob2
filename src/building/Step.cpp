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


bool Building::considerUnitForBuilding(Unit* unit, int* distBuilding)
{
	if(unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		unitsFailingRequirements[UnitNotAvailable] += 1;
		return false;
	}
	if(!canUnitWorkHere(unit))
	{
		unitsFailingRequirements[UnitTooLowLevel] += 1;
		return false;
	}

	int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungriness;
	if(!owner->map->buildingAvailable(this, unit->swimClass(), unit->posX, unit->posY, distBuilding))
	{
		unitsFailingRequirements[UnitCantAccessBuilding] += 1;
		return false;
	}
	if(*distBuilding >= timeLeft)
	{
		unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
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
	                                  unit->posX, unit->posY, &distResource))
	{
		if(wantedResource<BASIC_COUNT)
			unitsFailingRequirements[UnitCantAccessResource] += 1;
		else
			unitsFailingRequirements[UnitCantAccessFruit] += 1;
		return false;
	}
	if(distResource >= timeLeft)
	{
		if(wantedResource<BASIC_COUNT)
			unitsFailingRequirements[UnitTooFarFromResource] += 1;
		else
			unitsFailingRequirements[UnitTooFarFromFruit] += 1;
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

void Building::gatherBringResourcesCandidates(BringResourcesCandidate* candidates, int wantedResource)
{
	// The tallies count units, and the same unit is offered every resource the
	// building tries to staff, so start each scan from zero: what the info panel
	// ends up showing is one coherent pass, for the last resource attempted.
	for(int i=0; i<UnitCantWorkReasonSize; ++i)
		unitsFailingRequirements[i]=0;

	for(int n=0; n<Unit::MAX_COUNT; ++n)
	{
		candidates[n].unit = NULL;
		candidates[n].distance = 0;
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
			candidates[n].unit = unit;
			candidates[n].distance = dist;
		}
	}
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

void Building::selectFetcher(const BringResourcesCandidate* candidates, int wantedResource, BringResourcesSelection& sel)
{
	for(int n=0; n<Unit::MAX_COUNT; ++n)
	{
		Unit* unit=candidates[n].unit;
		if(unit==NULL)
			continue;

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


bool Building::subscribeToBringResourcesStep()
{
	for(int i=0; i<UnitCantWorkReasonSize; ++i)
	{
		unitsFailingRequirements[i]=0;
	}
	if (buildingState==DEAD)
		return false;
	if (verbose)
		printf("bgid=%d, subscribeToBringResourcesStep()...\n", gid);

	bool hired=false;
	if ((Sint32)unitsWorking.size()<desiredMaxUnitWorking)
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
				gatherBringResourcesCandidates(candidates, r);
				selectFetcher(candidates, r, sel);
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

bool Building::considerUnitForExplorerFlag(Unit* unit, int* dist)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		unitsFailingRequirements[UnitNotAvailable] += 1;
		return false;
	}
	if (!canUnitWorkHere(unit))
	{
		unitsFailingRequirements[UnitTooLowLevel] += 1;
		return false;
	}
	int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungriness;
	// warpDistSquare returns squared Euclidean distance, so timeLeft is
	// squared here to keep the comparison in the same units. Worker/warrior
	// flags compare against Map::buildingAvailable (linear gradient
	// distance) and must NOT square — see considerUnitForWorkerFlag.
	int timeLeftSquared = timeLeft * timeLeft;
	int directdist = owner->map->warpDistSquare(unit->posX, unit->posY, posX, posY);
	if (timeLeftSquared < directdist)
	{
		unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
		return false;
	}
	*dist = directdist;
	return true;
}

bool Building::considerUnitForWorkerFlag(Unit* unit, int* dist)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		unitsFailingRequirements[UnitNotAvailable] += 1;
		return false;
	}
	if (!canUnitWorkHere(unit))
	{
		unitsFailingRequirements[UnitTooLowLevel] += 1;
		return false;
	}
	int distBuilding = 0;
	// timeLeft and distBuilding are both linear (in ticks-remaining and
	// linear gradient steps respectively); compare as-is. The corresponding
	// check in subscribeToBringResourcesStep uses the same pairing.
	int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungriness;
	bool canSwim = unit->performance[SWIM];
	if (!owner->map->buildingAvailable(this, unit->swimClass(), unit->posX, unit->posY, &distBuilding))
	{
		unitsFailingRequirements[UnitCantAccessBuilding] += 1;
		return false;
	}
	if (distBuilding >= timeLeft)
	{
		unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
		return false;
	}
	if (anyResourceToClear[canSwim] == 2)
	{
		unitsFailingRequirements[UnitCantAccessResource] += 1;
		return false;
	}
	*dist = distBuilding;
	return true;
}

bool Building::considerUnitForWarriorFlag(Unit* unit, int* dist)
{
	if (unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
	{
		unitsFailingRequirements[UnitNotAvailable] += 1;
		return false;
	}
	if (!canUnitWorkHere(unit))
	{
		unitsFailingRequirements[UnitTooLowLevel] += 1;
		return false;
	}
	if (unit->movement == Unit::MOV_ATTACKING_TARGET)
	{
		unitsFailingRequirements[UnitNotAvailable] += 1;
		return false;
	}
	int distBuilding = 0;
	// timeLeft and distBuilding are both linear (in ticks-remaining and
	// linear gradient steps respectively); compare as-is. The corresponding
	// check in subscribeToBringResourcesStep uses the same pairing.
	int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungriness;
	if (!owner->map->buildingAvailable(this, unit->swimClass(), unit->posX, unit->posY, &distBuilding))
	{
		unitsFailingRequirements[UnitCantAccessBuilding] += 1;
		return false;
	}
	if (distBuilding >= timeLeft)
	{
		unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
		return false;
	}
	*dist = distBuilding;
	return true;
}

bool Building::subscribeForFlagingStep()
{
	if (buildingState==DEAD)
	{
		for(int i=0; i<UnitCantWorkReasonSize; ++i)
		{
			unitsFailingRequirements[i]=0;
		}
		return false;
	}

	bool hired=false;
	subscriptionWorkingTimer++;
	if (subscriptionWorkingTimer>32)
	{
		// Reset stale failure counts for the case where the while loop below
		// doesn't run (building already fully staffed). When the loop does run,
		// this is overwritten by the per-iteration reset on iteration 1.
		for(int i=0; i<UnitCantWorkReasonSize; ++i)
		{
			unitsFailingRequirements[i]=0;
		}
		while (((Sint32)unitsWorking.size()<desiredMaxUnitWorking))
		{
			// Per-iteration reset: the same Unit::MAX_COUNT array is rescanned
			// each iteration (already-hired units are filtered via
			// attachedBuilding==this); without this, the same failing units
			// would be counted N times across N iterations.
			for(int i=0; i<UnitCantWorkReasonSize; ++i)
			{
				unitsFailingRequirements[i]=0;
			}

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
				if(type->zonable[EXPLORER])
				{
					if(unit->typeNum != EXPLORER)
						continue;
					if(considerUnitForExplorerFlag(unit, &distances[n]))
						possibleUnits[n]=unit;
				}
				else if(type->zonable[WORKER])
				{
					if(unit->typeNum != WORKER)
						continue;
					if(considerUnitForWorkerFlag(unit, &distances[n]))
						possibleUnits[n]=unit;
				}
				else if(type->zonable[WARRIOR])
				{
					if(unit->typeNum != WARRIOR)
						continue;
					if(considerUnitForWarriorFlag(unit, &distances[n]))
						possibleUnits[n]=unit;
				}
			}

			int minValue=INT_MAX;
			int minLevel=INT_MAX;
			int maxLevel=-INT_MAX;
			Unit *choosen=NULL;

			/* To choose a good unit, we get a composition of things:
			1-the closer the unit is, the better it is.
			2-the less the unit is hungry, the better it is.
			3-the more hp the unit has, the better it is.
			*/
			if (type->zonable[EXPLORER])
			{
				for(int n=0; n<Unit::MAX_COUNT; ++n)
				{
					Unit* unit=possibleUnits[n];
					if(unit==NULL)
						continue;

					int timeLeft=unit->hungry/unit->race->hungriness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					timeLeft*=timeLeft;
					hp*=hp;
					int dist=distances[n];
					//Use explorers without ground attack first before ones with, so that ground attacking explorers
					//are available for more important jobs
					int value=dist-2*timeLeft-2*hp;
					int level = unit->level[MAGIC_ATTACK_GROUND];
					if ((level < minLevel) || (level==minLevel && value<minValue))
					{
						minValue=value;
						minLevel=level;
						choosen=unit;
					}
				}
			}
			else if (type->zonable[WARRIOR])
			{
				for(int n=0; n<Unit::MAX_COUNT; ++n)
				{
					Unit* unit=possibleUnits[n];
					if(unit==NULL)
						continue;

					int timeLeft=unit->hungry/unit->race->hungriness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					int dist = distances[n];
					int value=dist-2*timeLeft-2*hp;
					//We want to maximize the attack level, use higher level soldiers first
					int level=unit->performance[ATTACK_SPEED]*unit->getRealAttackStrength();
					if ((level > maxLevel) || (level==maxLevel && value<minValue))
					{
						minValue=value;
						maxLevel=level;
						choosen=unit;
					}
				}
			}
			else if (type->zonable[WORKER])
			{
				for(int n=0; n<Unit::MAX_COUNT; ++n)
				{
					Unit* unit=possibleUnits[n];
					if(unit==NULL)
						continue;

					int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungriness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					int dist = distances[n];
					int value=dist-timeLeft-hp;
					int level = unit->workerLevel();
					//We want to minimize the level of harvesting units, so that the higher level
					//units are available for more important work.
					if ((level < minLevel) || (level==minLevel && value<minValue))
					{
						minValue=value;
						minLevel=level;
						choosen=unit;
					}
				}
			}
			else
				assert(false);

			if (choosen)
			{
				unitsWorking.push_back(choosen);
				choosen->subscriptionSuccess(this, false);
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
	unitsInside.push_back(unit);
	unit->subscriptionSuccess(this, true);
	updateCallLists();
}


