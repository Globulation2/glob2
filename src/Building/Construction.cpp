/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <list>
#include <math.h>
#include <Stream.h>
#include <stdlib.h>
#include <algorithm>
#include <climits>

#include "Building.h"
#include "BuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
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
	Sint32 minProportion=0x7FFFFFFF;
	int minType=-1;
	int deci=syncRand()%MAX_RESSOURCES;
	for (int ib=0; ib<MAX_RESSOURCES; ib++)
	{
		int i=(ib+deci)%MAX_RESSOURCES;
		int maxr=type->maxRessource[i];
		if (maxr)
		{
			Sint32 proportion=(ressources[i]<<16)/maxr;
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

void Building::wishedRessources(int needs[MAX_NB_RESSOURCES])
{
	 // we balance the system with Units working on it:
	for (int ri = 0; ri < MAX_NB_RESSOURCES; ri++)
		needs[ri] = (4 * (type->maxRessource[ri] - ressources[ri])) / (type->multiplierRessource[ri] * 3);
	for (std::list<Unit *>::iterator ui = unitsWorking.begin(); ui != unitsWorking.end(); ++ui)
		if ((*ui)->destinationPurpose >= 0)
		{
			assert((*ui)->destinationPurpose < MAX_NB_RESSOURCES);
			needs[(*ui)->destinationPurpose]--;
		}
}

void Building::computeWishedRessources()
{
	 // we balance the system with Units working on it:
	for (int ri = 0; ri < MAX_NB_RESSOURCES; ri++)
		wishedResources[ri] = (4 * (type->maxRessource[ri] - ressources[ri])) / (type->multiplierRessource[ri] * 3);
	for (std::list<Unit *>::iterator ui = unitsWorking.begin(); ui != unitsWorking.end(); ++ui)
		if ((*ui)->destinationPurpose >= 0)
		{
			assert((*ui)->destinationPurpose < MAX_NB_RESSOURCES);
			wishedResources[(*ui)->destinationPurpose]--;
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
			if ((type->prevLevel==-1) || !isHardSpaceForBuildingSite(REPAIR))
				return;
			constructionResultState=REPAIR;
		}
		else
		{
			if ((type->nextLevel==-1) || !isHardSpaceForBuildingSite(UPGRADE))
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
		maxUnitWorkingLocal=0;
		maxUnitWorking=0;
		maxUnitInside=0;
		updateCallLists();
		updateUnitsWorking(); // To remove all units working.
		updateUnitsHarvesting(); // To remove all units working.
		//following reassigns units to work on upgrade, certain buildings will
		//glitch if units are not unassigned and then reassigned like this
		maxUnitWorking = unitWorking;
		maxUnitWorkingLocal = maxUnitWorking;
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
		int targetLevelTypeNum=-1;

		if (constructionResultState==UPGRADE)
			targetLevelTypeNum=type->prevLevel;
		else if (constructionResultState==REPAIR)
			targetLevelTypeNum=type->nextLevel;
		else
			assert(false);

		if (targetLevelTypeNum!=-1)
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
	posXLocal=posX;
	posYLocal=posY;

	if (!type->isVirtual)
		owner->map->setBuilding(posX, posY, type->width, type->height, gid);

	maxUnitWorking=maxUnitWorkingPrevious;
	maxUnitWorkingLocal=maxUnitWorking; //maxUnitWorking;
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
		maxUnitWorkingLocal=0;
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
	maxUnitWorkingLocal=maxUnitWorking;
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
			// if i am in the call lists, update my then my position will need to be updated
			else if(callListState == 1 && oldPriority == priority)
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
					//A Building newly getting available to feed is locked to conversion for 150 frames
					canNotConvertUnitTimer=150;
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
