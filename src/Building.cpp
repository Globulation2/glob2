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
#include "GlobalContainer.h"
#include <list>
#include <math.h>
#include "LogFileManager.h"

Building::Building(SDL_RWops *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor)
{
	for (int i=0; i<2; i++)
	{
		globalGradient[i]=NULL;
		localRessources[i]=NULL;
	}
	logFile = globalContainer->logFileManager->getFile("Building.log");
	load(stream, types, owner, versionMinor);
}

Building::Building(int x, int y, Uint16 gid, int typeNum, Team *team, BuildingsTypes *types)
{
	logFile = globalContainer->logFileManager->getFile("Building.log");
	
	// identity
	this->gid=gid;
	owner=team;
	
	// type
	this->typeNum=typeNum;
	type=types->get(typeNum);
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
	for(int i=0; i<MAX_NB_RESSOURCES; i++)
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

	receiveRessourceMask=0;
	sendRessourceMask=0;
	receiveRessourceMaskLocal=0;
	sendRessourceMaskLocal=0;

	shootingStep=0;
	shootingCooldown=SHOOTING_COOLDOWN_MAX;

	seenByMask=0;
	
	subscribeForInside=0;
	subscribeToBringRessources=0;
	subscribeForFlaging=0;
	canFeedUnit=0;
	canHealUnit=0;
	foodable=0;
	fillable=0;
	for (int i=0; i<NB_UNIT_TYPE; i++)
		zonable[i]=0;
	for (int i=0; i<NB_ABILITY; i++)
		upgrade[i]=0;
	
	for (int i=0; i<2; i++)
	{
		globalGradient[i]=NULL;
		localRessources[i]=NULL;
		dirtyLocalGradient[i]=true;
		locked[i]=false;
	}
	localRessourcesCleanTime=0;
	
	verbose=false;
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
	for (int i=0; i<MAX_NB_RESSOURCES; i++)
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

	receiveRessourceMask=SDL_ReadBE32(stream);
	sendRessourceMask=SDL_ReadBE32(stream);
	receiveRessourceMaskLocal=receiveRessourceMask;
	sendRessourceMaskLocal=sendRessourceMask;

	shootingStep=SDL_ReadBE32(stream);
	shootingCooldown=SDL_ReadBE32(stream);
	if (versionMinor>=24)
		bullets=SDL_ReadBE32(stream);
	else
		bullets=0;

	// type
	typeNum=SDL_ReadBE32(stream);
	type=types->get(typeNum);
	assert(type);
	owner->prestige+=type->prestige;
	
	seenByMask=SDL_ReadBE32(stream);
	
	subscribeForInside=0;
	subscribeToBringRessources=0;
	subscribeForFlaging=0;
	canFeedUnit=0;
	canHealUnit=0;
	foodable=0;
	fillable=0;
	for (int i=0; i<NB_UNIT_TYPE; i++)
		zonable[i]=0;
	for (int i=0; i<NB_ABILITY; i++)
		upgrade[i]=0;
	
	for (int i=0; i<2; i++)
	{
		if (globalGradient[i])
		{
			delete[] globalGradient[i];
			globalGradient[i]=NULL;
		}
		if (localRessources[i])
		{
			delete[] localRessources[i];
			localRessources[i]=NULL;
		}
		dirtyLocalGradient[i]=true;
		locked[i]=false;
	}
	localRessourcesCleanTime=0;
	
	verbose=false;
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
	for (int i=0; i<MAX_NB_RESSOURCES; i++)
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

	SDL_WriteBE32(stream, receiveRessourceMask);
	SDL_WriteBE32(stream, sendRessourceMask);

	SDL_WriteBE32(stream, shootingStep);
	SDL_WriteBE32(stream, shootingCooldown);
	SDL_WriteBE32(stream, bullets);

	// type
	SDL_WriteBE32(stream, typeNum);
	// we drop type
	
	SDL_WriteBE32(stream, seenByMask);
}

void Building::loadCrossRef(SDL_RWops *stream, BuildingsTypes *types, Team *owner)
{
	fprintf(logFile, "loadCrossRef (%d)\n", gid);
	
	// units
	maxUnitInside=SDL_ReadBE32(stream);
	assert(maxUnitInside<65536);
	
	int nbWorking=SDL_ReadBE32(stream);
	fprintf(logFile, " nbWorking=%d\n", nbWorking);
	unitsWorking.clear();
	for (int i=0; i<nbWorking; i++)
	{
		Unit *unit=owner->myUnits[Unit::GIDtoID(SDL_ReadBE16(stream))];
		assert(unit);
		unitsWorking.push_front(unit);
	}

	int nbWorkingSubscribe=SDL_ReadBE32(stream);
	fprintf(logFile, " nbWorkingSubscribe=%d\n", nbWorkingSubscribe);
	unitsWorkingSubscribe.clear();
	for (int i=0; i<nbWorkingSubscribe; i++)
	{
		Unit *unit=owner->myUnits[Unit::GIDtoID(SDL_ReadBE16(stream))];
		assert(unit);
		unitsWorkingSubscribe.push_front(unit);
	}

	lastWorkingSubscribe=SDL_ReadBE32(stream);

	maxUnitWorking=SDL_ReadBE32(stream);
	maxUnitWorkingPreferred=SDL_ReadBE32(stream);
	maxUnitWorkingLocal=maxUnitWorking;
	int nbInside=SDL_ReadBE32(stream);
	fprintf(logFile, " nbInside=%d\n", nbInside);
	unitsInside.clear();
	for (int i=0; i<nbInside; i++)
	{
		Unit *unit=owner->myUnits[Unit::GIDtoID(SDL_ReadBE16(stream))];
		assert(unit);
		unitsInside.push_front(unit);
	}

	int nbInsideSubscribe=SDL_ReadBE32(stream);
	fprintf(logFile, " nbInsideSubscribe=%d\n", nbInsideSubscribe);
	unitsInsideSubscribe.clear();
	for (int i=0; i<nbInsideSubscribe; i++)
	{
		Unit *unit=owner->myUnits[Unit::GIDtoID(SDL_ReadBE16(stream))];
		assert(unit);
		unitsInsideSubscribe.push_front(unit);
	}
	lastInsideSubscribe=SDL_ReadBE32(stream);
}

void Building::saveCrossRef(SDL_RWops *stream)
{
	fprintf(logFile, "saveCrossRef (%d)\n", gid);
	
	// units
	SDL_WriteBE32(stream, maxUnitInside);
	SDL_WriteBE32(stream, unitsWorking.size());
	fprintf(logFile, " nbWorking=%d\n", unitsWorking.size());
	for (std::list<Unit *>::iterator  it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		SDL_WriteBE16(stream, (*it)->gid);
	}

	SDL_WriteBE32(stream, unitsWorkingSubscribe.size());
	fprintf(logFile, " nbWorkingSubscribe=%d\n", unitsWorkingSubscribe.size());
	for (std::list<Unit *>::iterator  it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		SDL_WriteBE16(stream, (*it)->gid);
	}
	
	SDL_WriteBE32(stream, lastWorkingSubscribe);

	SDL_WriteBE32(stream, maxUnitWorking);
	SDL_WriteBE32(stream, maxUnitWorkingPreferred);
	SDL_WriteBE32(stream, unitsInside.size());
	fprintf(logFile, " nbInside=%d\n", unitsInside.size());
	for (std::list<Unit *>::iterator  it=unitsInside.begin(); it!=unitsInside.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		SDL_WriteBE16(stream, (*it)->gid);
	}

	SDL_WriteBE32(stream, unitsInsideSubscribe.size());
	fprintf(logFile, " nbInsideSubscribe=%d\n", unitsInsideSubscribe.size());
	for (std::list<Unit *>::iterator  it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		SDL_WriteBE16(stream, (*it)->gid);
	}
	SDL_WriteBE32(stream, lastInsideSubscribe);
}

bool Building::isRessourceFull(void)
{
	for (int i=0; i<MAX_RESSOURCES; i++)
		if (ressources[i]<type->maxRessource[i])
			return false;
	return true;
}

int Building::neededRessource(void)
{
	float minProportion=1.0;
	int minType=-1;
	int deci=syncRand()%MAX_RESSOURCES;
	for (int ib=0; ib<MAX_RESSOURCES; ib++)
	{
		int i=(ib+deci)%MAX_RESSOURCES;
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

void Building::neededRessources(Uint8 needs[MAX_NB_RESSOURCES])
{
	for (int r=0; r<MAX_NB_RESSOURCES; r++)
	{
		int max=type->maxRessource[r];
		int cur=ressources[r];
		if (max>cur)
			needs[r]=max-cur;
		else
			needs[r]=0;
	}
}

int Building::neededRessource(int r)
{
	int need=type->maxRessource[r]-ressources[r];
	if (need>0)
		return need;
	else
		return false;
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
			assert(u);
			int d=u->displacement;
			if ((d!=Unit::DIS_INSIDE)&&(d!=Unit::DIS_ENTERING_BUILDING))
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
			recoverType=globalContainer->buildingsTypes.get(targetLevelTypeNum);
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
	posXLocal=posX;
	posYLocal=posY;

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

	setMapDiscovered();
}

void Building::setMapDiscovered(void)
{
	int vr=type->viewingRange;
	if (type->canExchange)
		owner->map->setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVisionExchange);
	else if (type->canFeedUnit)
		owner->map->setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVisionFood);
	else
		owner->map->setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVisionOther);
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

void Building::updateCallLists(void)
{
	if (buildingState==DEAD)
		return;
	bool ressourceFull=isRessourceFull();
	if (ressourceFull && !(type->canExchange && owner->openMarket()))
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
			for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
				(*it)->standardRandomActivity();
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
					int r=(*it)->caryedRessource;
					if (r>=0 && !neededRessource(r))
					{
						int newDistSquare=distSquare((*it)->posX, (*it)->posY, posX, posY);
						if (newDistSquare>maxDistSquare)
						{
							maxDistSquare=newDistSquare;
							fu=(*it);
							ittemp=it;
						}
					}
				}

				// Second choice: free an unit who has no ressource..
				if (fu==NULL)
				{
					int minDistSquare=INT_MAX;
					for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
					{
						int r=(*it)->caryedRessource;
						if (r<0)
						{
							int newDistSquare=distSquare((*it)->posX, (*it)->posY, posX, posY);
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
			{
				if (canFeedUnit!=1)
				{
					owner->canFeedUnit.push_front(this);
					canFeedUnit=1;
				}
			}
			else
			{
				if (canFeedUnit!=2)
				{
					owner->canFeedUnit.remove(this);
					canFeedUnit=2;
				}
			}

		// this is for Unit headling
		if (type->canHealUnit && canHealUnit!=1)
		{
			owner->canHealUnit.push_front(this);
			canHealUnit=1;
		}
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

		if (type->canFeedUnit && canFeedUnit!=2)
		{
			owner->canFeedUnit.remove(this);
			canFeedUnit=2;
		}
		if (type->canHealUnit && canHealUnit!=2)
		{
			owner->canHealUnit.remove(this);
			canHealUnit=2;
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
			cancelConstruction();
		}
		else if ((unitsWorking.size()==0) && (unitsInside.size()==0) && (unitsWorkingSubscribe.size()==0) && (unitsInsideSubscribe.size()==0))
		{
			buildingState=WAITING_FOR_CONSTRUCTION_ROOM;
			owner->buildingsTryToBuildingSiteRoom.push_front(this);
			if (verbose)
				printf("bgid=%d, inserted in buildingsTryToBuildingSiteRoom\n", gid);
		}
		else if (verbose)
			printf("bgid=%d, Building wait for upgrade, uws=%lu, uis=%lu, uwss=%lu, uiss=%lu.\n", gid, (unsigned long)unitsWorking.size(), (unsigned long)unitsInside.size(), (unsigned long)unitsWorkingSubscribe.size(), (unsigned long)unitsInsideSubscribe.size());
	}
}

void Building::updateBuildingSite(void)
{
	assert(type->isBuildingSite);
	
	if (isRessourceFull() && (buildingState!=WAITING_FOR_DESTRUCTION))
	{
		// we really uses the resources of the buildingsite:
		for(int i=0; i<MAX_RESSOURCES; i++)
			ressources[i]-=type->maxRessource[i];

		owner->prestige-=type->prestige;
		typeNum=type->nextLevelTypeNum;
		type=globalContainer->buildingsTypes.get(type->nextLevelTypeNum);
		assert(constructionResultState!=NO_CONSTRUCTION);
		constructionResultState=NO_CONSTRUCTION;
		owner->prestige+=type->prestige;

		// we don't need any worker any more

		// Notice that we could avoid freeing thoses units,
		// this would keep the units working to the same building,
		// and then ensure that all newly contructed food building to
		// be filled (at least start to be filled).

		for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); it++)
			(*it)->standardRandomActivity();
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
		if (type->canExchange)
			owner->canExchange.push_front(this);

		setMapDiscovered();
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

void Building::getRessourceCountToRepair(int ressources[BASIC_COUNT])
{
	assert(!type->isBuildingSite);
	int repairLevelTypeNum=type->lastLevelTypeNum;
	BuildingType *repairBt=globalContainer->buildingsTypes.get(repairLevelTypeNum);
	assert(repairBt);

	float destructionRatio=(float)hp/(float)type->hpMax;
	float fTotErr=0;
	for (int i=0; i<BASIC_COUNT; i++)
	{
		float fVal=destructionRatio*(float)repairBt->maxRessource[i];
		int iVal=(int)fVal;
		fTotErr+=fVal-(float)iVal;
		if (fTotErr>1)
		{
			fTotErr-=1;
			iVal++;
		}
		ressources[i]=repairBt->maxRessource[i]-iVal;
	}
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

	BuildingType *targetBt=globalContainer->buildingsTypes.get(targetLevelTypeNum);
	int newPosX=midPosX+targetBt->decLeft;
	int newPosY=midPosY+targetBt->decTop;
	
	int newWidth=targetBt->width;
	int newHeight=targetBt->height;

	bool isRoom=owner->map->isFreeForBuilding(newPosX, newPosY, newWidth, newHeight, gid);
	if (isRoom)
	{
		// OK, we have found enough room to expand our building-site, then we set-up the building-site.
		if (constructionResultState==REPAIR)
		{
			float destructionRatio=(float)hp/(float)type->hpMax;
			float fTotErr=0;
			for (int i=0; i<MAX_RESSOURCES; i++)
			{
				float fVal=destructionRatio*(float)targetBt->maxRessource[i];
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
		type=targetBt;
		owner->prestige+=type->prestige;

		buildingState=ALIVE;

		// units
		if (verbose)
			printf("bgid=%d, uses maxUnitWorkingPreferred=%d\n", gid, maxUnitWorkingPreferred);
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
	BuildingType *bt=globalContainer->buildingsTypes.get(tltn);
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
		(*it)->standardRandomActivity();
	unitsWorkingSubscribe.clear();

	for (std::list<Unit *>::iterator  it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); ++it)
		(*it)->standardRandomActivity();
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
	lastWorkingSubscribe++;
	if (fullWorking())
	{
		for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
			(*it)->standardRandomActivity();
		unitsWorkingSubscribe.clear();
		if (verbose)
			printf("...fullWorking()\n");
		return;
	}
	
	if (lastWorkingSubscribe>32)
	{
		if (verbose)
			printf("bgid=%d, subscribeToBringRessourcesStep()...\n", gid);
		if ((signed)unitsWorking.size()<maxUnitWorking)
		{
		
			int minValue=INT_MAX;
			Unit *choosen=NULL;
			Map *map=owner->map;
			/* To choose a good unit, we get a composition of things:
			1-the closest the unit is, the better it is.
			2-the less the unit is hungry, the better it is.
			3-if the unit has a needed ressource, this is better.
			4-if the unit as a not needed ressource, this is worse.
			5-if the unit is close of a needed ressource, this is better
			*/
			
			//First: we look only for units with a needed ressource:
			for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
			{
				Unit *unit=(*it);
				int r=unit->caryedRessource;
				int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->unitTypes[0][0].hungryness;
				if ((r>=0)&& neededRessource(r))
				{
					int dist;
					if (map->buildingAviable(this, unit->performance[SWIM], unit->posX, unit->posY, &dist) && dist<timeLeft)
					{
						int value=dist-(timeLeft>>1);
						unit->destinationPurprose=r;
						if (value<minValue)
						{
							minValue=value;
							choosen=unit;
						}
					}
				}
			}
			
			//Second: we look for an unit whois not carying a ressource:
			if (choosen==NULL)
			{
				Uint8 needs[MAX_NB_RESSOURCES];
				neededRessources(needs);
				int teamNumber=owner->teamNumber;
				for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
				{
					Unit *unit=(*it);
					if (unit->caryedRessource<0)
					{
						int x=unit->posX;
						int y=unit->posY;
						bool canSwim=unit->performance[SWIM];
						int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->unitTypes[0][0].hungryness;
						for (int r=0; r<MAX_RESSOURCES; r++)
						{
							int need=needs[r];
							if (need)
							{
								int distUnitRessource;
								if (map->ressourceAviable(teamNumber, r, canSwim, x, y, &distUnitRessource) && (distUnitRessource<timeLeft))
								{
									int distUnitBuilding;
									if (map->buildingAviable(this, canSwim, x, y, &distUnitBuilding) && distUnitBuilding<timeLeft)
									{
										int value=((distUnitRessource+distUnitBuilding)<<8)/need;
										if (value<minValue)
										{
											unit->destinationPurprose=r;
											minValue=value;
											choosen=unit;
											if (verbose)
												printf(" guid=%5d, distUnitRessource=%d, distUnitBuilding=%d, need=%d, value=%d\n", choosen->gid, distUnitRessource, distUnitBuilding, need, value);
										}
									}
								}
							}
						}
					}
				}
			}
			
			//special case for an exchange building:
			if (choosen==NULL && type->canExchange && owner->openMarket())
			{
				SessionGame &session=owner->game->session;
				// We compute all what's aviable from foreign ressources: (mask)
				Uint32 allForeignSendRessourceMask=0;
				Uint32 allForeignReceiveRessourceMask=0;
				for (int ti=0; ti<session.numberOfTeam; ti++)
					if (ti!=owner->teamNumber && (owner->game->teams[ti]->sharedVisionExchange & owner->me))
					{
						std::list<Building *> foreignCanExchange=owner->game->teams[ti]->canExchange;
						for (std::list<Building *>::iterator fbi=foreignCanExchange.begin(); fbi!=foreignCanExchange.end(); ++fbi)
						{
							Uint32 sendRessourceMask=(*fbi)->sendRessourceMask;
							for (int r=0; r<HAPPYNESS_COUNT; r++)
								if ((sendRessourceMask & (1<<r)) && ((*fbi)->ressources[HAPPYNESS_BASE+r]<=0))
									sendRessourceMask&=(~(1<<r));
							allForeignSendRessourceMask|=sendRessourceMask;

							Uint32 receiveRessourceMask=(*fbi)->receiveRessourceMask;
							for (int r=0; r<HAPPYNESS_COUNT; r++)
								if ((receiveRessourceMask & (1<<r))
									&& ((*fbi)->ressources[HAPPYNESS_BASE+r]>=(*fbi)->type->maxRessource[HAPPYNESS_BASE+r]))
									receiveRessourceMask&=(~(1<<r));
							allForeignReceiveRessourceMask|=receiveRessourceMask;
						}
					}

				if ((allForeignSendRessourceMask & receiveRessourceMask) && (allForeignReceiveRessourceMask & sendRessourceMask))
				{
					if (verbose)
						printf(" find best foreign exchangeBuilding\n");
					
					for (std::list<Unit *>::iterator uit=unitsWorkingSubscribe.begin(); uit!=unitsWorkingSubscribe.end(); ++uit)
					{
						Unit *unit=(*uit);
						int x=unit->posX;
						int y=unit->posY;
						bool canSwim=unit->performance[SWIM];
						int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->unitTypes[0][0].hungryness;
						int buildingDist;
						if (map->buildingAviable(this, canSwim, x, y, &buildingDist)
							&& (buildingDist<timeLeft))
							for (int ti=0; ti<session.numberOfTeam; ti++)
								if (ti!=owner->teamNumber)
								{
									Team *foreignTeam=owner->game->teams[ti];
									if (foreignTeam->sharedVisionExchange & owner->me)
									{
										std::list<Building *> foreignCanExchange=foreignTeam->canExchange;
										for (std::list<Building *>::iterator fbi=foreignCanExchange.begin(); fbi!=foreignCanExchange.end(); ++fbi)
										{
											Uint32 foreignSendRessourceMask=(*fbi)->sendRessourceMask;
											Uint32 foreignReceiveRessourceMask=(*fbi)->receiveRessourceMask;
											int foreignBuildingDist;
											if ((sendRessourceMask & foreignReceiveRessourceMask)
												&& (receiveRessourceMask & foreignSendRessourceMask)
												&& map->buildingAviable(*fbi, canSwim, x, y, &foreignBuildingDist)
												&& (buildingDist+(foreignBuildingDist<<1)<timeLeft))
											{
												int dist=buildingDist+foreignBuildingDist;
												if (dist<minValue)
												{
													if (verbose)
														printf(" found unit guid=%d, dist=%d\n", unit->gid, dist);
													choosen=unit;
													minValue=dist;
													unit->ownExchangeBuilding=this;
													unit->foreingExchangeBuilding=*fbi;
													unit->destinationPurprose=receiveRessourceMask & foreignSendRessourceMask;
												}
											}
										}
									}
								}
					}
				}
			}

			if (choosen==NULL)
			{
				Uint8 needs[MAX_NB_RESSOURCES];
				neededRessources(needs);
				int teamNumber=owner->teamNumber;
				//Third: we look for an unit whois carying an unwanted ressource:
				for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
				{
					Unit *unit=(*it);
					if (unit->caryedRessource>=0)
					{
						int x=unit->posX;
						int y=unit->posY;
						bool canSwim=unit->performance[SWIM];
						int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->unitTypes[0][0].hungryness;
						for (int r=0; r<MAX_RESSOURCES; r++)
						{
							int need=needs[r];
							if (need)
							{
								int distUnitRessource;
								if (map->ressourceAviable(teamNumber, r, canSwim, x, y, &distUnitRessource) && (distUnitRessource<timeLeft))
								{
									int distUnitBuilding;
									if (map->buildingAviable(this, canSwim, x, y, &distUnitBuilding) && distUnitBuilding<timeLeft)
									{
										int value=((distUnitRessource+distUnitBuilding)<<8)/need;
										if (value<minValue)
										{
											unit->destinationPurprose=r;
											minValue=value;
											choosen=unit;
										}
									}
								}
							}
						}
					}
				}
			}

			if (choosen)
			{
				if (verbose)
					printf(" unit %d choosen.\n", choosen->gid);
				
				unitsWorkingSubscribe.remove(choosen);
				if (neededRessource(choosen->destinationPurprose))
				{
					unitsWorking.push_back(choosen);
					choosen->subscriptionSuccess();
					updateCallLists();
				}
				else if (type->canExchange && owner->openMarket())
				{
					unitsWorking.push_back(choosen);
					choosen->subscriptionSuccess();
					updateCallLists();
				}
				else
				{
					// This unit may no more be needed here.
					// Let's remove it from this subscribing list.
					lastWorkingSubscribe=0;
					choosen->standardRandomActivity();
					if (verbose)
						printf("...!neededRessource(choosen->destinationPurprose=%d)\n", choosen->destinationPurprose);
					return;
				}
			}
		}
		
		if ((signed)unitsWorking.size()>=maxUnitWorking)
		{
			if (verbose)
				printf(" unitsWorking.size()>=maxUnitWorking\n");
			for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
				(*it)->standardRandomActivity();
			unitsWorkingSubscribe.clear();
		}
		if (verbose)
			printf(" ...done\n");
	}
}

void Building::subscribeForFlagingStep()
{
	lastWorkingSubscribe++;
	if (fullWorking())
	{
		for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
			(*it)->standardRandomActivity();
		unitsWorkingSubscribe.clear();
		return;
	}
	
	if (lastWorkingSubscribe>32)
	{
		lastWorkingSubscribe=0;
		if ((signed)unitsWorking.size()<maxUnitWorking)
		{
			int minValue=INT_MAX;
			Unit *choosen=NULL;
			Map *map=owner->map;
			
			/* To choose a good unit, we get a composition of things:
			1-the closest the unit is, the better it is.
			2-the less the unit is hungry, the better it is.
			2-the more hp the unit has, the better it is.
			*/
			if (zonable[EXPLORER])
			{
				for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
				{
					Unit *unit=(*it);
					int timeLeft=unit->hungry/unit->race->unitTypes[0][0].hungryness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					timeLeft*=timeLeft;
					hp*=hp;
					int dist=map->warpDistSquare(unit->posX, unit->posY, posX, posY);
					if (dist<timeLeft)
					{
						int value=dist-timeLeft-hp;
						if (value<minValue)
						{
							minValue=value;
							choosen=unit;
						}
					}
				}
			}
			else
			{
				for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
				{
					Unit *unit=(*it);
					int timeLeft=unit->hungry/unit->race->unitTypes[0][0].hungryness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					int dist;
					if (map->buildingAviable(this, unit->performance[SWIM], unit->posX, unit->posY, &dist) && (dist<timeLeft))
					{
						int value=dist-timeLeft-hp;
						if (value<minValue)
						{
							minValue=value;
							choosen=unit;
						}
					}
				}
			}
			
			if (choosen)
			{
				//printf("f(%x) choosen.\n", (int)choosen);
				unitsWorkingSubscribe.remove(choosen);
				unitsWorking.push_back(choosen);
				choosen->subscriptionSuccess();
				updateCallLists();
			}
		}
	}
}

void Building::subscribeForInsideStep()
{
	lastInsideSubscribe++;
	if (fullInside())
	{
		for (std::list<Unit *>::iterator it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
			(*it)->standardRandomActivity();
		unitsInsideSubscribe.clear();
		return;
	}
	
	if (lastInsideSubscribe>32)
	{
		if ((signed)unitsInside.size()<maxUnitInside)
		{
			int mindist=INT_MAX;
			Unit *u=NULL;
			Map *map=owner->map;
			for (std::list<Unit *>::iterator it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
			{
				Unit *unit=*it;
				if (unit->performance[FLY])
				{
					int dist=map->warpDistSquare((*it)->posX, (*it)->posY, posX, posY);
					if (dist<mindist)
					{
						mindist=dist;
						u=*it;
					}
				}
				else
				{
					int dist=map->warpDistSquare((*it)->posX, (*it)->posY, posX, posY);
					if (map->buildingAviable(this, unit->performance[SWIM], unit->posX, unit->posY, &dist) && (dist<mindist))
					{
						mindist=dist;
						u=*it;
					}
				}
			}
			if (u)
			{
				unitsInsideSubscribe.remove(u);
				assert(u);
				unitsInside.push_back(u);
				u->subscriptionSuccess();
				updateCallLists();
			}
		}
		
		if ((signed)unitsInside.size()>=maxUnitInside)
		{
			for (std::list<Unit *>::iterator it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
				(*it)->standardRandomActivity();
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
	if (ressources[STONE]>0 && (bullets<=(type->maxBullets-type->multiplierStoneToBullets)))
	{
		ressources[STONE]--;
		bullets+=type->multiplierStoneToBullets;
		
		// we need to be stone-feeded
		updateCallLists();
	}
	
	if (shootingCooldown>0)
	{
		shootingCooldown-=type->shootRythme;
		return;
	}
	
	if (bullets<=0)
		return;

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
						if (testUnit->foreingExchangeBuilding==NULL)
						{
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
						}
						//printf("found unit target: (%d, %d) t=%d, id=%d \n", targetX, targetY, otherTeam, Unit::UIDtoID(targetUID));
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

		bullets--;
		shootingCooldown=SHOOTING_COOLDOWN_MAX;
	}

}

void Building::kill(void)
{
	fprintf(logFile, "kill gid=%d buildingState=%d\n", gid, buildingState);
	if (buildingState==DEAD)
		return;
	
	
	fprintf(logFile, " still %d unitsInside\n", unitsInside.size());
	for (std::list<Unit *>::iterator it=unitsInside.begin(); it!=unitsInside.end(); ++it)
	{
		Unit *u=*it;
		fprintf(logFile, "  guid=%d\n", u->gid);
		if (u->displacement==Unit::DIS_INSIDE)
			u->isDead=true;

		if (u->displacement==Unit::DIS_ENTERING_BUILDING)
		{
			if (!u->performance[FLY])
				owner->map->setGroundUnit(u->posX-u->dx, u->posY-u->dy, NOGUID);
			//printf("(%x)Building:: Unit(uid%d)(id%d) killed while entering. dis=%d, mov=%d, ab=%x, ito=%d \n",this, u->gid, Unit::UIDtoID(u->gid), u->displacement, u->movement, (int)u->attachedBuilding, u->insideTimeout);
			u->isDead=true;
		}
		u->standardRandomActivity();
	}
	unitsInside.clear();

	fprintf(logFile, " still %d unitsWorking\n", unitsInside.size());
	for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
	{
		assert(*it);
		(*it)->standardRandomActivity();
	}
	unitsWorking.clear();
	
	removeSubscribers();
	
	maxUnitWorking=0;
	maxUnitWorkingLocal=0;
	maxUnitInside=0;
	updateCallLists();

	if (!type->isVirtual)
	{
		owner->map->setBuilding(posX, posY, type->width, type->height, NOGBID);
		if (type->isBuildingSite && type->level==0)
		{
			bool good=false;
			for (int r=0; r<BASIC_COUNT; r++)
				if (ressources[r]>0)
				{
					good=true;
					break;
				}
			if (!good)
				owner->noMoreBuildingSitesCountdown=Team::noMoreBuildingSitesCountdownMax;
		}
		
	}
	
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

Uint32 Building::eatOnce(Uint32 *mask)
{
	ressources[CORN]--;
	assert(ressources[CORN]>=0);
	Uint32 fruitMask=0;
	Uint32 fruitCount=0;
	for (int i=0; i<HAPPYNESS_COUNT; i++)
	{
		int resId=i+HAPPYNESS_BASE;
		if (ressources[resId])
		{
			ressources[resId]--;
			fruitMask|=(1<<i);
			fruitCount++;
		}
	}
	if (mask)
		*mask=fruitMask;
	return fruitCount;
}

int Building::aviableHappynessLevel()
{
	int inside=(int)unitsInside.size();
	if (ressources[CORN]<=inside)
		return 0;
	int happyness=1;
	for (int i=0; i<HAPPYNESS_COUNT; i++)
		if (ressources[i+HAPPYNESS_BASE]>inside)
			happyness++;
	return happyness;
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

void Building::integrity()
{
	assert(unitsWorking.size()>=0);
	assert(unitsWorking.size()<=1024);
	for (std::list<Unit *>::iterator  it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		assert((*it)->attachedBuilding==this);
	}

	assert(unitsWorkingSubscribe.size()>=0);
	assert(unitsWorkingSubscribe.size()<=1024);
	for (std::list<Unit *>::iterator  it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		assert((*it)->attachedBuilding==this);
	}
	
	assert(unitsInside.size()>=0);
	assert(unitsInside.size()<=1024);
	for (std::list<Unit *>::iterator  it=unitsInside.begin(); it!=unitsInside.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		assert((*it)->attachedBuilding==this);
	}

	assert(unitsInsideSubscribe.size()>=0);
	assert(unitsInsideSubscribe.size()<=1024);
	for (std::list<Unit *>::iterator  it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		assert((*it)->attachedBuilding==this);
	}
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

	for (int i=0; i<MAX_RESSOURCES; i++)
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



