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


Building::Building(SDL_RWops *stream, BuildingsTypes *types, Team *owner)
{
	load(stream, types, owner);
}

Building::Building(int x, int y, int uid, int typeNum, Team *team, BuildingsTypes *types)
{
	// type
	this->typeNum=typeNum;
	type=types->buildingsTypes[typeNum];

	// construction state
	buildingState=ALIVE;

	// units
	maxUnitInside=type->maxUnitInside;
	maxUnitWorking=type->maxUnitWorking;
	maxUnitWorkingLocal=maxUnitWorking;
	maxUnitWorkingPreferred=1;
	lastInsideSubscribe=0;
	lastWorkingSubscribe=0;

	// identity
	UID=uid;
	owner=team;

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
	for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
	{
		ratioLocal[i]=ratio[i]=1;
		totalRatio++;
		percentUsed[i]=0;
	}
	shootingStep=0;
	shootingCooldown=SHOOTING_COOLDOWN_MAX;

	seenByMask=0;
	
	for (int i=0; i<NB_ABILITY; i++)
	{
		job[i]=false;
		attract[i]=false;
	}
	// optimisation parameters
	// FIXME: we don't know it this would be usefull or not !
	// Now, it's not used.
	//Sint32 closestRessourceX[NB_RESSOURCES], closestRessourceY[NB_RESSOURCES];
}

void Building::load(SDL_RWops *stream, BuildingsTypes *types, Team *owner)
{
	// construction state
	buildingState=(BuildingState)SDL_ReadBE32(stream);

	// identity
	UID=SDL_ReadBE32(stream);
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
	{
		for (int i=0; i<NB_RESSOURCES; i++)
		{
			ressources[i]=SDL_ReadBE32(stream);
		}
	}

	// quality parameters
	hp=SDL_ReadBE32(stream);

	// prefered parameters
	productionTimeout=SDL_ReadBE32(stream);
	totalRatio=SDL_ReadBE32(stream);
	{
		for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
		{
			ratioLocal[i]=ratio[i]=SDL_ReadBE32(stream);
			percentUsed[i]=SDL_ReadBE32(stream);
		}
	}

	shootingStep=SDL_ReadBE32(stream);
	shootingCooldown=SDL_ReadBE32(stream);

	// optimisation parameters
	{
		for (int i=0; i<NB_RESSOURCES; i++)
		{
			closestRessourceX[i]=SDL_ReadBE32(stream);
			closestRessourceY[i]=SDL_ReadBE32(stream);
		}
	}

	// type
	typeNum=SDL_ReadBE32(stream);
	type=types->buildingsTypes[typeNum];
	
	seenByMask=0; //TODO: load it!
}

void Building::save(SDL_RWops *stream)
{
	int i;

	// construction state
	SDL_WriteBE32(stream, (Uint32)buildingState);

	// identity
	SDL_WriteBE32(stream, UID);
	// we drop team

	// position
	SDL_WriteBE32(stream, posX);
	SDL_WriteBE32(stream, posY);

	// Flag specific
	SDL_WriteBE32(stream, unitStayRange);

	// Building Specific
	for (i=0; i<NB_RESSOURCES; i++)
	{
		SDL_WriteBE32(stream, ressources[i]);
	}

	// quality parameters
	SDL_WriteBE32(stream, hp);

	// prefered parameters
	SDL_WriteBE32(stream, productionTimeout);
	SDL_WriteBE32(stream, totalRatio);
	for (i=0; i<UnitType::NB_UNIT_TYPE; i++)
	{
		SDL_WriteBE32(stream, ratio[i]);
		SDL_WriteBE32(stream, percentUsed[i]);
	}

	SDL_WriteBE32(stream, shootingStep);
	SDL_WriteBE32(stream, shootingCooldown);

	// optimisation parameters
	for (i=0; i<NB_RESSOURCES; i++)
	{
		SDL_WriteBE32(stream, closestRessourceX[i]);
		SDL_WriteBE32(stream, closestRessourceY[i]);
	}

	// type
	SDL_WriteBE32(stream, typeNum);
	// we drop type
}

void Building::loadCrossRef(SDL_RWops *stream, BuildingsTypes *types, Team *owner)
{
	int i;
	// units
	maxUnitInside=SDL_ReadBE32(stream);
	int nbWorking=SDL_ReadBE32(stream);
	unitsWorking.clear();
	for (i=0; i<nbWorking; i++)
	{
		unitsWorking.push_front(owner->myUnits[Unit::UIDtoID(SDL_ReadBE32(stream))]);
	}

	int nbWorkingSubscribe=SDL_ReadBE32(stream);
	unitsWorkingSubscribe.clear();
	for (i=0; i<nbWorkingSubscribe; i++)
	{
		unitsWorkingSubscribe.push_front(owner->myUnits[Unit::UIDtoID(SDL_ReadBE32(stream))]);
	}

	lastWorkingSubscribe=SDL_ReadBE32(stream);

	maxUnitWorking=SDL_ReadBE32(stream);
	maxUnitWorkingPreferred=SDL_ReadBE32(stream);
	maxUnitWorkingLocal=maxUnitWorking;
	int nbInside=SDL_ReadBE32(stream);
	unitsInside.clear();
	for (i=0; i<nbInside; i++)
	{
		unitsInside.push_front(owner->myUnits[Unit::UIDtoID(SDL_ReadBE32(stream))]);
	}

	int nbInsideSubscribe=SDL_ReadBE32(stream);
	unitsInsideSubscribe.clear();
	for (i=0; i<nbInsideSubscribe; i++)
	{
		unitsInsideSubscribe.push_front(owner->myUnits[Unit::UIDtoID(SDL_ReadBE32(stream))]);
	}
	lastInsideSubscribe=SDL_ReadBE32(stream);
}

void Building::saveCrossRef(SDL_RWops *stream)
{
	std::list<Unit *>::iterator it;

	// units
	SDL_WriteBE32(stream, maxUnitInside);
	SDL_WriteBE32(stream, unitsWorking.size());
	for (it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
	{
		SDL_WriteBE32(stream, (*it)->UID);
	}

	SDL_WriteBE32(stream, unitsWorkingSubscribe.size());
	for (it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
	{
		SDL_WriteBE32(stream, (*it)->UID);
	}

	SDL_WriteBE32(stream, lastWorkingSubscribe);

	SDL_WriteBE32(stream, maxUnitWorking);
	SDL_WriteBE32(stream, maxUnitWorkingPreferred);
	SDL_WriteBE32(stream, unitsInside.size());
	for (it=unitsInside.begin(); it!=unitsInside.end(); ++it)
	{
		SDL_WriteBE32(stream, (*it)->UID);
	}

	SDL_WriteBE32(stream, unitsInsideSubscribe.size());
	for (it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); ++it)
	{
		SDL_WriteBE32(stream, (*it)->UID);
	}
	SDL_WriteBE32(stream, lastInsideSubscribe);
}

bool Building::isRessourceFull(void)
{
	bool isFull=true;
	int i;
	for (i=0; i<NB_RESSOURCES; i++)
	{
		if (ressources[i]<type->maxRessource[i])
			isFull=false;
	}
	return isFull;
}

int Building::neededRessource(void)
{
	float minProportion=1.0;
	int minType=-1;
	int deci=syncRand()%NB_RESSOURCES;
	int ib;
	for (ib=0; ib<NB_RESSOURCES; ib++)
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

void Building::launchUpgrade(void)
{
	if ((buildingState==ALIVE) && (!type->isBuildingSite) && (type->nextLevelTypeNum!=-1) && (isHardSpace()))
	{
		if (type->unitProductionTime)
			owner->swarms.remove(this);
		if (type->shootingRange)
			owner->turrets.remove(this);
		
		removeSubscribers();
		
		// We remove all units who are going to the building:
		// Notice that the algotithme is not fast but clean.
		std::list<Unit *> unitsToRemove;
		for (std::list<Unit *>::iterator it=unitsInside.begin(); it!=unitsInside.end(); it++)
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
		for (std::list<Unit *>::iterator it=unitsToRemove.begin(); it!=unitsToRemove.end(); it++)
		{
			Unit *u=*it;
			unitsInside.remove(u);
		}
		
		buildingState=Building::WAITING_FOR_UPGRADE;
		maxUnitWorkingLocal=0;
		maxUnitWorking=0;
		maxUnitInside=0;
		update();
	}
}

void Building::cancelUpgrade(void)
{
	Uint32 recoverTypeNum=typeNum;
	BuildingType *recoverType=type;
	
	if (type->isBuildingSite)
	{
		int lastLevelTypeNum=type->lastLevelTypeNum;
		
		if (lastLevelTypeNum!=-1)
		{
			recoverTypeNum=lastLevelTypeNum;
			recoverType=globalContainer->buildingsTypes.getBuildingType(lastLevelTypeNum);
		}
		else
			assert(false);
	}
	else if (buildingState==Building::WAITING_FOR_UPGRADE_ROOM)
	{
		owner->buildingsToBeUpgraded.remove(this);
		buildingState=Building::ALIVE;
	}
	else if (buildingState==Building::WAITING_FOR_UPGRADE)
	{
		buildingState=Building::ALIVE;
	}
	else
	{
		// Congratulation, you have managed to click "cancel upgrade"
		// when the building upgrade" was already canceled.
		return;
		assert(false);
	}
	
	if (!type->isVirtual)
		owner->game->map.setBuilding(posX, posY, type->width, type->height, NOUID);
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;
	
	typeNum=recoverTypeNum;
	type=recoverType;
	
	posX=midPosX+type->decLeft;
	posY=midPosY+type->decTop;

	if (!type->isVirtual)
		owner->game->map.setBuilding(posX, posY, type->width, type->height, UID);
	
	if (type->maxUnitWorking)
		maxUnitWorking=maxUnitWorkingPreferred;
	else
		maxUnitWorking=0;
	maxUnitWorkingLocal=maxUnitWorking;
	maxUnitInside=type->maxUnitInside;

	if (hp>=type->hpInit)
		hp=type->hpInit;
	
	productionTimeout=type->unitProductionTime;

	if (type->unitProductionTime)
		owner->swarms.push_front(this);
	if (type->shootingRange)
		owner->turrets.push_front(this);
	
	totalRatio=0;
	
	for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
	{
		ratio[i]=1;
		totalRatio++;
		percentUsed[i]=0;
	}

	int vr=type->viewingRange;
	owner->game->map.setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVision);
	
	update();
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
		update();
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
	update();
}

void Building::update(void)
{
	int i;
	if (buildingState==DEAD)
		return;

	if (buildingState==WAITING_FOR_DESTRUCTION)
	{
		if ((unitsWorking.size()==0) && (unitsInside.size()==0) && (unitsWorkingSubscribe.size()==0) && (unitsInsideSubscribe.size()==0))
		{
			if (!type->isVirtual)
				owner->game->map.setBuilding(posX, posY, type->width, type->height, NOUID);
			buildingState=DEAD;
			owner->buildingsToBeDestroyed.push_front(UIDtoID(UID));
		}
		else
		{
			printf("(%d)Building wait for destruction, uws=%lu, uis=%lu, uwss=%lu, uiss=%lu.\n", UID, (unsigned long)unitsWorking.size(), (unsigned long)unitsInside.size(), (unsigned long)unitsWorkingSubscribe.size(), (unsigned long)unitsInsideSubscribe.size());
		}
	}

	if ((buildingState==WAITING_FOR_UPGRADE) || (buildingState==WAITING_FOR_UPGRADE_ROOM))
	{
		if (!isHardSpace())
		{
			cancelUpgrade();
		}
		else if ((unitsWorking.size()==0) && (unitsInside.size()==0) && (unitsWorkingSubscribe.size()==0) && (unitsInsideSubscribe.size()==0))
		{
			buildingState=WAITING_FOR_UPGRADE_ROOM;
			owner->buildingsToBeUpgraded.push_front(this);
			//printf("inserted %d, w=%d\n", (int)this, type->width);
		}
		else
			printf("(%d)Building wait for upgrade, uws=%lu, uis=%lu, uwss=%lu, uiss=%lu.\n", UID, (unsigned long)unitsWorking.size(), (unsigned long)unitsInside.size(), (unsigned long)unitsWorkingSubscribe.size(), (unsigned long)unitsInsideSubscribe.size());
	}

	// TODO : save the knowledge weather or not the building is already in the Call list in the building
	if (unitsWorking.size()<(unsigned)maxUnitWorking)
	{
		// add itself in Call lists
		for (i=0; i<NB_ABILITY; i++)
		{
			if (!job[i] && type->job[i])
				owner->job[i].push_front(this);
			if (!attract[i] && type->attract[i])
				owner->attract[i].push_front(this);
			job[i]=true;
			attract[i]=true;
		}
	}
	else
	{
		// delete itself from all Call lists
		for (i=0; i<NB_ABILITY; i++)
		{
			if (job[i] && type->job[i])
				owner->job[i].remove(this);
			if (attract[i] && type->attract[i])
				owner->attract[i].remove(this);
			job[i]=false;
			attract[i]=false;
		}

		while (unitsWorking.size()>(unsigned)maxUnitWorking) // TODO : the same with insides units
		{
			int maxDistSquare=0;

			Unit *fu=NULL;
			std::list<Unit *>::iterator ittemp;
			
			// First choice: free an unit who has a not needed ressource..
			for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); it++)
			{
				int newDistSquare=distSquare((*it)->posX, (*it)->posY, posX, posY);
				int r=(*it)->caryedRessource;
				if ( (r>=0) && (!neededRessource(r)) )
					if (newDistSquare>maxDistSquare)
					{
						maxDistSquare=newDistSquare;
						fu=(*it);
						ittemp=it;
					}
			}
			
			// Second choice: free an unit who has no ressource..
			if (fu==NULL)
				for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); it++)
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
				for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); it++)
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
				//printf("Building::update::unitsWorking::fu->UID=%d\n", fu->UID);
				// We free the unit.
				fu->activity=Unit::ACT_RANDOM;
				fu->displacement=Unit::DIS_RANDOM;
				
				unitsWorking.erase(ittemp);
				update();
				fu->attachedBuilding=NULL;
				fu->needToRecheckMedical=true;
			}
			else
				break;

		}
	}

	if ((signed)unitsInside.size()<maxUnitInside)
	{
		// add itself in Call lists
		for (i=0; i<NB_ABILITY; i++)
		{
			if (type->upgrade[i])
				owner->upgrade[i].push_front(this);
		}

		// this is for food handling
		if (type->canFeedUnit)
		{
			if (ressources[CORN]>(int)unitsInside.size())
			{
				//printf("work : unit : act %d - max %d   res : %d\n", unitsInside.size(), maxUnitInside, ressources[CORN]);
				owner->canFeedUnit.push_front(this);
			}
			else
				owner->canFeedUnit.remove(this);
		}

		// this is for Unit headling
		if (type->canHealUnit)
			owner->canHealUnit.push_front(this);
	}
	else
	{
		// delete itself from all Call lists
		for (i=0; i<NB_ABILITY; i++)
		{
			if (type->upgrade[i])
				owner->upgrade[i].remove(this);
		}

		if (type->canFeedUnit)
			owner->canFeedUnit.remove(this);
		if (type->canHealUnit)
			owner->canHealUnit.remove(this);
	}

	// this is for ressource gathering
	if (isRessourceFull() && (buildingState!=WAITING_FOR_DESTRUCTION))
	{
		if (type->isBuildingSite)
		{
			// we really uses the resources of the buildingsite:
			for(i=0; i<NB_RESSOURCES; i++)
				ressources[i]-=type->maxRessource[i];

			typeNum=type->nextLevelTypeNum;
			type=globalContainer->buildingsTypes.getBuildingType(type->nextLevelTypeNum);

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

			// DUNNO : when do we update closestRessourceXY[] ?
			int vr=type->viewingRange;
			owner->game->map.setMapDiscovered(posX-vr, posY-vr, type->width+vr*2, type->height+vr*2, owner->sharedVision);
			owner->setEvent(getMidX(), getMidY(), Team::BUILDING_FINISHED_EVENT);

			// we need to do an update again
			update();
		}
		else if (job[HARVEST])
		{
			owner->job[HARVEST].remove(this);
			job[HARVEST]=false;
		}
	}
}

bool Building::tryToUpgradeRoom(void)
{
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;

	int nextLevelTypeNum=type->nextLevelTypeNum;
	BuildingType *nextBt=globalContainer->buildingsTypes.getBuildingType(type->nextLevelTypeNum);
	int newPosX=midPosX+nextBt->decLeft;
	int newPosY=midPosY+nextBt->decTop;

	int newWidth=nextBt->width;
	int newHeight=nextBt->height;
	int lastNewPosX=newPosX+newWidth;
	int lastNewPosY=newPosY+newHeight;

	bool isRoom=true;

	int i;
	
	for(int x=newPosX; x<lastNewPosX; x++)
	{
		for(int y=newPosY; y<lastNewPosY; y++)
		{
			// TODO : put this code in map.cpp to optimise speed.
			Sint32 UID=owner->game->map.getUnit(x, y);
			if ( (!owner->game->map.isGrass(x, y)) || ((UID!=this->UID) && (UID!=NOUID)) )
				isRoom=false;
		}
	}

	if (isRoom)
	{
		if (!type->isVirtual)
		{
			owner->game->map.setBuilding(posX, posY, type->decLeft, type->decLeft, NOUID);
			owner->game->map.setBuilding(newPosX, newPosY, newWidth, newHeight, UID);
		}

		typeNum=nextLevelTypeNum;
		type=nextBt;

		buildingState=ALIVE;

		// units
		//printf("uses maxUnitWorkingPreferred=%d\n", maxUnitWorkingPreferred);
		maxUnitWorking=maxUnitWorkingPreferred;
		maxUnitWorkingLocal=maxUnitWorking;
		maxUnitInside=type->maxUnitInside;

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
		for (i=0; i<UnitType::NB_UNIT_TYPE; i++)
		{
			ratio[i]=1;
			totalRatio++;
			percentUsed[i]=0;
		}

		update();
	}
	return isRoom;
}

bool Building::isHardSpace(void)
{
	int nltn=type->nextLevelTypeNum;
	if (nltn==-1)
		return true;
	BuildingType *bt=globalContainer->buildingsTypes.getBuildingType(nltn);
	int x=posX+bt->decLeft-type->decLeft;
	int y=posY+bt->decTop -type->decTop ;
	int w=bt->width;
	int h=bt->height;
	int dx, dy;

	if (bt->isVirtual)
	{
		for (dy=y; dy<y+h; dy++)
		{
			for (dx=x; dx<x+w; dx++)
			{
				Sint32 uid=owner->game->map.getUnit(dx, dy);
				if ((uid<0)&&(uid!=NOUID)&&(uid!=UID))
					return false;
			}
		}
		return true;
	}
	else
	{
		for (dy=y; dy<y+h; dy++)
		{
			for (dx=x; dx<x+w; dx++)
			{
				Sint32 uid=owner->game->map.getUnit(dx, dy);
				if (((uid<0)&&(uid!=NOUID)&&(uid!=UID))||(!(owner->game->map.isGrass(dx, dy))))
					return false;
			}
		}
		return true;
	}
	assert(false);
	return true;
}


void Building::step(void)
{
	assert(false);
	// NOTE : Unit needs to update itself when it is in a building
}

void Building::removeSubscribers(void)
{
	std::list<Unit *>::iterator it;
	for (it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
	{
		(*it)->attachedBuilding=NULL;
		(*it)->subscribed=false;
		(*it)->activity=Unit::ACT_RANDOM;
		(*it)->needToRecheckMedical=true;
	}
	unitsWorkingSubscribe.clear();

	for (it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); ++it)
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

void Building::subscribeForConstructionStep()
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
			int minValue=owner->game->map.getW()*owner->game->map.getW();
			Unit *choosen=NULL;
			Map &map=owner->game->map;
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
					int dist=owner->game->map.warpDistSquare(x, y, posX, posY);
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
					if (map.nearestRessource(x, y, &(RessourceType)r, &dx, &dy)&& neededRessource(r))
					{
						int dist=owner->game->map.warpDistSquare(dx, dy, posX, posY);
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
					int dist=owner->game->map.warpDistSquare(unit->posX, unit->posY, posX, posY);
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
					
					printf("C-B(%x)UID=(%d), choosen=(%x) UUID=(%d), dp=(%d), nr=(%d, %d, %d, %d).\n", (int)this, UID, (int)choosen, (int)choosen->UID, choosen->destinationPurprose, neededRessource(0), neededRessource(1), neededRessource(2), neededRessource(3));
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
					update();
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

void Building::subscribeForFightingStep()
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
			int minValue=owner->game->map.getW()*owner->game->map.getW();
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
				int dist=owner->game->map.warpDistSquare(x, y, posX, posY);
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
				update();
			}
		}
	}
}

void Building::subscribeForWorkingStep()
{
	// TODO : add new lists to Building, to make the difference between
	// workers for ressources (construction and reparation), and workers
	// for the building (Fighters).
	if (!isRessourceFull())
		subscribeForConstructionStep();
	else
		subscribeForFightingStep();
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
			int mindist=owner->game->map.getW()*owner->game->map.getW();
			Unit *u=NULL;
			for (it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
			{
				int dist=owner->game->map.warpDistSquare((*it)->posX, (*it)->posY, posX, posY);
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
				update();
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
	assert(UnitType::NB_UNIT_TYPE==3);
	if ((ressources[CORN]>=type->ressourceForOneUnit)&&(ratio[0]|ratio[1]|ratio[2]))
		productionTimeout--;

	if (productionTimeout<0)
	{
		// We find the kind of unit we have to create:
		float proportion;
		float minProportion=1.0;
		int minType=-1;
		int i;
		for (i=0; i<UnitType::NB_UNIT_TYPE; i++)
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
		assert(minType<UnitType::NB_UNIT_TYPE);
		if (minType<0 || minType>=UnitType::NB_UNIT_TYPE)
			minType=0;

		// help printf
		/*printf("SWARM : prod ratio %d/%d/%d - pused %d/%d/%d, producing : %d\n",
			ratio[0], ratio[1], ratio[2],
			percentUsed[0], percentUsed[1], percentUsed[2],
			minType);*/

		// We get the unit UnitType:
		int posX, posY, dx, dy;
		UnitType *ut=owner->race.getUnitType((UnitType::TypeNum)minType, 0);
		Unit *u;

		// Is there a place to exit ?
#		ifdef WIN32
#			pragma warning (disable : 4800)
#		endif
		if (findExit(&posX, &posY, &dx, &dy, ut->performance[FLY]))
#		ifdef WIN32
#			pragma warning (default : 4800)
#		endif
		{
			u=owner->game->addUnit(posX, posY, owner->teamNumber, minType, 0, 0, dx, dy);
			if (u)
			{
				ressources[CORN]-=type->ressourceForOneUnit;
				update(); // TODO : is there a trigger, to avoid comming back in call lists too often ?
				
				u->activity=Unit::ACT_RANDOM;
				u->displacement=Unit::DIS_RANDOM;
				u->movement=Unit::MOV_EXITING_BUILDING;
				u->speed=u->performance[u->action];

				productionTimeout=type->unitProductionTime;

				// We update percentUsed[]
				percentUsed[minType]++;

				bool allDone=true;
				for (i=0; i<UnitType::NB_UNIT_TYPE; i++)
				{
					if (percentUsed[i]<ratio[i])
						allDone=false;
				}

				if (allDone)
				{
					for (i=0; i<UnitType::NB_UNIT_TYPE; i++)
						percentUsed[i]=0;
				}
			}
			else
			{
				printf("WARNING, no more UNIT ID free for team %d\n", owner->teamNumber);
			}
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
	Map *map=&(owner->game->map);

	int targetX=-1;
	int targetY=-1;
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
				int targetUID=map->getUnit(targetX, targetY);
				if (targetUID>=0)
				{
					int otherTeam=Unit::UIDtoTeam(targetUID);
					Uint32 otherTeamMask=1<<otherTeam;
					if (enemies&otherTeamMask)
					{
						targetFound=true;
						//printf("found unit target found: (%d, %d) t=%d, id=%d \n", targetX, targetY, otherTeam, Unit::UIDtoID(targetUID));
						shootingStep=0;
						break;
					}
				}
				else if (targetUID!=NOUID)
				{
					int otherTeam=Building::UIDtoTeam(targetUID);
					int otherID=Building::UIDtoID(targetUID);
					Uint32 otherTeamMask=1<<otherTeam;
					if (enemies&otherTeamMask)
					{
						Building *b=owner->game->teams[otherTeam]->myBuildings[otherID];
						if (!b->type->defaultUnitStayRange)
						{
							targetFound=true;
							shootingStep=0;
							break;
						}
					}
				}
			}
				
	/*for (i=0; i<=range && !targetFound; i++)
	{
		for (int k=0; k<2 && !targetFound; k++)
		{
			for (int l=0; l<2 && !targetFound; l++)
			{
				//targetX=midX+(k)*(  dir)*(pos+1)-(1-k)*(  dir)*(pos)+(l)*(1-dir)*(i+width +1)-(1-l)*(1-dir)*(i);
				//targetY=midY+(k)*(1-dir)*(pos+1)-(1-k)*(1-dir)*(pos)+(l)*(  dir)*(i+height+1)-(1-l)*(  dir)*(i);
				//printf("midX=%d, targetX=%d, k=%d, l=%d, pos=%d, dir=%d, i=%d \n", midX, targetX, k, l, pos, dir, i);
				//printf("midY=%d, targetY=%d, k=%d, l=%d, pos=%d, dir=%d, i=%d \n", midY, targetY, k, l, pos, dir, i);
				targetX=posX+

				int targetUID=map->getUnit(targetX, targetY);
				if (targetUID>=0)
				{
					int otherTeam=Unit::UIDtoTeam(targetUID);
					Uint32 otherTeamMask=1<<otherTeam;
					if (enemies&otherTeamMask)
					{
						targetFound=true;
						//printf("found unit target found: (%d, %d) t=%d, id=%d \n", targetX, targetY, otherTeam, Unit::UIDtoID(targetUID));
						shootingStep=0;
					}
				}
				else if (targetUID!=NOUID)
				{
					int otherTeam=Building::UIDtoTeam(targetUID);
					int otherID=Building::UIDtoID(targetUID);
					Uint32 otherTeamMask=1<<otherTeam;
					if (enemies&otherTeamMask)
					{
						Building *b=owner->game->teams[otherTeam]->myBuildings[otherID];
						if (!b->type->defaultUnitStayRange)
						{
							targetFound=true;
							shootingStep=0;
						}
					}
				}
			}
		}
	}*/
	int midX=getMidX();
	int midY=getMidY();
	if (targetFound)
	{
		//printf("%d found target found: (%d, %d) \n", UID, targetX, targetY);
		Sector *s=owner->game->map.getSector(midX, midY);

		int px, py;
		px=((posX)<<5)+((type->width)<<4);
		py=((posY)<<5)+((type->height)<<4);

		int speedX, speedY, ticksLeft;

		// TODO : shall we really uses shootSpeed ?
		// FIXME : is it correct this way ? IS there a function for this ?
		int dpx=(targetX*32)+16-4-px; // 4 is the half size of the bullet
		int dpy=(targetY*32)+16-4-py;
		//printf("%d insert: dp=(%d, %d).\n", UID, dpx, dpy);
		if (dpx>(owner->game->map.getW()<<4))
			dpx=dpx-(owner->game->map.getW()<<5);
		if (dpx<-(owner->game->map.getW()<<4))
			dpx=dpx+(owner->game->map.getW()<<5);
		if (dpy>(owner->game->map.getH()<<4))
			dpy=dpy-(owner->game->map.getH()<<5);
		if (dpy<-(owner->game->map.getH()<<4))
			dpy=dpy+(owner->game->map.getH()<<5);


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

		Bullet *b=new Bullet(px, py, speedX, speedY, ticksLeft, type->shootDamage, targetX, targetY);
		//printf("%d insert: pos=(%d, %d), target=(%d, %d), p=(%d, %d), dp=(%d, %d), mdp=%d, speed=(%d, %d).\n", UID, posX, posY, targetX, targetY, px, py, dpx, dpy, mdp, speedX, speedY);
		//printf("%d insert: (px=%d, py=%d, sx=%d, sy=%d, tl=%d, sd=%d) \n", UID, px, py, speedX, speedY, ticksLeft, type->shootDamage);
		s->bullets.push_front(b);

		shootingCooldown=SHOOTING_COOLDOWN_MAX;
	}

}

void Building::kill(void)
{
	std::list<Unit *>::iterator it;
	for (it=unitsInside.begin(); it!=unitsInside.end(); it++)
	{
		Unit *u=*it;
		if (u->displacement==Unit::DIS_INSIDE)
		{
			//printf("(%x)Building:: Unit(uid%d)(id%d) killed. dis=%d, mov=%d, ab=%x, ito=%d \n", this, u->UID, Unit::UIDtoID(u->UID), u->displacement, u->movement, (int)u->attachedBuilding, u->insideTimeout);
			u->isDead=true;
		}

		if (u->displacement==Unit::DIS_ENTERING_BUILDING)
		{
			owner->game->map.setUnit(u->posX-u->dx, u->posY-u->dy, NOUID);
			//printf("(%x)Building:: Unit(uid%d)(id%d) killed while entering. dis=%d, mov=%d, ab=%x, ito=%d \n",this, u->UID, Unit::UIDtoID(u->UID), u->displacement, u->movement, (int)u->attachedBuilding, u->insideTimeout);
			u->isDead=true;
		}
		u->attachedBuilding=NULL;
		u->activity=Unit::ACT_RANDOM;
		u->displacement=Unit::DIS_RANDOM;
		(*it)->needToRecheckMedical=true;
	}
	unitsInside.clear();

	for (it=unitsWorking.begin(); it!=unitsWorking.end(); it++)
	{
		(*it)->attachedBuilding=NULL;
		(*it)->activity=Unit::ACT_RANDOM;
		(*it)->needToRecheckMedical=true;
	}
	unitsWorking.clear();

	maxUnitWorking=0;
	maxUnitInside=0;

	buildingState=WAITING_FOR_DESTRUCTION;

	update();
}

int Building::getMidX(void)
{
	return ((posX-type->decLeft)&owner->game->map.getMaskW());
}

int Building::getMidY(void)
{
	return ((posY-type->decTop)&owner->game->map.getMaskH());
}

bool Building::findExit(int *posX, int *posY, int *dx, int *dy, bool canFly)
{
	int testX, testY;
	int exitQuality=0;
	int oldQuality;
	int exitX, exitY;
	
	//if (exitQuality<4)
	{
		testY=this->posY-1;
		oldQuality=0;
		for (testX=this->posX-1; (testX<=this->posX+type->width) ; testX++)
			if (owner->game->map.isFreeForUnit(testX, testY, canFly))
			{
				if (owner->game->map.isFreeForUnit(testX, testY-1, canFly))
					oldQuality++;
				if (owner->game->map.isRessource(testX, testY-1))
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
			if (owner->game->map.isFreeForUnit(testX, testY, canFly))
			{
				if (owner->game->map.isFreeForUnit(testX, testY+1, canFly))
					oldQuality++;
				if (owner->game->map.isRessource(testX, testY+1))
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
			if (owner->game->map.isFreeForUnit(testX, testY, canFly))
			{
				if (owner->game->map.isFreeForUnit(testX-1, testY, canFly))
					oldQuality++;
				if (owner->game->map.isRessource(testX-1, testY))
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
			if (owner->game->map.isFreeForUnit(testX, testY, canFly))
			{
				if (owner->game->map.isFreeForUnit(testX+1, testY, canFly))
					oldQuality++;
				if (owner->game->map.isRessource(testX+1, testY))
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
		bool shouldBeTrue=owner->game->map.doesPosTouchUID(exitX, exitY, UID, dx, dy);
		assert(shouldBeTrue);
		*dx=-*dx;
		*dy=-*dy;
		*posX=exitX & owner->game->map.getMaskW();
		*posY=exitY & owner->game->map.getMaskH();
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

Sint32 Building::UIDtoID(Sint32 uid)
{
	return (-1-uid)%512;
}

Sint32 Building::UIDtoTeam(Sint32 uid)
{
	return (-1-uid)/512;
}

Sint32 Building::UIDfrom(Sint32 id, Sint32 team)
{
	return -1 -id -team*512;
}

Sint32 Building::checkSum()
{
	int cs=0;
	int i;
	
	cs^=typeNum;

	cs^=buildingState;

	cs^=maxUnitWorking;
	cs^=maxUnitWorkingPreferred;
	cs^=unitsWorking.size();
	cs^=maxUnitInside;
	cs^=unitsInside.size();
	
	cs^=UID;

	cs^=posX;
	cs^=posY;

	cs^=unitStayRange;

	for (int i=0; i<NB_RESSOURCES; i++)
		cs^=ressources[i];

	cs^=hp;

	cs^=productionTimeout;
	cs^=totalRatio;
	for (i=0; i<UnitType::NB_UNIT_TYPE; i++)
	{
		cs^=ratio[i];
		cs^=percentUsed[i];
		cs=(cs<<31)|(cs>>1);
	}

	cs^=shootingStep;
	cs^=shootingCooldown;
	
	return cs;
}

Bullet::Bullet(SDL_RWops *stream)
{
	bool good=load(stream);
	assert(good);
}

Bullet::Bullet(Sint32 px, Sint32 py, Sint32 speedX, Sint32 speedY, Sint32 ticksLeft, Sint32 shootDamage, Sint32 targetX, Sint32 targetY)
{
	this->px=px;
	this->py=py;
	this->speedX=speedX;
	this->speedY=speedY;
	this->ticksLeft=ticksLeft;
	this->shootDamage=shootDamage;
	this->targetX=targetX;
	this->targetY=targetY;
}

bool Bullet::load(SDL_RWops *stream)
{
	px=SDL_ReadBE32(stream);
	py=SDL_ReadBE32(stream);
	speedX=SDL_ReadBE32(stream);
	speedY=SDL_ReadBE32(stream);
	ticksLeft=SDL_ReadBE32(stream);
	shootDamage=SDL_ReadBE32(stream);
	targetX=SDL_ReadBE32(stream);
	targetY=SDL_ReadBE32(stream);
	return true;
}

void Bullet::save(SDL_RWops *stream)
{
	SDL_WriteBE32(stream, px);
	SDL_WriteBE32(stream, py);
	SDL_WriteBE32(stream, speedX);
	SDL_WriteBE32(stream, speedY);
	SDL_WriteBE32(stream, ticksLeft);
	SDL_WriteBE32(stream, shootDamage);
	SDL_WriteBE32(stream, targetX);
	SDL_WriteBE32(stream, targetY);
}

void Bullet::step(void)
{
	if (ticksLeft>0)
	{
		//printf("bullet %d stepped to p=(%d, %d), tl=%d\n", (int)this, px, py, ticksLeft);
		px+=speedX;
		py+=speedY;
		ticksLeft--;
	}
}


