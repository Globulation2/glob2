/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "Building.h"
#include "BuildingType.h"
#include "Team.h"
#include "Game.h"
#include "Utilities.h"
#include <list>
#include "GlobalContainer.h"

Building::Building(SDL_RWops *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor)
{
	load(stream, types, owner, versionMinor);
}

Building::Building(int x, int y, Uint16 gid, int typeNum, Team *team, BuildingsTypes *types)
{
	// identity
	this->gid=gid;
	owner=team;
	
	// type
	this->typeNum=typeNum;
	type=types->buildingsTypes[typeNum];
	owner->prestige+=type->prestige;

	// construction state
	buildingState=ALIVE;
	// We can only push on map level 0 building-sites !
	// If you want to add higher level building-sites, you have to change the "constructionResultState" to UPGRADE,
	// and set the "buildingState" correctly.
	if (type->isBuildingSite)
		constructionResultState=NEW_BUILDING;
	else
		constructionResultState=NO_CONSTRUCTION;
	

	// units
	maxUnitInside=type->maxUnitInside;
	maxUnitWorking=type->maxUnitWorking;
	maxUnitWorkingLocal=maxUnitWorking;
	maxUnitWorkingPreferred=1;
	lastInsideSubscribe=0;
	lastWorkingSubscribe=0;

	// position
	posX=x;
	posY=y;
	posXLocal=posX;
	posYLocal=posY;

	// flag usefull :
	unitStayRange=type->defaultUnitStayRange;
	unitStayRangeLocal=unitStayRange;

	// building specific :
	for(int i=0; i<NB_RESSOURCES; i++)
		ressources[i]=0;

	// quality parameters
	hp=type->hpInit; // (Uint16)

	// prefered parameters
	productionTimeout=type->unitProductionTime;

	totalRatio=0;
	ratioLocal[0]=ratio[0]=1;
	totalRatio++;
	percentUsed[0]=0;
	for (int i=1; i<NB_UNIT_TYPE; i++)
	{
		ratioLocal[i]=ratio[i]=0;
		//totalRatio++;
		percentUsed[i]=0;
	}
	shootingStep=0;
	shootingCooldown=SHOOTING_COOLDOWN_MAX;

	seenByMask=0;
	
	foodable=0;
	fillable=0;
	for (int i=0; i<NB_UNIT_TYPE; i++)
		zonable[i]=0;
	for (int i=0; i<NB_ABILITY; i++)
		upgrade[i]=0;
	// optimisation parameters
	// FIXME: we don't know it this would be usefull or not !
	// Now, it's not used.
	//Sint32 closestRessourceX[NB_RESSOURCES], closestRessourceY[NB_RESSOURCES];
}

void Building::load(SDL_RWops *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor)
{
	// construction state
	buildingState=(BuildingState)SDL_ReadBE32(stream);
	constructionResultState=(ConstructionResultState)SDL_ReadBE32(stream);

	// identity
	gid=SDL_ReadBE16(stream);
	this->owner=owner;

	// position
	posX=SDL_ReadBE32(stream);
	posY=SDL_ReadBE32(stream);
	posXLocal=posX;
	posYLocal=posY;

	// Flag specific
	unitStayRange=SDL_ReadBE32(stream);
	unitStayRangeLocal=unitStayRange;

	// Building Specific
	for (int i=0; i<NB_RESSOURCES; i++)
		ressources[i]=SDL_ReadBE32(stream);

	// quality parameters
	hp=SDL_ReadBE32(stream);

	// prefered parameters
	productionTimeout=SDL_ReadBE32(stream);
	totalRatio=SDL_ReadBE32(stream);
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		ratioLocal[i]=ratio[i]=SDL_ReadBE32(stream);
		percentUsed[i]=SDL_ReadBE32(stream);
	}

	shootingStep=SDL_ReadBE32(stream);
	shootingCooldown=SDL_ReadBE32(stream);

	// optimisation parameters
	for (int i=0; i<NB_RESSOURCES; i++)
	{
		closestRessourceX[i]=SDL_ReadBE32(stream);
		closestRessourceY[i]=SDL_ReadBE32(stream);
	}

	// type
	typeNum=SDL_ReadBE32(stream);
	type=types->buildingsTypes[typeNum];
	owner->prestige+=type->prestige;
	
	seenByMask=SDL_ReadBE32(stream);
	
	foodable=0;
	fillable=0;
	for (int i=0; i<NB_UNIT_TYPE; i++)
		zonable[i]=0;
	for (int i=0; i<NB_ABILITY; i++)
		upgrade[i]=0;
}

void Building::save(SDL_RWops *stream)
{
	// construction state
	SDL_WriteBE32(stream, (Uint32)buildingState);
	SDL_WriteBE32(stream, (Uint32)constructionResultState);

	// identity
	SDL_WriteBE16(stream, gid);
	// we drop team

	// position
	SDL_WriteBE32(stream, posX);
	SDL_WriteBE32(stream, posY);

	// Flag specific
	SDL_WriteBE32(stream, unitStayRange);

	// Building Specific
	for (int i=0; i<NB_RESSOURCES; i++)
		SDL_WriteBE32(stream, ressources[i]);

	// quality parameters
	SDL_WriteBE32(stream, hp);

	// prefered parameters
	SDL_WriteBE32(stream, productionTimeout);
	SDL_WriteBE32(stream, totalRatio);
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		SDL_WriteBE32(stream, ratio[i]);
		SDL_WriteBE32(stream, percentUsed[i]);
	}

	SDL_WriteBE32(stream, shootingStep);
	SDL_WriteBE32(stream, shootingCooldown);

	// optimisation parameters
	for (int i=0; i<NB_RESSOURCES; i++)
	{
		SDL_WriteBE32(stream, closestRessourceX[i]);
		SDL_WriteBE32(stream, closestRessourceY[i]);
	}

	// type
	SDL_WriteBE32(stream, typeNum);
	// we drop type
	
	SDL_WriteBE32(stream, seenByMask);
}

void Building::loadCrossRef(SDL_RWops *stream, BuildingsTypes *types, Team *owner)
{
	// units
	maxUnitInside=SDL_ReadBE32(stream);
	int nbWorking=SDL_ReadBE32(stream);
	unitsWorking.clear();
	for (int i=0; i<nbWorking; i++)
		unitsWorking.push_front(owner->myUnits[Unit::GIDtoID(SDL_ReadBE16(stream))]);

	int nbWorkingSubscribe=SDL_ReadBE32(stream);
	unitsWorkingSubscribe.clear();
	for (int i=0; i<nbWorkingSubscribe; i++)
		unitsWorkingSubscribe.push_front(owner->myUnits[Unit::GIDtoID(SDL_ReadBE16(stream))]);

	lastWorkingSubscribe=SDL_ReadBE32(stream);

	maxUnitWorking=SDL_ReadBE32(stream);
	maxUnitWorkingPreferred=SDL_ReadBE32(stream);
	maxUnitWorkingLocal=maxUnitWorking;
	int nbInside=SDL_ReadBE32(stream);
	unitsInside.clear();
	for (int i=0; i<nbInside; i++)
		unitsInside.push_front(owner->myUnits[Unit::GIDtoID(SDL_ReadBE16(stream))]);

	int nbInsideSubscribe=SDL_ReadBE32(stream);
	unitsInsideSubscribe.clear();
	for (int i=0; i<nbInsideSubscribe; i++)
		unitsInsideSubscribe.push_front(owner->myUnits[Unit::GIDtoID(SDL_ReadBE16(stream))]);
	lastInsideSubscribe=SDL_ReadBE32(stream);
}

void Building::saveCrossRef(SDL_RWops *stream)
{
	std::list<Unit *>::iterator it;

	// units
	SDL_WriteBE32(stream, maxUnitInside);
	SDL_WriteBE32(stream, unitsWorking.size());
	for (it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
		SDL_WriteBE16(stream, (*it)->gid);

	SDL_WriteBE32(stream, unitsWorkingSubscribe.size());
	for (it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
		SDL_WriteBE16(stream, (*it)->gid);

	SDL_WriteBE32(stream, lastWorkingSubscribe);

	SDL_WriteBE32(stream, maxUnitWorking);
	SDL_WriteBE32(stream, maxUnitWorkingPreferred);
	SDL_WriteBE32(stream, unitsInside.size());
	for (it=unitsInside.begin(); it!=unitsInside.end(); ++it)
		SDL_WriteBE16(stream, (*it)->gid);

	SDL_WriteBE32(stream, unitsInsideSubscribe.size());
	for (it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); ++it)
		SDL_WriteBE16(stream, (*it)->gid);
	SDL_WriteBE32(stream, lastInsideSubscribe);
}

bool Building::isRessourceFull(void)
{
	for (int i=0; i<NB_RESSOURCES; i++)
		if (ressources[i]<type->maxRessource[i])
			return false;
	return true;
}

int Building::neededRessource(void)
{
	float minProportion=1.0;
	int minType=-1;
	int deci=syncRand()%NB_RESSOURCES;
	for (int ib=0; ib<NB_RESSOURCES; ib++)
	{
		int i=(ib+deci)%NB_RESSOURCES;
		int maxr=type->maxRessource[i];
		if (maxr)
		{
			float proportion=((float)ressources[i])/((float)maxr);
			if (proportion<minProportion)
			{
				minProportion=proportion;
				minType=i;
			}
		}
	}
	return minType;
}

int Building::neededRessource(int r)
{
	return (type->maxRessource[r]>ressources[r]);
}

void Building::launchConstruction(void)
{
	if ((buildingState==ALIVE) && (!type->isBuildingSite))
	{
		if (hp<type->hpMax)
		{
			if ((type->lastLevelTypeNum==-1) || !isHardSpaceForBuildingSite(REPAIR))
				return;
			constructionResultState=REPAIR;
		}
		else
		{
			if ((type->nextLevelTypeNum==-1) || !isHardSpaceForBuildingSite(UPGRADE))
				return;
			constructionResultState=UPGRADE;
		}
		
		if (type->unitProductionTime)
			owner->swarms.remove(this);
		if (type->shootingRange)
			owner->turrets.remove(this);
		
		removeSubscribers();
		
		// We remove all units who are going to the building:
		// Notice that the algotithm is not fast but clean.
		std::list<Unit *> unitsToRemove;
		for (std::list<Unit *>::iterator it=unitsInside.begin(); it!=unitsInside.end(); ++it)
		{
			Unit *u=*it;
			int d=u->displacement;
			if ((d!=Unit::DIS_INSIDE)&&(d!=Unit::DIS_ENTERING_BUILDING))
			{
				u->attachedBuilding=NULL;
				u->activity=Unit::ACT_RANDOM;
				u->displacement=Unit::DIS_RANDOM;
				u->needToRecheckMedical=true;
				unitsToRemove.push_front(u);
			}
		}
		for (std::list<Unit *>::iterator it=unitsToRemove.begin(); it!=unitsToRemove.end(); ++it)
		{
			Unit *u=*it;
			unitsInside.remove(u);
		}
		
		buildingState=WAITING_FOR_CONSTRUCTION;
		maxUnitWorkingLocal=0;
		maxUnitWorking=0;
		maxUnitInside=0;
		updateCallLists(); // To remove all units working.
		
		updateConstructionState(); // To switch to a realy building site, if all units have been freed from building.
	}
}

void Building::cancelConstruction(void)
{
	Uint32 recoverTypeNum=typeNum;
	BuildingType *recoverType=type;
	
	if (type->isBuildingSite)
	{
		assert(buildingState==ALIVE);
		int targetLevelTypeNum;
		
		if (constructionResultState==UPGRADE)
			targetLevelTypeNum=type->lastLevelTypeNum;
		else if (constructionResultState==REPAIR)
			targetLevelTypeNum=type->nextLevelTypeNum;
		else
			assert(false);
		
		if (targetLevelTypeNum!=-1)
		{
			recoverTypeNum=targetLevelTypeNum;
			recoverType=globalContainer->buildingsTypes.getBuildingType(targetLevelTypeNum);
		}
		else
			assert(false);
	}
	else if (buildingState==WAITING_FOR_CONSTRUCTION_ROOM)
	{
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
	
	owner->prestige-=type->prestige;
	typeNum=recoverTypeNum;
	type=recoverType;
	owner->prestige+=type->prestige;
	
	posX=midPosX+type->decLeft;
	posY=midPosY+type->decTop;

	if (!type->isVirtual)
		owner->map->setBuilding(posX, posY, type->width, type->height, gid);
	
	if (type->maxUnitWorking)
		maxUnitWorking=maxUnitWorkingPreferred;
	else
		maxUnitWorking=0;
	maxUnitWorkingLocal=maxUnitWorking;
	maxUnitInside=type->maxUnitInside;
	updateCallLists();

	if (hp>=type->hpInit)
		hp=type->hpInit;
	
	productionTimeout=type->unitProductionTime;

	if (type->unitProductionTime)
		owner->swarms.push_front(this);
	if (type->shootingRange)
		owner->turrets.push_front(this);
	
	totalRatio=0;
	
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		ratio[i]=1;
		totalRatio++;
		percentUsed[i]=0;
	}

	int vr=type->viewingRange;
	owner->map->setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVision);
}

void Building::launchDelete(void)
{
	if (buildingState==ALIVE)
	{
		removeSubscribers();
		buildingState=WAITING_FOR_DESTRUCTION;
		maxUnitWorking=0;
		maxUnitWorkingLocal=0;
		maxUnitInside=0;
		updateCallLists();
		
		owner->buildingsWaitingForDestruction.push_front(this);
	}
}

void Building::cancelDelete(void)
{
	buildingState=ALIVE;
	if (type->maxUnitWorking)
		maxUnitWorking=maxUnitWorkingPreferred;
	else
		maxUnitWorking=0;
	maxUnitWorkingLocal=maxUnitWorking;
	maxUnitInside=type->maxUnitInside;
	updateCallLists();
}

void Building::updateConstructionState(void)
{
	if (buildingState==DEAD)
		return;
	
	if ((buildingState==WAITING_FOR_CONSTRUCTION) || (buildingState==WAITING_FOR_CONSTRUCTION_ROOM))
	{
		if (!isHardSpaceForBuildingSite())
		{
			cancelConstruction();
		}
		else if ((unitsWorking.size()==0) && (unitsInside.size()==0) && (unitsWorkingSubscribe.size()==0) && (unitsInsideSubscribe.size()==0))
		{
			buildingState=WAITING_FOR_CONSTRUCTION_ROOM;
			owner->buildingsTryToBuildingSiteRoom.push_front(this);
			//printf("inserted %d in buildingsTryToBuildingSiteRoom\n", gid);
		}
		else
			printf("(%d)Building wait for upgrade, uws=%lu, uis=%lu, uwss=%lu, uiss=%lu.\n", gid, (unsigned long)unitsWorking.size(), (unsigned long)unitsInside.size(), (unsigned long)unitsWorkingSubscribe.size(), (unsigned long)unitsInsideSubscribe.size());
	}
}

void Building::updateCallLists(void)
{
	if (buildingState==DEAD)
		return;
	bool ressourceFull=isRessourceFull();
	if (ressourceFull)
	{
		// Then we don't need anyone more to fill me:
		if (foodable!=2)
		{
			owner->foodable.remove(this);
			foodable=2;
		}
		if (fillable!=2)
		{
			owner->fillable.remove(this);
			fillable=2;
		}
	}
	
	if (unitsWorking.size()<(unsigned)maxUnitWorking)
	{
		// Add itself in the right "call-lists":
		if (!ressourceFull)
		{
			if (foodable!=1 && type->foodable)
			{
				owner->foodable.push_front(this);
				foodable=1;
			}
			if (fillable!=1 && type->fillable)
			{
				owner->fillable.push_front(this);
				fillable=1;
			}
		}
		for (int i=0; i<NB_UNIT_TYPE; i++)
			if (zonable[i]!=1 && type->zonable[i])
			{
				owner->zonable[i].push_front(this);
				zonable[i]=1;
			}
	}
	else
	{
		// delete itself from all Call lists
		if (foodable!=2 && type->foodable)
		{
			owner->foodable.remove(this);
			foodable=2;
		}
		if (fillable!=2 && type->fillable)
		{
			owner->fillable.remove(this);
			fillable=2;
		}
		for (int i=0; i<NB_UNIT_TYPE; i++)
			if (zonable[i]!=2)
			{
				owner->zonable[i].remove(this);
				zonable[i]=2;
			}
		if (maxUnitWorking==0)
		{
			// This is only a special optimisation case:
			for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); it++)
			{
				(*it)->attachedBuilding=NULL;
				(*it)->activity=Unit::ACT_RANDOM;
				(*it)->needToRecheckMedical=true;
			}
			unitsWorking.clear();
		}
		else
			while (unitsWorking.size()>(unsigned)maxUnitWorking)
			{
				int maxDistSquare=0;

				Unit *fu=NULL;
				std::list<Unit *>::iterator ittemp;

				// First choice: free an unit who has a not needed ressource..
				for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
				{
					int newDistSquare=distSquare((*it)->posX, (*it)->posY, posX, posY);
					int r=(*it)->caryedRessource;
					if ( (r>=0) || (!neededRessource(r)) )
						if (newDistSquare>maxDistSquare)
						{
							maxDistSquare=newDistSquare;
							fu=(*it);
							ittemp=it;
						}
				}

				// Second choice: free an unit who has no ressource..
				if (fu==NULL)
					for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
					{
						int newDistSquare=distSquare((*it)->posX, (*it)->posY, posX, posY);
						int r=(*it)->caryedRessource;
						if (r<0)
							if (newDistSquare>maxDistSquare)
							{
								maxDistSquare=newDistSquare;
								fu=(*it);
								ittemp=it;
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
					//printf("Building::update::unitsWorking::fu->gid=%d\n", fu->gid);
					// We free the unit.
					fu->activity=Unit::ACT_RANDOM;
					// fu->displacement=Unit::DIS_RANDOM; TODO: why was this here?

					unitsWorking.erase(ittemp);
					//update(); // TODO: why was this here?
					fu->attachedBuilding=NULL;
					fu->needToRecheckMedical=true;
				}
				else
					break;
			}
	}

	if ((signed)unitsInside.size()<maxUnitInside)
	{
		// Add itself in the right "call-lists":
		for (int i=0; i<NB_ABILITY; i++)
			if (upgrade[i]!=1 && type->upgrade[i])
			{
				owner->upgrade[i].push_front(this);
				upgrade[i]=1;
			}

		// this is for food handling
		if (type->canFeedUnit)
			if (ressources[CORN]>(int)unitsInside.size())
				owner->canFeedUnit.push_front(this);
			else
				owner->canFeedUnit.remove(this);

		// this is for Unit headling
		if (type->canHealUnit)
			owner->canHealUnit.push_front(this);
	}
	else
	{
		// delete itself from all Call lists
		for (int i=0; i<NB_ABILITY; i++)
			if (upgrade[i]!=2 && type->upgrade[i])
			{
				owner->upgrade[i].remove(this);
				upgrade[i]=2;
			}

		if (type->canFeedUnit)
			owner->canFeedUnit.remove(this);
		if (type->canHealUnit)
			owner->canHealUnit.remove(this);
	}
}

void Building::updateBuildingSite(void)
{
	assert(type->isBuildingSite);
	
	if (isRessourceFull() && (buildingState!=WAITING_FOR_DESTRUCTION))
	{
		// we really uses the resources of the buildingsite:
		for(int i=0; i<NB_RESSOURCES; i++)
			ressources[i]-=type->maxRessource[i];

		owner->prestige-=type->prestige;
		typeNum=type->nextLevelTypeNum;
		type=globalContainer->buildingsTypes.getBuildingType(type->nextLevelTypeNum);
		assert(constructionResultState!=NO_CONSTRUCTION);
		constructionResultState=NO_CONSTRUCTION;
		owner->prestige+=type->prestige;

		// we don't need any worker any more

		// Notice that we could avoid freeing thoses units,
		// this would keep the units working to the same building,
		// and then ensure that all newly contructed food building to
		// be filled (at least start to be filled).

		for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); it++)
		{
			(*it)->attachedBuilding=NULL;
			(*it)->activity=Unit::ACT_RANDOM;
			(*it)->needToRecheckMedical=true;
		}
		unitsWorking.clear();

		if (type->maxUnitWorking)
			maxUnitWorking=maxUnitWorkingPreferred;
		else
			maxUnitWorking=0;
		maxUnitWorkingLocal=maxUnitWorking;

		// The working units still works for us, but
		// we don't have any unit in buildings
		assert(unitsInside.size()==0);
		maxUnitInside=type->maxUnitInside;

		hp=type->hpInit;

		productionTimeout=type->unitProductionTime;
		if (type->unitProductionTime)
			owner->swarms.push_front(this);

		if (type->shootingRange)
			owner->turrets.push_front(this);

		// TODO: DUNNO : when do we update closestRessourceXY[] ?
		int vr=type->viewingRange;
		owner->map->setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVision);
		owner->setEvent(getMidX(), getMidY(), Team::BUILDING_FINISHED_EVENT, gid);

		// we need to do an update again
		updateCallLists();
	}
}

void Building::update(void)
{
	if (buildingState==DEAD)
		return;

	updateCallLists();
	updateConstructionState();
	if (type->isBuildingSite)
		updateBuildingSite();
}

bool Building::tryToBuildingSiteRoom(void)
{
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;

	int targetLevelTypeNum;
	if (constructionResultState==UPGRADE)
		targetLevelTypeNum=type->nextLevelTypeNum;
	else if (constructionResultState==REPAIR)
		targetLevelTypeNum=type->lastLevelTypeNum;
	else
		assert(false);
	
	BuildingType *nextBt=globalContainer->buildingsTypes.getBuildingType(targetLevelTypeNum);
	int newPosX=midPosX+nextBt->decLeft;
	int newPosY=midPosY+nextBt->decTop;

	int newWidth=nextBt->width;
	int newHeight=nextBt->height;

	bool isRoom=owner->map->isFreeForBuilding(newPosX, newPosY, newWidth, newHeight, gid);
	if (isRoom)
	{
		// OK, we have found enough room to expand our building-site, then we set-up the building-site.
		if (constructionResultState==REPAIR)
		{
			float destructionRatio = (float)hp/(float)type->hpMax;
			float fTotErr=0;
			for (int i=0; i<NB_RESSOURCES; i++)
			{
				float fVal=destructionRatio*(float)nextBt->maxRessource[i];
				int iVal=(int)fVal;
				fTotErr+=fVal-(float)iVal;
				if (fTotErr>1)
				{
					fTotErr-=1;
					iVal++;
				}
				ressources[i]=iVal;
			}
		}
		
		if (!type->isVirtual)
		{
			owner->map->setBuilding(posX, posY, type->decLeft, type->decLeft, NOGBID);
			owner->map->setBuilding(newPosX, newPosY, newWidth, newHeight, gid);
		}

		
		owner->prestige-=type->prestige;
		typeNum=targetLevelTypeNum;
		type=nextBt;
		owner->prestige+=type->prestige;

		buildingState=ALIVE;

		// units
		//printf("%d uses maxUnitWorkingPreferred=%d\n", gid, maxUnitWorkingPreferred);
		maxUnitWorking=maxUnitWorkingPreferred;
		maxUnitWorkingLocal=maxUnitWorking;
		maxUnitInside=type->maxUnitInside;
		updateCallLists();

		// position
		posX=newPosX;
		posY=newPosY;
		posXLocal=posX;
		posYLocal=posY;

		// flag usefull :
		unitStayRange=type->defaultUnitStayRange;
		unitStayRangeLocal=unitStayRange;

		// quality parameters
		// hp=type->hpInit; // (Uint16)

		// prefered parameters
		productionTimeout=type->unitProductionTime;

		totalRatio=0;
		for (int i=0; i<NB_UNIT_TYPE; i++)
		{
			ratio[i]=1;
			totalRatio++;
			percentUsed[i]=0;
		}

		/*zzz if (foodable!=2)
		{
			owner->foodable.remove(this);
			foodable=2;
		}
		if (fillable!=2)
		{
			owner->fillable.remove(this);
			fillable=2;
		}
		for (int i=0; i<NB_UNIT_TYPE; i++)
			if (zonable[i]!=2)
			{
				owner->zonable[i].remove(this);
				zonable[i]=2;
			}
		update();*/
	}
	return isRoom;
}

bool Building::isHardSpaceForBuildingSite(void)
{
	return isHardSpaceForBuildingSite(constructionResultState);
}

bool Building::isHardSpaceForBuildingSite(ConstructionResultState constructionResultState)
{
	int tltn;
	if (constructionResultState==UPGRADE)
		tltn=type->nextLevelTypeNum;
	else if (constructionResultState==REPAIR)
		tltn=type->lastLevelTypeNum;
	else
		assert(false);
	
	if (tltn==-1)
		return true;
	BuildingType *bt=globalContainer->buildingsTypes.getBuildingType(tltn);
	int x=posX+bt->decLeft-type->decLeft;
	int y=posY+bt->decTop -type->decTop ;
	int w=bt->width;
	int h=bt->height;

	if (bt->isVirtual)
		return true;
	return owner->map->isHardSpaceForBuilding(x, y, w, h, gid);
}


void Building::step(void)
{
	assert(false);
	// NOTE : Unit needs to update itself when it is in a building
}

void Building::removeSubscribers(void)
{
	for (std::list<Unit *>::iterator  it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
	{
		(*it)->attachedBuilding=NULL;
		(*it)->subscribed=false;
		(*it)->activity=Unit::ACT_RANDOM;
		(*it)->needToRecheckMedical=true;
	}
	unitsWorkingSubscribe.clear();

	for (std::list<Unit *>::iterator  it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); ++it)
	{
		(*it)->attachedBuilding=NULL;
		(*it)->subscribed=false;
		(*it)->activity=Unit::ACT_RANDOM;
		(*it)->needToRecheckMedical=true;
	}
	unitsInsideSubscribe.clear();
}

bool Building::fullWorking(void)
{
	return ((signed)unitsWorking.size()>=maxUnitWorking);
}

bool Building::fullInside(void)
{
	if ((type->canFeedUnit) && (ressources[CORN]<=(int)unitsInside.size()))
		return true;
	else
		return ((signed)unitsInside.size()>=maxUnitInside);
}

void Building::subscribeToBringRessourcesStep()
{
	std::list<Unit *>::iterator it;

	lastWorkingSubscribe++;
	if (fullWorking())
	{
		for (it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
		{
			(*it)->attachedBuilding=NULL;
			(*it)->subscribed=false;
			(*it)->activity=Unit::ACT_RANDOM;
			(*it)->needToRecheckMedical=true;
		}
		unitsWorkingSubscribe.clear();
		return;
	}
	
	if (lastWorkingSubscribe>32)
	{
		//is it usefull? lastWorkingSubscribe=0;
		if ((signed)unitsWorking.size()<maxUnitWorking)
		{
			int minValue=owner->map->getW()*owner->map->getW();
			Unit *choosen=NULL;
			Map *map=owner->map;
			/* To choose a good unit, we get a composition of things:
			1-the closest the unit is, the better it is.
			2-the less the unit is hungry, the better it is.
			3-if the unit has a needed ressource, this is better.
			4-if the unit as a not needed ressource, this is worse.
			5-if the unit is close of a needed ressource, this is better
			*/
			for (it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
			{
				Unit *unit=(*it);
				int r=unit->caryedRessource;
				// The following "10" is totaly arbitrary between [2..100]
				int hungry=unit->hungry/(10*unit->race->unitTypes[0][0].hungryness);
				int x=unit->posX;
				int y=unit->posY;
				hungry*=hungry;
				if ((r>=0)&& neededRessource(r))
				{
					int dist=map->warpDistSquare(x, y, posX, posY);
					int value=dist-hungry;
					//printf("d(%x) dist=%d, hungry=%d, value=%d, r=%d.\n", (int)unit, dist, hungry, value, r);
					unit->destinationPurprose=r;
					if (value<minValue)
					{
						minValue=value;
						choosen=unit;
					}
				}
			}
			
			if (choosen==NULL)
				for (it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
				{
					Unit *unit=(*it);
					// The following "10" is totaly arbitrary between [2..100]
					int hungry=unit->hungry/(10*unit->race->unitTypes[0][0].hungryness);
					int x=unit->posX;
					int y=unit->posY;
					int dx, dy;
					int r=-1;
					if (map->nearestRessource(x, y, (RessourceType *)&r, &dx, &dy) && neededRessource(r))
					{
						int dist=map->warpDistSquare(dx, dy, posX, posY);
						dist+=(x-dx)*(x-dx)+(y-dy)*(y-dy);
						int value=dist-hungry;
						//printf("i(%x) dist=%d, hungry=%d, value=%d, r=%d.\n", (int)unit, dist, hungry, value, r);
						unit->destinationPurprose=r;
						if (value<minValue)
						{
							minValue=value;
							choosen=unit;
						}
					}
				}
			
			if (choosen==NULL)
			{
				for (it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
				{
					Unit *unit=(*it);
					// The following "10" is totaly arbitrary between [2..100]
					int hungry=unit->hungry/(10*unit->race->unitTypes[0][0].hungryness);
					int dist=map->warpDistSquare(unit->posX, unit->posY, posX, posY);
					int value=dist-hungry;
					//printf("u(%x) dist=%d, hungry=%d, value=%d\n", (int)unit, dist, hungry, value);
					if (value<minValue)
					{
						minValue=value;
						choosen=unit;
					}
				}
			}

			if (choosen)
			{
				//printf("f(%x) choosen.\n", (int)choosen);
				unitsWorkingSubscribe.remove(choosen);
				if (!neededRessource(choosen->destinationPurprose))
				{
					//this does works but is less efficient: choosen->destinationPurprose=neededRessource();
					
					printf("C-B(%x)gid=(%d), choosen=(%x) Ugid=(%d), dp=(%d), nr=(%d, %d, %d, %d).\n", (int)this, gid, (int)choosen, (int)choosen->gid, choosen->destinationPurprose, neededRessource(0), neededRessource(1), neededRessource(2), neededRessource(3));
					// This unit may no more be needed here.
					// Let's remove it from this subscribing list.
					lastWorkingSubscribe=0;
					
					choosen->attachedBuilding=NULL;
					choosen->subscribed=false;
					choosen->activity=Unit::ACT_RANDOM;
					choosen->needToRecheckMedical=true;
					
					return;
				}
				else
				{
					unitsWorking.push_back(choosen);
					choosen->unsubscribed();
					updateCallLists();
				}
			}
		}
		
		if ((signed)unitsWorking.size()>=maxUnitWorking)
		{
			for (it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
			{
				(*it)->attachedBuilding=NULL;
				(*it)->subscribed=false;
				(*it)->activity=Unit::ACT_RANDOM;
				(*it)->needToRecheckMedical=true;
			}
			unitsWorkingSubscribe.clear();
		}
	}
}

void Building::subscribeForFlagingStep()
{
	std::list<Unit *>::iterator it;
	lastWorkingSubscribe++;
	if (fullWorking())
	{
		for (it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
		{
			(*it)->attachedBuilding=NULL;
			(*it)->subscribed=false;
			(*it)->activity=Unit::ACT_RANDOM;
			(*it)->needToRecheckMedical=true;
		}
		unitsWorkingSubscribe.clear();
		return;
	}
	
	if (lastWorkingSubscribe>32)
	{
		lastWorkingSubscribe=0;
		if ((signed)unitsWorking.size()<maxUnitWorking)
		{
			int minValue=owner->map->getW()*owner->map->getW();
			Unit *choosen=NULL;
			
			/* To choose a good unit, we get a composition of things:
			1-the closest the unit is, the better it is.
			2-the less the unit is hungry, the better it is.
			2-the more hp the unit has, the better it is.
			*/
			for (it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
			{
				Unit *unit=(*it);
				// The following "10" is totaly arbitrary between [2..100]
				int hungry=unit->hungry/(10*unit->race->unitTypes[0][0].hungryness);
				int hp=(10*unit->hp)/unit->race->unitTypes[0][0].performance[HP];
				int x=unit->posX;
				int y=unit->posY;
				hungry*=hungry;
				hp*=hp;
				int dist=owner->map->warpDistSquare(x, y, posX, posY);
				int value=dist-hungry+hp;
				//printf("d(%x) dist=%d, hungry=%d, hp=%d, value=%d\n", (int)unit, dist, hungry, hp, value);
				if (value<minValue)
				{
					minValue=value;
					choosen=unit;
				}
				
			}
			
			if (choosen)
			{
				//printf("f(%x) choosen.\n", (int)choosen);
				unitsWorkingSubscribe.remove(choosen);
				unitsWorking.push_back(choosen);
				choosen->unsubscribed();
				updateCallLists();
			}
		}
	}
}

void Building::subscribeForInsideStep()
{
	std::list<Unit *>::iterator it;
	lastInsideSubscribe++;
	if (fullInside())
	{
		for (it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
		{
			(*it)->attachedBuilding=NULL;
			(*it)->subscribed=false;
			(*it)->activity=Unit::ACT_RANDOM;
			(*it)->needToRecheckMedical=true;
		}
		unitsInsideSubscribe.clear();
		return;
	}
	
	if (lastInsideSubscribe>32)
	{
		if ((signed)unitsInside.size()<maxUnitInside)
		{
			int mindist=owner->map->getW()*owner->map->getW();
			Unit *u=NULL;
			for (it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
			{
				int dist=owner->map->warpDistSquare((*it)->posX, (*it)->posY, posX, posY);
				if (dist<mindist)
				{
					mindist=dist;
					u=*it;
				}
			}
			if (u)
			{
				unitsInsideSubscribe.remove(u);
				unitsInside.push_back(u);
				u->unsubscribed();
				updateCallLists();
			}
		}
		
		if ((signed)unitsInside.size()>=maxUnitInside)
		{
			for (it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
			{
				(*it)->attachedBuilding=NULL;
				(*it)->subscribed=false;
				(*it)->activity=Unit::ACT_RANDOM;
				(*it)->needToRecheckMedical=true;
			}
			unitsInsideSubscribe.clear();
		}
	}
}

void Building::swarmStep(void)
{
	// increase HP
	if (hp<type->hpMax)
		hp++;
	assert(NB_UNIT_TYPE==3);
	if ((ressources[CORN]>=type->ressourceForOneUnit)&&(ratio[0]|ratio[1]|ratio[2]))
		productionTimeout--;

	if (productionTimeout<0)
	{
		// We find the kind of unit we have to create:
		float proportion;
		float minProportion=1.0;
		int minType=-1;
		for (int i=0; i<NB_UNIT_TYPE; i++)
		{
			if (ratio[i]!=0)
			{
				proportion=((float)percentUsed[i])/((float)ratio[i]);
				if (proportion<=minProportion)
				{
					minProportion=proportion;
					minType=i;
				}
			}
		}

		if (minType==-1)
			minType=0;
		assert(minType>=0);
		assert(minType<NB_UNIT_TYPE);
		if (minType<0 || minType>=NB_UNIT_TYPE)
			minType=0;

		// help printf
		/*printf("SWARM : prod ratio %d/%d/%d - pused %d/%d/%d, producing : %d\n",
			ratio[0], ratio[1], ratio[2],
			percentUsed[0], percentUsed[1], percentUsed[2],
			minType);*/

		// We get the unit UnitType:
		int posX, posY, dx, dy;
		UnitType *ut=owner->race.getUnitType(minType, 0);

		// Is there a place to exit ?
		bool exitFound;
		if (ut->performance[FLY])
			exitFound=findAirExit(&posX, &posY, &dx, &dy);
		else
			exitFound=findGroundExit(&posX, &posY, &dx, &dy, ut->performance[SWIM]);
		if (exitFound)
		{
			Unit * u=owner->game->addUnit(posX, posY, owner->teamNumber, minType, 0, 0, dx, dy);
			if (u)
			{
				ressources[CORN]-=type->ressourceForOneUnit;
				updateCallLists();
				
				u->activity=Unit::ACT_RANDOM;
				u->displacement=Unit::DIS_RANDOM;
				u->movement=Unit::MOV_EXITING_BUILDING;
				u->speed=u->performance[u->action];

				productionTimeout=type->unitProductionTime;

				// We update percentUsed[]
				percentUsed[minType]++;

				bool allDone=true;
				for (int i=0; i<NB_UNIT_TYPE; i++)
					if (percentUsed[i]<ratio[i])
						allDone=false;

				if (allDone)
					for (int i=0; i<NB_UNIT_TYPE; i++)
						percentUsed[i]=0;
			}
			else
				printf("WARNING, no more UNIT ID free for team %d\n", owner->teamNumber);
		}
	}
}

void Building::turretStep(void)
{
	if (shootingCooldown>0)
	{
		shootingCooldown-=type->shootRythme;
		return;
	}

	assert(type->width ==2);
	assert(type->height==2);
	
	int range=type->shootingRange;
	shootingStep=(shootingStep+1)&0x7;
	
	Uint32 enemies=owner->enemies;
	bool targetFound=false;
	int bestTime=256;
	Map *map=owner->map;
	assert(map);

	int targetX, targetY;
	int bestTargetX=0, bestTargetY=0;
	for (int i=0; i<=range && !targetFound; i++)
		for (int j=0; j<=i && !targetFound; j++)
			//for (int k=0; k<8; k++)
			{
				switch (shootingStep)
				{
				case 0:
				targetX=posX-j;
				targetY=posY-i;
				break;
				case 1:
				targetX=posX+j+1;
				targetY=posY-i;
				break;
				case 2:
				targetX=posX-j;
				targetY=posY+i+1;
				break;
				case 3:
				targetX=posX+j+1;
				targetY=posY+i+1;
				break;
				case 4:
				targetX=posX-i;
				targetY=posY-j;
				break;
				case 5:
				targetX=posX+i+1;
				targetY=posY-j;
				break;
				case 6:
				targetX=posX-i;
				targetY=posY+j+1;
				break;
				case 7:
				targetX=posX+i+1;
				targetY=posY+j+1;
				break;
				default:
				assert(false);
				}
				int targetGUID=map->getGroundUnit(targetX, targetY);
				if (targetGUID!=NOGUID)
				{
					Sint32 otherTeam=Unit::GIDtoTeam(targetGUID);
					Sint32 targetID=Unit::GIDtoID(targetGUID);
					Uint32 otherTeamMask=1<<otherTeam;
					if (enemies&otherTeamMask)
					{
						Unit *testUnit=owner->game->teams[otherTeam]->myUnits[targetID];
						int targetTime;
						if (testUnit->movement==Unit::MOV_ATTACKING_TARGET)
							targetTime=0;
						else
							targetTime=(256-testUnit->delta)/testUnit->speed;
						if (targetTime<bestTime)
						{
							bestTime=targetTime;
							bestTargetX=targetX;
							bestTargetY=targetY;
							targetFound=true;
						}
						//printf("found unit target found: (%d, %d) t=%d, id=%d \n", targetX, targetY, otherTeam, Unit::UIDtoID(targetUID));
						//break;
					}
				}
				int targetGBID=map->getBuilding(targetX, targetY);
				if (targetGBID!=NOGBID)
				{
					int otherTeam=Building::GIDtoTeam(targetGBID);
					int otherID=Building::GIDtoID(targetGBID);
					Uint32 otherTeamMask=1<<otherTeam;
					if (enemies&otherTeamMask)
					{
						Building *b=owner->game->teams[otherTeam]->myBuildings[otherID];
						if (b->hp>1 || !b->type->isBuildingSite)
						{
							targetFound=true;
							bestTargetX=targetX;
							bestTargetY=targetY;
							break;
						}
						else if (bestTime==256)
						{
							targetFound=true;
							bestTargetX=targetX;
							bestTargetY=targetY;
							bestTime=255;
						}
					}
				}
			}

	int midX=getMidX();
	int midY=getMidY();
	if (targetFound)
	{
		shootingStep=0;
		
		//printf("%d found target found: (%d, %d) \n", gid, targetX, targetY);
		Sector *s=owner->map->getSector(midX, midY);

		int px, py;
		px=((posX)<<5)+((type->width)<<4);
		py=((posY)<<5)+((type->height)<<4);

		int speedX, speedY, ticksLeft;

		// TODO : shall we really uses shootSpeed ?
		// FIXME : is it correct this way ? Is there a function for this ?
		int dpx=(bestTargetX*32)+16-4-px; // 4 is the half size of the bullet
		int dpy=(bestTargetY*32)+16-4-py;
		//printf("%d insert: dp=(%d, %d).\n", gid, dpx, dpy);
		if (dpx>(map->getW()<<4))
			dpx=dpx-(map->getW()<<5);
		if (dpx<-(map->getW()<<4))
			dpx=dpx+(map->getW()<<5);
		if (dpy>(map->getH()<<4))
			dpy=dpy-(map->getH()<<5);
		if (dpy<-(map->getH()<<4))
			dpy=dpy+(map->getH()<<5);


		int mdp;

		assert(dpx);
		assert(dpy);
		if (abs(dpx)>abs(dpy)) //we avoid a square root, since all ditances are squares lengthed.
		{
			mdp=abs(dpx);
			speedX=((dpx*type->shootSpeed)/(mdp<<8));
			speedY=((dpy*type->shootSpeed)/(mdp<<8));
			if (speedX)
				ticksLeft=abs(mdp/speedX);
			else
			{
				assert(false);
				return;
			}
		}
		else
		{
			mdp=abs(dpy);
			speedX=((dpx*type->shootSpeed)/(mdp<<8));
			speedY=((dpy*type->shootSpeed)/(mdp<<8));
			if (speedY)
				ticksLeft=abs(mdp/speedY);
			else
			{
				assert(false);
				return;
			}
		}

		Bullet *b=new Bullet(px, py, speedX, speedY, ticksLeft, type->shootDamage, bestTargetX, bestTargetY);
		//printf("%d insert: pos=(%d, %d), target=(%d, %d), p=(%d, %d), dp=(%d, %d), mdp=%d, speed=(%d, %d).\n", gid, posX, posY, targetX, targetY, px, py, dpx, dpy, mdp, speedX, speedY);
		//printf("%d insert: (px=%d, py=%d, sx=%d, sy=%d, tl=%d, sd=%d) \n", gid, px, py, speedX, speedY, ticksLeft, type->shootDamage);
		s->bullets.push_front(b);

		shootingCooldown=SHOOTING_COOLDOWN_MAX;
	}

}

void Building::kill(void)
{
	if (buildingState==DEAD)
		return;
	
	for (std::list<Unit *>::iterator it=unitsInside.begin(); it!=unitsInside.end(); ++it)
	{
		Unit *u=*it;
		if (u->displacement==Unit::DIS_INSIDE)
			u->isDead=true;

		if (u->displacement==Unit::DIS_ENTERING_BUILDING)
		{
			if (!u->performance[FLY])
				owner->map->setGroundUnit(u->posX-u->dx, u->posY-u->dy, NOGUID);
			//printf("(%x)Building:: Unit(uid%d)(id%d) killed while entering. dis=%d, mov=%d, ab=%x, ito=%d \n",this, u->gid, Unit::UIDtoID(u->gid), u->displacement, u->movement, (int)u->attachedBuilding, u->insideTimeout);
			u->isDead=true;
		}
		u->attachedBuilding=NULL;
		u->activity=Unit::ACT_RANDOM;
		u->displacement=Unit::DIS_RANDOM;
		(*it)->needToRecheckMedical=true;
	}
	unitsInside.clear();

	for (std::list<Unit *>::iterator  it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
	{
		(*it)->attachedBuilding=NULL;
		(*it)->activity=Unit::ACT_RANDOM;
		(*it)->needToRecheckMedical=true;
	}
	unitsWorking.clear();
	
	removeSubscribers();
	
	maxUnitWorking=0;
	maxUnitWorkingLocal=0;
	maxUnitInside=0;
	updateCallLists();

	if (!type->isVirtual)
		owner->map->setBuilding(posX, posY, type->width, type->height, NOGBID);
	
	buildingState=DEAD;
	owner->prestige-=type->prestige;
	
	owner->buildingsToBeDestroyed.push_front(this);
}

int Building::getMidX(void)
{
	return ((posX-type->decLeft)&owner->map->getMaskW());
}

int Building::getMidY(void)
{
	return ((posY-type->decTop)&owner->map->getMaskH());
}

bool Building::findAirExit(int *posX, int *posY, int *dx, int *dy)
{
	for (int xi=this->posX; xi<this->posX+type->width; xi++)
		for (int yi=this->posY; yi<this->posY+type->height; yi++)
			if (owner->map->isFreeForAirUnit(xi, yi))
			{
				*posX=xi;
				*posY=yi;
				int tdx=xi-getMidX();
				int tdy=yi-getMidY();
				*dx=sign(tdx);
				if (tdx<0)
					*dx=-1;
				else if (tdx==0)
					*dx=0;
				else
					*dx=1;
				
				if (tdy<0)
					*dy=-1;
				else if (tdy==0)
					*dy=0;
				else
					*dy=1;
				return true;
			}
	return false;
}

bool Building::findGroundExit(int *posX, int *posY, int *dx, int *dy, bool canSwim)
{
	int testX, testY;
	int exitQuality=0;
	int oldQuality;
	int exitX=0, exitY=0;
	Uint32 me=owner->me;
	
	//if (exitQuality<4)
	{
		testY=this->posY-1;
		oldQuality=0;
		for (testX=this->posX-1; (testX<=this->posX+type->width) ; testX++)
			if (owner->map->isFreeForGroundUnit(testX, testY, canSwim, me))
			{
				if (owner->map->isFreeForGroundUnit(testX, testY-1, canSwim, me))
					oldQuality++;
				if (owner->map->isRessource(testX, testY-1))
				{
					if (exitQuality<1+oldQuality)
					{
						exitQuality=1+oldQuality;
						exitX=testX;
						exitY=testY;
					}
					oldQuality=0;
				}
				else
				{
					if (exitQuality<2+oldQuality)
					{
						exitQuality=2+oldQuality;
						exitX=testX;
						exitY=testY;
					}
					oldQuality=1;
				}
			}
	}
	if (exitQuality<4)
	{
		testY=this->posY+type->height;
		oldQuality=0;
		for (testX=this->posX-1; (testX<=this->posX+type->width) ; testX++)
			if (owner->map->isFreeForGroundUnit(testX, testY, canSwim, me))
			{
				if (owner->map->isFreeForGroundUnit(testX, testY+1, canSwim, me))
					oldQuality++;
				if (owner->map->isRessource(testX, testY+1))
				{
					if (exitQuality<1+oldQuality)
					{
						exitQuality=1+oldQuality;
						exitX=testX;
						exitY=testY;
					}
					oldQuality=0;
				}
				else
				{
					if (exitQuality<2+oldQuality)
					{
						exitQuality=2+oldQuality;
						exitX=testX;
						exitY=testY;
					}
					oldQuality=1;
				}
			}
	}
	if (exitQuality<4)
	{
		oldQuality=0;
		testX=this->posX-1;
		for (testY=this->posY-1; (testY<=this->posY+type->height) ; testY++)
			if (owner->map->isFreeForGroundUnit(testX, testY, canSwim, me))
			{
				if (owner->map->isFreeForGroundUnit(testX-1, testY, canSwim, me))
					oldQuality++;
				if (owner->map->isRessource(testX-1, testY))
				{
					if (exitQuality<1+oldQuality)
					{
						exitQuality=1+oldQuality;
						exitX=testX;
						exitY=testY;
					}
					oldQuality=0;
				}
				else
				{
					if (exitQuality<2+oldQuality)
					{
						exitQuality=2+oldQuality;
						exitX=testX;
						exitY=testY;
					}
					oldQuality=1;
				}
			}
	}
	if (exitQuality<4)
	{
		oldQuality=0;
		testX=this->posX+type->width;
		for (testY=this->posY-1; (testY<=this->posY+type->height) ; testY++)
			if (owner->map->isFreeForGroundUnit(testX, testY, canSwim, me))
			{
				if (owner->map->isFreeForGroundUnit(testX+1, testY, canSwim, me))
					oldQuality++;
				if (owner->map->isRessource(testX+1, testY))
				{
					if (exitQuality<1+oldQuality)
					{
						exitQuality=1+oldQuality;
						exitX=testX;
						exitY=testY;
					}
					oldQuality=0;
				}
				else
				{
					if (exitQuality<2+oldQuality)
					{
						exitQuality=2+oldQuality;
						exitX=testX;
						exitY=testY;
					}
					oldQuality=1;
				}
			}
	}
	if (exitQuality>0)
	{
		bool shouldBeTrue=owner->map->doesPosTouchBuilding(exitX, exitY, gid, dx, dy);
		assert(shouldBeTrue);
		*dx=-*dx;
		*dy=-*dy;
		*posX=exitX & owner->map->getMaskW();
		*posY=exitY & owner->map->getMaskH();
		return true;
	}
	return false;
}

void Building::computeFlagStat(int *goingTo, int *onSpot)
{
	*goingTo=0;
	*onSpot=0;

	for (std::list<Unit *>::iterator unitIt=unitsWorking.begin(); unitIt!=unitsWorking.end(); ++unitIt)
	{
		Unit *unit=*unitIt;
		if (unit->displacement==Unit::DIS_GOING_TO_FLAG)
			(*goingTo)++;
		else if (unit->displacement==Unit::DIS_ATTACKING_AROUND)
			(*onSpot)++;
		else if (unit->displacement==Unit::DIS_REMOVING_BLACK_AROUND)
			(*onSpot)++;
		else if (unit->displacement==Unit::DIS_CLEARING_RESSOURCES)
			(*onSpot)++;
	}
}

Sint32 Building::GIDtoID(Uint16 gid)
{
	assert(gid<32768);
	return gid%1024;
}

Sint32 Building::GIDtoTeam(Uint16 gid)
{
	assert(gid<32768);
	return gid/1024;
}

Uint16 Building::GIDfrom(Sint32 id, Sint32 team)
{
	assert(id<1024);
	assert(team<32);
	return id+team*1024;
}

Sint32 Building::checkSum()
{
	int cs=0;
	
	cs^=typeNum;

	cs^=buildingState;

	cs^=maxUnitWorking;
	cs^=maxUnitWorkingPreferred;
	cs^=unitsWorking.size();
	cs^=maxUnitInside;
	cs^=unitsInside.size();
	
	cs^=gid;

	cs^=posX;
	cs^=posY;

	cs^=unitStayRange;

	for (int i=0; i<NB_RESSOURCES; i++)
		cs^=ressources[i];

	cs^=hp;

	cs^=productionTimeout;
	cs^=totalRatio;
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		cs^=ratio[i];
		cs^=percentUsed[i];
		cs=(cs<<31)|(cs>>1);
	}

	cs^=shootingStep;
	cs^=shootingCooldown;
	
	return cs;
}



