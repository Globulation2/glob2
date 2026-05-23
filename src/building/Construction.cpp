// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <list>
#include <math.h>
#include <Stream.h>
#include <stdlib.h>
#include <algorithm>
#include <climits>

#include "Building.h"
#include "BuildingType.h"
#include "EngineTiming.h"
#include "FixedPoint.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include "Order.h"
#include "Bullet.h"
#include "Integrity.h"

bool Building::isRessourceFull(void)
{
	for (int i=0; i<MAX_NB_RESSOURCES; i++)
	{
		if (ressources[i]+type->multiplierRessource[i]<=type->maxRessource[i])
			return false;
	}
	return true;
}

int Building::neededRessource(void)
{
	Sint32 minProportion = MIN_PROPORTION_INIT;
	int minType = RESSOURCE_TYPE_NONE;
	int deci=syncRand()%MAX_RESSOURCES;
	for (int ib=0; ib<MAX_RESSOURCES; ib++)
	{
		int i=(ib+deci)%MAX_RESSOURCES;
		int maxr=type->maxRessource[i];
		if (maxr)
		{
			Sint32 proportion=(ressources[i]<<FIXED_POINT_SHIFT_16)/maxr;
			if (proportion<minProportion)
			{
				minProportion=proportion;
				minType=i;
			}
		}
	}
	return minType;
}

void Building::neededRessources(int needs[MAX_NB_RESSOURCES])
{
	for (int ri=0; ri<MAX_NB_RESSOURCES; ri++)
		needs[ri]=Building::neededRessource(ri);
}

void Building::computeWishedRessources(int needs[MAX_NB_RESSOURCES])
{
	 // we balance the system with Units working on it:
	for (int ri = 0; ri < MAX_NB_RESSOURCES; ri++)
		needs[ri] = (WISHED_RESOURCE_NUM * (type->maxRessource[ri] - ressources[ri])) / (type->multiplierRessource[ri] * WISHED_RESOURCE_DEN);
	for (std::list<Unit *>::iterator ui = unitsWorking.begin(); ui != unitsWorking.end(); ++ui)
		if ((*ui)->destinationPurpose >= 0)
		{
			assert((*ui)->destinationPurpose < MAX_NB_RESSOURCES);
			needs[(*ui)->destinationPurpose]--;
		}
}

int Building::neededRessource(int r)
{
	assert(r >= 0);
	int need = type->maxRessource[r] - ressources[r] + 1 - type->multiplierRessource[r];
	return std::max(need,0);
}


int Building::totalWishedRessource()
{
	int sum=0;
	for (int ri = 0; ri < MAX_NB_RESSOURCES; ri++)
		sum += wishedResources[ri];
	return sum;
}



void Building::launchConstruction(Sint32 unitWorking, Sint32 unitWorkingFuture)
{
	if ((buildingState==ALIVE) && (!type->isBuildingSite))
	{
		if (hp<type->hpMax)
		{
			if ((type->prevLevel==BUILDING_LEVEL_NONE) || !isHardSpaceForBuildingSite(REPAIR))
				return;
			constructionResultState=REPAIR;
		}
		else
		{
			if ((type->nextLevel==BUILDING_LEVEL_NONE) || !isHardSpaceForBuildingSite(UPGRADE))
				return;
			constructionResultState=UPGRADE;
		}

		owner->removeFromAbilitiesLists(this);

		// We remove all units who are going to the building:
		// Notice that the algotithm is not fast but clean.
		std::list<Unit *> unitsToRemove;
		for (std::list<Unit *>::iterator it=unitsInside.begin(); it!=unitsInside.end(); ++it)
		{
			Unit *u=*it;
			assert(u);
			int d=u->displacement;
			if ((d!=Unit::DIS_INSIDE)&&(d!=Unit::DIS_ENTERING_BUILDING)&&(d!=Unit::DIS_EXITING_BUILDING))
			{
				u->standardRandomActivity();
				unitsToRemove.push_front(u);
			}
		}

		for (std::list<Unit *>::iterator it=unitsToRemove.begin(); it!=unitsToRemove.end(); ++it)
		{
			Unit *u=*it;
			assert(u);
			unitsInside.remove(u);
		}

		maxUnitWorkingPrevious = maxUnitWorking;
		buildingState=WAITING_FOR_CONSTRUCTION;
		maxUnitWorking=0;
		maxUnitInside=0;
		updateCallLists();
		updateUnitsWorking(); // To remove all units working.
		updateUnitsHarvesting(); // To remove all units working.
		//following reassigns units to work on upgrade, certain buildings will
		//glitch if units are not unassigned and then reassigned like this
		maxUnitWorking = unitWorking;
		maxUnitWorkingPreferred = maxUnitWorking;
		maxUnitWorkingFuture = unitWorkingFuture;
		updateConstructionState(); // To switch to a real building site, if all units have been freed from building.
	}
}

void Building::cancelConstruction(Sint32 unitWorking)
{
	Sint32 recoverTypeNum=typeNum;
	BuildingType *recoverType=type;

	if (type->isBuildingSite)
	{
		assert(buildingState==ALIVE);
		int targetLevelTypeNum=BUILDING_LEVEL_NONE;

		if (constructionResultState==UPGRADE)
			targetLevelTypeNum=type->prevLevel;
		else if (constructionResultState==REPAIR)
			targetLevelTypeNum=type->nextLevel;
		else
			assert(false);

		if (targetLevelTypeNum!=BUILDING_LEVEL_NONE)
		{
			recoverTypeNum=targetLevelTypeNum;
			recoverType=globalContainer->buildingsTypes.get(targetLevelTypeNum);
		}
		else
			assert(false);
	}
	else if (buildingState==WAITING_FOR_CONSTRUCTION_ROOM)
	{
		if(constructionResultState == UPGRADE)
			removeForbiddenZoneFromUpgradeArea();

		owner->buildingsTryToBuildingSiteRoom.remove(this);
		buildingState=ALIVE;
	}
	else if (buildingState==WAITING_FOR_CONSTRUCTION)
	{
		buildingState=ALIVE;
	}
	else
	{
		// Congratulation, you have managed to click "cancel upgrade"
		// when the building upgrade" was already canceled.
		return;
	}

	constructionResultState=NO_CONSTRUCTION;

	if (!type->isVirtual)
		owner->map->setBuilding(posX, posY, type->width, type->height, NOGBID);
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;
	owner->removeFromAbilitiesLists(this);
	owner->prestige-=type->prestige;
	typeNum=recoverTypeNum;
	type=recoverType;
	owner->prestige+=type->prestige;
	owner->addToStaticAbilitiesLists(this);

	//Update the pointer ressources to the newly changed type
	updateRessourcesPointer();

	posX=midPosX+type->decLeft;
	posY=midPosY+type->decTop;

	if (!type->isVirtual)
		owner->map->setBuilding(posX, posY, type->width, type->height, gid);

	maxUnitWorking=maxUnitWorkingPrevious;
	maxUnitInside=type->maxUnitInside;
	updateCallLists();
	updateUnitsWorking();
	// no unit harvesting at that point

	if (hp>=type->hpInit)
		hp=type->hpInit;

	productionTimeout=type->unitProductionTime;

	if (type->unitProductionTime)
		owner->swarms.push_back(this);
	if (type->shootingRange)
		owner->turrets.push_back(this);
	if (type->canExchange)
		owner->canExchange.push_back(this);
	if (type->isVirtual)
		owner->virtualBuildings.push_back(this);
	if (type->zonable[WORKER])
		owner->clearingFlags.push_back(this);

	totalRatio=0;

	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		ratio[i]=1;
		totalRatio++;
		percentUsed[i]=0;
	}

	setMapDiscovered();
}

void Building::launchDelete(void)
{
	if (buildingState==ALIVE)
	{
		buildingState=WAITING_FOR_DESTRUCTION;
		maxUnitWorkingPrevious = maxUnitWorking;
		maxUnitWorking=0;
		maxUnitInside=0;
		desiredMaxUnitWorking = 0;
		updateCallLists();
		updateUnitsWorking();
		updateUnitsHarvesting();
		owner->buildingsWaitingForDestruction.push_front(this);
	}
}

void Building::cancelDelete(void)
{
	buildingState=ALIVE;
	maxUnitWorking=maxUnitWorkingPrevious;
	maxUnitInside=type->maxUnitInside;
	updateCallLists();
	updateUnitsWorking();
	// we do not update units harvesting because there is none at this point
	// we do not update owner->buildingsWaitingForDestruction because Team::syncStep will remove this building from the list
}


void Building::updateCallLists(void)
{
	if (buildingState==DEAD)
		return;
	desiredMaxUnitWorking = desiredNumberOfWorkers();
	bool ressourceFull=isRessourceFull();
	if (ressourceFull && !(type->canExchange && owner->openMarket()))
	{
		// Then we don't need anyone more to fill me, if I'm still in the call list for units,
		// remove me
		if(callListState != 0)
		{
			owner->remove_building_needing_work(this, oldPriority);
			callListState=0;
			oldPriority = priority;
		}
	}

	if (unitsWorking.size()<(unsigned)desiredMaxUnitWorking)
	{
		if (buildingState==ALIVE)
		{
			// I need units, if I am not in the call lists, add me
			if(callListState != 1)
			{
				owner->add_building_needing_work(this, priority);
				callListState = 1;
				oldPriority = priority;
			}
			// I am in the call list. Re-register at current priority. This
			// handles both BH-230 (priority changed -> move to new bucket)
			// and the original same-priority re-sort, which is observable
			// because Team::updateAllBuildingTasks calls subscribe* on each
			// building in the bucket, and subscribe* -> updateCallLists
			// mutates the bucket mid-iteration.
			else
			{
				owner->remove_building_needing_work(this, oldPriority);
				owner->add_building_needing_work(this, priority);
				oldPriority = priority;
			}
		}
	}
	else
	{
		if(callListState != 0)
		{
			owner->remove_building_needing_work(this, oldPriority);
			callListState=0;
			oldPriority = priority;
		}
	}

	if ((signed)unitsInside.size()<maxUnitInside)
	{
		// Add itself in the right "call-lists":
		for (int i=0; i<NB_ABILITY; i++)
			if (inUpgrade[i]!=LS_IN && type->upgrade[i])
			{
				owner->upgrade[i].push_front(this);
				inUpgrade[i]=LS_IN;
			}

		// this is for food handling
		if (type->canFeedUnit)
		{
			if (ressources[CORN]>(int)unitsInside.size())
			{
				if (inCanFeedUnit!=LS_IN)
				{
					owner->canFeedUnit.push_front(this);
					//A Building newly getting available to feed is locked to conversion for CANNOT_CONVERT_TIMER_INIT frames
					canNotConvertUnitTimer=CANNOT_CONVERT_TIMER_INIT;
					inCanFeedUnit=LS_IN;
				}
			}
			else
			{
				if (inCanFeedUnit!=LS_OUT)
				{
					owner->canFeedUnit.remove(this);
					inCanFeedUnit=LS_OUT;
				}
			}
		}

		// this is for Unit healing
		if (type->canHealUnit && inCanHealUnit!=LS_IN)
		{
			owner->canHealUnit.push_front(this);
			inCanHealUnit=LS_IN;
		}
	}
	else
	{
		// delete itself from all Call lists
		for (int i=0; i<NB_ABILITY; i++)
			if (inUpgrade[i]!=LS_OUT && type->upgrade[i])
			{
				owner->upgrade[i].remove(this);
				inUpgrade[i]=LS_OUT;
			}

		if (type->canFeedUnit && inCanFeedUnit!=LS_OUT)
		{
			owner->canFeedUnit.remove(this);
			inCanFeedUnit=LS_OUT;
		}
		if (type->canHealUnit && inCanHealUnit!=LS_OUT)
		{
			owner->canHealUnit.remove(this);
			inCanHealUnit=LS_OUT;
		}
	}
}

void Building::updateConstructionState(void)
{
	if (buildingState==DEAD)
		return;

	if ((buildingState==WAITING_FOR_CONSTRUCTION) || (buildingState==WAITING_FOR_CONSTRUCTION_ROOM))
	{
		if (!isHardSpaceForBuildingSite())
		{
			//this is semi-faulty code and needs to be fixed later
			//anytime a building is upgraded but unable to do so it reverts to
			//one worker working instead of previous value
			cancelConstruction(1);
		}
		else if ((unitsWorking.size()==0) && (unitsInside.size()==0))
		{
			if (buildingState!=WAITING_FOR_CONSTRUCTION_ROOM)
			{
				buildingState=WAITING_FOR_CONSTRUCTION_ROOM;
				owner->buildingsTryToBuildingSiteRoom.push_front(this);
				if(constructionResultState == UPGRADE)
					addForbiddenZoneToUpgradeArea();
				if (verbose)
					printf("bgid=%d, inserted in buildingsTryToBuildingSiteRoom\n", gid);
			}
		}
		else if (verbose)
			printf("bgid=%d, Building wait for upgrade, uws=%lu, uis=%lu.\n", gid, (unsigned long)unitsWorking.size(), (unsigned long)unitsInside.size());
	}
}
