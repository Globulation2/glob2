// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <list>
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <climits>

#include "Building.h"
#include "BuildingType.h"
#include "FixedPoint.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include "Order.h"


void Building::updateBuildingSite(void)
{
	assert(type->isBuildingSite);

	if (isResourceFull() && (buildingState!=WAITING_FOR_DESTRUCTION))
	{
		// we really uses the resources of the building site:
		for(int i=0; i<MAX_RESOURCES; i++)
			resources[i]-=type->maxResource[i];

		owner->prestige-=type->prestige;
		typeNum=type->nextLevel;
		type=globalContainer->buildingsTypes.get(type->nextLevel);
		assert(constructionResultState!=NO_CONSTRUCTION);
		constructionResultState=NO_CONSTRUCTION;
		owner->prestige+=type->prestige;

		//Update the pointer resources to the newly changed type
		updateResourcesPointer();


		//now that building is complete clear the workers
		releaseAllWorkers();

		if (type->maxUnitWorking)
		{
			maxUnitWorking = maxUnitWorkingFuture;
			maxUnitWorkingFuture = 0;
		}
		else
			maxUnitWorking=0;

		// The working units still works for us, but
		// we don't have any unit in buildings
		assert(unitsInside.size()==0);
		maxUnitInside=type->maxUnitInside;

		if (hp>=getEffectiveInitHp())
			hp=getEffectiveInitHp();

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

		setMapDiscovered();
		owner->pushGameEvent(GameEvent::buildingCompleted(owner->game->stepCounter, getMidX(), getMidY(), shortTypeNum));

		// we need to do an update again
		updateCallLists();
		updateUnitsWorking();
		// no unit harvesting at that point
	}
}



void Building::updateUnitsWorking(void)
{
	if (maxUnitWorking==0)
	{
		// This is only a special optimization case:
		releaseAllWorkers();
	}
	else
	{
		while (unitsWorking.size()>(unsigned)desiredMaxUnitWorking)
		{
			int maxDistSquare=0;

			Unit *fu=NULL;
			std::list<Unit *>::iterator ittemp;

			// First choice: free a unit who has a not needed resource..
			for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end();)
			{
				int r=(*it)->carriedResource;
				if (r>=0 && !neededResource(r))
				{
					fu=(*it);
					fu->standardRandomActivity();
					it=unitsWorking.erase(it);
					continue;
				} else {
					++it;
				}
			}
			if(fu!=NULL) continue;
			// Second choice: free a unit who has no resource..
			if (fu==NULL)
			{
				int minDistSquare=INT_MAX;
				for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
				{
					int r=(*it)->carriedResource;
					if (r<0)
					{
						int tx = posX;
						int ty = posY;
						if((*it)->targetX != -1)
						{
							tx = (*it)->targetX;
							ty = (*it)->targetY;
						}
						int newDistSquare=distSquare((*it)->posX, (*it)->posY, tx, ty);
						if (newDistSquare<minDistSquare)
						{
							minDistSquare=newDistSquare;
							fu=(*it);
							ittemp=it;
						}
					}
				}
			}

			// Third choice: free any unit..
			if (fu==NULL)
				for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
				{
					int newDistSquare=distSquare((*it)->posX, (*it)->posY, posX, posY);
					if (newDistSquare>maxDistSquare)
					{
						maxDistSquare=newDistSquare;
						fu=(*it);
						ittemp=it;
					}
				}

			if (fu!=NULL)
			{
				if (verbose)
					printf("bgid=%d, we free the unit gid=%d\n", gid, fu->gid);
				// We free the unit.
				fu->standardRandomActivity();
				unitsWorking.erase(ittemp);
			}
			else
				break;
		}
	}
}

void Building::updateUnitsHarvesting(void)
{
	// if we are not alive or has not vision, remove all units harvesting from this building
	for (std::list<Unit *>::iterator it=unitsHarvesting.begin(); it!=unitsHarvesting.end();)
	{
		std::list<Unit *>::iterator tmpIt = it;
		Unit* u = *tmpIt;
		it++;
		
		// if the building is not available to fetch from (invisible or broken)
		if ((buildingState != ALIVE) || ((owner->sharedVisionExchange & u->owner->me) == 0))
		{
			// cancel the task u were just doing
		    u->attachedBuilding->removeUnitFromWorking(u);
		    // cancel fetching resources here
		    removeUnitFromHarvesting(u);
		    // behave randomly
		    u->standardRandomActivity();
			// TODO: replacing the remove by an erase should be a lot faster but
			// it causes the game to crash when a market gets destroyed. No idea
			// why. Actually there's no point bothering about this here as this
			// method is not performance critical but still it's weird to me
			// why it doesn't work the other way round.
			// unitsHarvesting.erase(tmpIt);
		}
	}
}

void Building::update(void)
{
	computeWishedResources(wishedResources);
	if (buildingState==DEAD)
		return;
	desiredMaxUnitWorking = desiredNumberOfWorkers();
	updateCallLists();
	updateUnitsWorking();
	updateUnitsHarvesting();
	updateConstructionState();
	if (type->isBuildingSite)
		updateBuildingSite();
}

void Building::setMapDiscovered(void)
{
	assert(type);
	int vr=type->viewingRange;
	if (type->canExchange)
		owner->map->setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVisionExchange);
	else if (type->canFeedUnit)
		owner->map->setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVisionFood);
	else
		owner->map->setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVisionOther);
	owner->map->setMapExploredByBuilding(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->teamNumber);
}

void Building::getResourceCountToRepair(int resources[BASIC_COUNT])
{
	assert(!type->isBuildingSite);
	int repairLevelTypeNum=type->prevLevel;
	BuildingType *repairBt=globalContainer->buildingsTypes.get(repairLevelTypeNum);
	assert(repairBt);
	Sint32 fDestructionRatio=(hp<<FIXED_POINT_SHIFT_16)/getEffectiveMaxHp();
	Sint32 fTotErr=0;
	for (int i=0; i<BASIC_COUNT; i++)
	{
		int fVal=fDestructionRatio*repairBt->maxResource[i];
		int iVal=(fVal>>FIXED_POINT_SHIFT_16);
		fTotErr+=fVal&(int)FIXED_POINT_FRAC_MASK;
		if (fTotErr>=(int)FIXED_POINT_ONE)
		{
			fTotErr-=(int)FIXED_POINT_ONE;
			iVal++;
		}
		resources[i]=repairBt->maxResource[i]-iVal;
	}
}

bool Building::tryToBuildingSiteRoom(void)
{
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;

	int targetLevelTypeNum=BUILDING_LEVEL_NONE;
	if (constructionResultState==UPGRADE)
		targetLevelTypeNum=type->nextLevel;
	else if (constructionResultState==REPAIR)
		targetLevelTypeNum=type->prevLevel;
	else
		assert(false);

	if (targetLevelTypeNum==BUILDING_LEVEL_NONE)
		return false;

	BuildingType *targetBt=globalContainer->buildingsTypes.get(targetLevelTypeNum);
	int newPosX=midPosX+targetBt->decLeft;
	int newPosY=midPosY+targetBt->decTop;

	int newWidth=targetBt->width;
	int newHeight=targetBt->height;

	bool isRoom=owner->map->isFreeForBuilding(newPosX, newPosY, newWidth, newHeight, gid);
	if (isRoom)
	{
		if(constructionResultState == UPGRADE)
			removeForbiddenZoneFromUpgradeArea();

		// OK, we have found enough room to expand our building-site, then we set-up the building-site.
		if (constructionResultState==REPAIR)
		{
			Sint32 fDestructionRatio=(hp<<FIXED_POINT_SHIFT_16)/getEffectiveMaxHp();
			Sint32 fTotErr=0;
			for (int i=0; i<MAX_RESOURCES; i++)
			{
				int fVal=fDestructionRatio*targetBt->maxResource[i];
				int iVal=(fVal>>FIXED_POINT_SHIFT_16);
				fTotErr+=fVal&(int)FIXED_POINT_FRAC_MASK;
				if (fTotErr>=(int)FIXED_POINT_ONE)
				{
					fTotErr-=(int)FIXED_POINT_ONE;
					iVal++;
				}
				resources[i]=iVal;
			}
		}

		if (!type->isVirtual)
		{
			owner->map->setBuilding(posX, posY, type->width, type->height, NOGBID);
			owner->map->setBuilding(newPosX, newPosY, newWidth, newHeight, gid);
		}


		owner->prestige-=type->prestige;
		typeNum=targetLevelTypeNum;
		type=targetBt;
		owner->prestige+=type->prestige;

		//Update the pointer resources to the newly changed type
		updateResourcesPointer();

		buildingState=ALIVE;
		owner->addToStaticAbilitiesLists(this);

		// towers may already have some stone!
		if (constructionResultState==UPGRADE)
			for (int i=0; i<MAX_NB_RESOURCES; i++)
			{
				int res=resources[i];
				int resMax=type->maxResource[i];
				if (res>0 && resMax>0)
				{
					if (res>resMax)
						res=resMax;
					if (verbose)
						printf("using %d resources[%d] for fast constr (hp+=%d)\n", res, i, res*type->hpInc);
					hp+=res*type->hpInc;
					hp = std::min(hp, getEffectiveMaxHp());
				}
			}

		// units
		if (verbose)
			printf("bgid=%d, uses maxUnitWorkingPreferred=%d\n", gid, maxUnitWorkingPreferred);
		maxUnitWorking=maxUnitWorkingPreferred;
		maxUnitInside=type->maxUnitInside;
		updateCallLists();
		updateUnitsWorking();
		// no unit harvesting at that point

		// position
		posX=newPosX;
		posY=newPosY;

		// flag useful :
		unitStayRange=type->defaultUnitStayRange;

		// quality parameters
		// hp=type->hpInit; // (Uint16)

		// preferred parameters
		productionTimeout=type->unitProductionTime;

		totalRatio=0;
		for (int i=0; i<NB_UNIT_TYPE; i++)
		{
			ratio[i]=1;
			totalRatio++;
			percentUsed[i]=0;
		}
	}
	return isRoom;
}

/// Toggle (add or remove) the forbidden zone covering the footprint this building would
/// occupy if it completed its current upgrade. Disperses units so the building site
/// isn't waiting for space when there are lots of units around.
///
/// The post-mutation refresh of displayedForbiddenView is per-client display state
/// (not in Map::checkSum) — it runs only when this building's team is the locally-
/// displayed team, identified via Map::getDisplayedTeam() (mirrored from GameGUI's
/// localTeamNo). updateForbiddenGradient, by contrast, is sim state and runs
/// unconditionally for the owning team.
void Building::modifyForbiddenZoneForUpgradeArea(bool add)
{
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;

	BuildingType *targetBt=globalContainer->buildingsTypes.get(type->nextLevel);
	int newPosX=midPosX+targetBt->decLeft;
	int newPosY=midPosY+targetBt->decTop;
	int newWidth=targetBt->width;
	int newHeight=targetBt->height;

	for(int x=newPosX; x<(newPosX+newWidth); ++x)
	{
		for(int y=newPosY; y<(newPosY+newHeight); ++y)
		{
			if (add)
				owner->map->addForbidden(x, y, owner->teamNumber);
			else
				owner->map->removeForbidden(x, y, owner->teamNumber);
		}
	}
	if(owner->teamNumber == owner->map->getDisplayedTeam())
		owner->map->computeDisplayedForbidden(owner->teamNumber);
	owner->map->updateForbiddenGradient(owner->teamNumber);
}

void Building::addForbiddenZoneToUpgradeArea(void)      { modifyForbiddenZoneForUpgradeArea(true); }
void Building::removeForbiddenZoneFromUpgradeArea(void) { modifyForbiddenZoneForUpgradeArea(false); }



bool Building::isHardSpaceForBuildingSite(void)
{
	return isHardSpaceForBuildingSite(constructionResultState);
}

bool Building::isHardSpaceForBuildingSite(ConstructionResultState requestedState)
{
	int futureBuildingTypeId=BUILDING_LEVEL_NONE;
	if (requestedState==UPGRADE)
		futureBuildingTypeId=type->nextLevel;
	else if (requestedState==REPAIR)
		futureBuildingTypeId=type->prevLevel;
	else
		assert(false);

	if (futureBuildingTypeId==BUILDING_LEVEL_NONE)
		return true;
	BuildingType *bt=globalContainer->buildingsTypes.get(futureBuildingTypeId);
	int x=posX+bt->decLeft-type->decLeft;
	int y=posY+bt->decTop -type->decTop ;
	int w=bt->width;
	int h=bt->height;

	if (bt->isVirtual)
		return true;
	return owner->map->isHardSpaceForBuilding(x, y, w, h, gid);
}

bool Building::fullInside(void)
{
	if ((type->canFeedUnit) && (resources[CORN]<=(int)unitsInside.size()))
		return true;
	else
		return ((signed)unitsInside.size()>=maxUnitInside);
}


int Building::desiredNumberOfWorkers(void)
{
	//If It's virtual, then this building is a flag and always gets
	//full resources
	if(type->isVirtual)
	{
		return maxUnitWorking;
	}
	//Otherwise, this building gets what the user desires, up to a limit of 2 units per 1 needed resource,
	//thus if no resources are needed, then no units will be working here.
	int neededResourcesSum = 0;
	for (size_t ri = 0; ri < MAX_RESOURCES; ri++)
	{
		int neededResources = (type->maxResource[ri] - resources[ri]) / type->multiplierResource[ri];
		if (neededResources > 0)
			neededResourcesSum += neededResources;
	}
	int user_num = maxUnitWorking;
	int max_considering_resources = (WISHED_RESOURCE_NUM * neededResourcesSum) / WISHED_RESOURCE_DEN;
	return std::min(user_num, max_considering_resources);
}


