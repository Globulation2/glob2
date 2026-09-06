// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <list>
#include <math.h>
#include <stdlib.h>
#include <climits>

#include "Building.h"
#include "BuildingType.h"
#include "FixedPoint.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Order.h"

namespace
{
	/// Sentinel for the best need-scaled ressource distance found so far: larger
	/// than any real value, so the first reachable needed ressource always wins
	/// the running-minimum comparison.
	constexpr int UNREACHABLE_RESSOURCE_DIST = 100000;

	/// Weight of the harvest level in a ressource candidate's ranking key. The
	/// harvest level dominates the comparison; the walk level breaks ties.
	constexpr int HARVEST_LEVEL_WEIGHT = 10;

	/// Composite "experience" key used to rank ressource-carrying candidates;
	/// higher is preferred.
	int bringRessourcesLevel(const Unit* unit)
	{
		return unit->level[HARVEST] * HARVEST_LEVEL_WEIGHT + unit->level[WALK];
	}
}

void Building::step(void)
{
	computeWishedRessources(wishedResources);

	updateCallLists();
	if(underAttackTimer>0)
		underAttackTimer--;
	if(canNotConvertUnitTimer>0)
		canNotConvertUnitTimer--;
	// NOTE : Unit needs to update itself when it is in a building
}


bool Building::considerUnitForRessources(Unit* unit, int* dist, int* resource)
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

	Map* map = owner->map;
	int distBuilding=0;
	int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
	bool canSwim=unit->performance[SWIM];
	if(!map->buildingAvailable(this, canSwim, unit->posX, unit->posY, &distBuilding))
	{
		unitsFailingRequirements[UnitCantAccessBuilding] += 1;
		return false;
	}
	if(distBuilding >= timeLeft)
	{
		unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
		return false;
	}

	// A unit already carrying a needed ressource is taken as-is; its distance
	// metric is just the gradient distance to the building.
	int unitr = unit->carriedRessource;
	if((unitr>=0) && neededRessource(unitr))
	{
		*dist = distBuilding;
		*resource = unitr;
		return true;
	}

	// Otherwise look for the best reachable needed ressource the unit could
	// fetch, scoring by combined (building + ressource) distance scaled by how
	// badly the ressource is needed. Track whether the only candidates were
	// out of hunger range, and whether they were regular ressources or fruit,
	// so the rejection reason is specific.
	int bestDist = UNREACHABLE_RESSOURCE_DIST;
	int bestResource = RESSOURCE_TYPE_NONE;
	bool regularFound=false;
	bool fruitFound=false;
	bool regularFoundTooFar=false;
	bool fruitFoundTooFar=false;
	int x=unit->posX;
	int y=unit->posY;
	for(int r=0; r<MAX_NB_RESSOURCES; ++r)
	{
		int need = neededRessource(r);
		if(need>0)
		{
			if(r<BASIC_COUNT)
				regularFound=true;
			else
				fruitFound=true;
			int distResource = 0;
			if (map->ressourceAvailable(owner->teamNumber, r, canSwim, x, y, &distResource))
			{
				if(distResource<timeLeft)
				{
					int dist = (distBuilding + distResource)<<Q8_FIXED_POINT_SHIFT;
					int value = dist / need;
					if(value < bestDist)
					{
						bestDist = value;
						bestResource=r;
					}
				}
				else
				{
					if(r<BASIC_COUNT)
						regularFoundTooFar=true;
					else
						fruitFoundTooFar=true;
				}
			}
		}
	}
	if(bestResource == RESSOURCE_TYPE_NONE)
	{
		if(regularFound)
		{
			if(regularFoundTooFar)
				unitsFailingRequirements[UnitTooFarFromResource] += 1;
			else
				unitsFailingRequirements[UnitCantAccessResource] += 1;
		}
		else if(fruitFound)
		{
			if(fruitFoundTooFar)
				unitsFailingRequirements[UnitCantAccessFruit] += 1;
			else
				unitsFailingRequirements[UnitTooFarFromFruit] += 1;
		}
		return false;
	}

	*resource = bestResource;
	*dist = bestDist;
	return true;
}

void Building::gatherBringRessourcesCandidates(BringRessourcesCandidate* candidates)
{
	for(int n=0; n<Unit::MAX_COUNT; ++n)
	{
		candidates[n].unit = NULL;
		candidates[n].distance = 0;
		candidates[n].resource = -1;
		Unit* unit=owner->myUnits[n];
		if(!unit)
			continue;
		if(!unit->performance[HARVEST])
			continue;
		if(unit->attachedBuilding == this && unit->activity == Unit::ACT_FILLING)
			continue;

		int dist;
		int resource;
		if(considerUnitForRessources(unit, &dist, &resource))
		{
			candidates[n].unit = unit;
			candidates[n].distance = dist;
			candidates[n].resource = resource;
		}
	}
}

void Building::selectUnitCarryingNeededRessource(const BringRessourcesCandidate* candidates, BringRessourcesSelection& sel)
{
	for(int n=0; n<Unit::MAX_COUNT; ++n)
	{
		Unit* unit=candidates[n].unit;
		if(unit==NULL)
			continue;

		int r=unit->carriedRessource;
		int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
		if ((r>=0) && neededRessource(r))
		{
			int value=candidates[n].distance-(timeLeft>>1);
			int level = bringRessourcesLevel(unit);
			// Every carrying candidate has its destinationPurpose set to the
			// ressource it carries, not only the one finally chosen.
			unit->destinationPurpose=r;
			if ((level>sel.maxLevel) || (level==sel.maxLevel && value<sel.minValue))
			{
				sel.minValue=value;
				sel.maxLevel=level;
				sel.choosen=unit;
			}
		}
	}
}

void Building::selectEmptyHandedUnit(const BringRessourcesCandidate* candidates, BringRessourcesSelection& sel)
{
	for(int n=0; n<Unit::MAX_COUNT; ++n)
	{
		Unit* unit=candidates[n].unit;
		if(unit==NULL)
			continue;

		if (unit->carriedRessource<0)
		{
			int value=candidates[n].distance;
			int level = bringRessourcesLevel(unit);
			if ((level>sel.maxLevel) || (level==sel.maxLevel && value<sel.minValue))
			{
				sel.minValue=value;
				sel.maxLevel=level;
				sel.choosen=unit;
				unit->destinationPurpose=candidates[n].resource;
			}
		}
	}
}

void Building::selectUnitCarryingUnwantedRessource(const BringRessourcesCandidate* candidates, BringRessourcesSelection& sel)
{
	for(int n=0; n<Unit::MAX_COUNT; ++n)
	{
		Unit* unit=candidates[n].unit;
		if(unit==NULL)
			continue;

		int r2=unit->carriedRessource;
		if ((r2>=0) && !neededRessource(r2))
		{
			int value=candidates[n].distance;
			int level = bringRessourcesLevel(unit);
			if ((level>sel.maxLevel) || (level==sel.maxLevel && value<sel.minValue))
			{
				sel.minValue=value;
				sel.maxLevel=level;
				sel.choosen=unit;
				unit->destinationPurpose=candidates[n].resource;
			}
		}
	}
}

bool Building::subscribeToBringRessourcesStep()
{
	for(int i=0; i<UnitCantWorkReasonSize; ++i)
	{
		unitsFailingRequirements[i]=0;
	}
	if (buildingState==DEAD)
		return false;
	if (verbose)
		printf("bgid=%d, subscribeToBringRessourcesStep()...\n", gid);

	bool hired=false;
	if ((Sint32)unitsWorking.size()<desiredMaxUnitWorking)
	{
		BringRessourcesCandidate candidates[Unit::MAX_COUNT];
		gatherBringRessourcesCandidates(candidates);

		// Hire the best candidate in strict priority tiers: a unit already
		// carrying a needed ressource first, then an empty-handed unit, then a
		// unit carrying an unwanted ressource. A later tier is only consulted
		// when the earlier tiers found nobody.
		BringRessourcesSelection sel;
		sel.maxLevel = -1;
		sel.minValue = INT_MAX;
		sel.choosen = NULL;

		selectUnitCarryingNeededRessource(candidates, sel);
		if (sel.choosen==NULL)
			selectEmptyHandedUnit(candidates, sel);
		if (sel.choosen==NULL)
			selectUnitCarryingUnwantedRessource(candidates, sel);

		if (sel.choosen)
		{
			unitsWorking.push_back(sel.choosen);
			sel.choosen->subscriptionSuccess(this, false);
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
	int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungryness;
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
	// check in subscribeToBringRessourcesStep uses the same pairing.
	int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungryness;
	bool canSwim = unit->performance[SWIM];
	if (!owner->map->buildingAvailable(this, canSwim, unit->posX, unit->posY, &distBuilding))
	{
		unitsFailingRequirements[UnitCantAccessBuilding] += 1;
		return false;
	}
	if (distBuilding >= timeLeft)
	{
		unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
		return false;
	}
	if (anyRessourceToClear[canSwim] == 2)
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
	// check in subscribeToBringRessourcesStep uses the same pairing.
	int timeLeft = (unit->hungry - unit->trigHungry) / unit->race->hungryness;
	bool canSwim = unit->performance[SWIM];
	if (!owner->map->buildingAvailable(this, canSwim, unit->posX, unit->posY, &distBuilding))
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

					int timeLeft=unit->hungry/unit->race->hungryness;
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

					int timeLeft=unit->hungry/unit->race->hungryness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					int dist = distances[n];
					int value=dist-2*timeLeft-2*hp;
					//We want to maximize the attack level, use higher level soldeirs first
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

					int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					int dist = distances[n];
					int value=dist-timeLeft-hp;
					int level = unit->level[HARVEST];
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


