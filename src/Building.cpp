/*
 * Globulation 2 building
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "Building.h"
#include "BuildingType.h"
#include "Team.h"
#include "Game.h"
#include "Utilities.h"
#include <list>


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

	// Flag specific
	unitStayRange=SDL_ReadBE32(stream);
	unitStayRangeLocal=unitStayRange;

	// Building Specific
	for (int i=0; i<NB_RESSOURCES; i++)
	{
		ressources[i]=SDL_ReadBE32(stream);
	}

	// quality parameters
	hp=SDL_ReadBE32(stream);

	// prefered parameters
	productionTimeout=SDL_ReadBE32(stream);
	totalRatio=SDL_ReadBE32(stream);
	for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
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
}

void Building::save(SDL_RWops *stream)
{
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
	for (int i=0; i<NB_RESSOURCES; i++)
	{
		SDL_WriteBE32(stream, ressources[i]);
	}

	// quality parameters
	SDL_WriteBE32(stream, hp);

	// prefered parameters
	SDL_WriteBE32(stream, productionTimeout);
	SDL_WriteBE32(stream, totalRatio);
	for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
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
}

void Building::loadCrossRef(SDL_RWops *stream, BuildingsTypes *types, Team *owner)
{
	// units
	maxUnitInside=SDL_ReadBE32(stream);
	int nbWorking=SDL_ReadBE32(stream);
	unitsWorking.clear();
	for (int i=0; i<nbWorking; i++)
	{
		unitsWorking.push_front(owner->myUnits[Unit::UIDtoID(SDL_ReadBE32(stream))]);
	}
	int nbWorkingSubscribe=SDL_ReadBE32(stream);
	unitsWorkingSubscribe.clear();
	for (int i=0; i<nbWorkingSubscribe; i++)
	{
		unitsWorkingSubscribe.push_front(owner->myUnits[Unit::UIDtoID(SDL_ReadBE32(stream))]);
	}
	lastWorkingSubscribe=SDL_ReadBE32(stream);
	
	
	maxUnitWorking=SDL_ReadBE32(stream);
	maxUnitWorkingPreferred=SDL_ReadBE32(stream);
	maxUnitWorkingLocal=maxUnitWorking;
	int nbInside=SDL_ReadBE32(stream);
	unitsInside.clear();
	for (int i=0; i<nbInside; i++)
	{
		unitsInside.push_front(owner->myUnits[Unit::UIDtoID(SDL_ReadBE32(stream))]);
	}
	int nbInsideSubscribe=SDL_ReadBE32(stream);
	unitsInsideSubscribe.clear();
	for (int i=0; i<nbInsideSubscribe; i++)
	{
		unitsInsideSubscribe.push_front(owner->myUnits[Unit::UIDtoID(SDL_ReadBE32(stream))]);
	}
	lastInsideSubscribe=SDL_ReadBE32(stream);
}

void Building::saveCrossRef(SDL_RWops *stream)
{
	// units
	SDL_WriteBE32(stream, maxUnitInside);
	SDL_WriteBE32(stream, unitsWorking.size());
	for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); it++)
	{
		SDL_WriteBE32(stream, (*it)->UID);
	}
	SDL_WriteBE32(stream, unitsWorkingSubscribe.size());
	for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
	{
		SDL_WriteBE32(stream, (*it)->UID);
	}
	SDL_WriteBE32(stream, lastWorkingSubscribe);
	
	SDL_WriteBE32(stream, maxUnitWorking);
	SDL_WriteBE32(stream, maxUnitWorkingPreferred);
	SDL_WriteBE32(stream, unitsInside.size());
	for (std::list<Unit *>::iterator it=unitsInside.begin(); it!=unitsInside.end(); it++)
	{
		SDL_WriteBE32(stream, (*it)->UID);
	}
	SDL_WriteBE32(stream, unitsInsideSubscribe.size());
	for (std::list<Unit *>::iterator it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
	{
		SDL_WriteBE32(stream, (*it)->UID);
	}
	SDL_WriteBE32(stream, lastInsideSubscribe);
}

bool Building::isRessourceFull(void)
{
	bool isFull=true;
	for (int i=0; i<NB_RESSOURCES; i++)
	{
		if (ressources[i]<type->maxRessource[i])
			isFull=false;
	}
	return isFull;
}

int Building::neededRessource(void)
{
	for (int i=0; i<NB_RESSOURCES; i++)
	{
		if (ressources[i]<type->maxRessource[i])
			return i;
	}
	return -1;
}

void Building::cancelUpgrade(void)
{
	owner->game->map.setBuilding(posX, posY, type->width, type->height, NOUID);
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;
	
	if (type->isBuildingSite)
	{
		int lastLevelTypeNum=type->lastLevelTypeNum;
		if (lastLevelTypeNum!=-1)
		{
			typeNum=lastLevelTypeNum;
			type=Game::buildingsTypes.getBuildingType(lastLevelTypeNum);
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
		assert(false);
	
	posX=midPosX+type->decLeft;
	posY=midPosY+type->decTop;
	
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
	
	owner->game->map.setMapDiscovered(posX-1, posY-1, type->width+2, type->height+2, owner->teamNumber);
	
	update();
}

void Building::update(void)
{
	if (buildingState==DEAD)
		return;

	if (buildingState==WAITING_FOR_DESTRUCTION)
	{
		if ((unitsWorking.size()==0) && (unitsInside.size()==0) && (unitsWorkingSubscribe.size()==0) && (unitsInsideSubscribe.size()==0))
		{
			owner->game->map.setBuilding(posX, posY, type->width, type->height, NOUID);
			buildingState=DEAD;
			owner->buildingsToBeDestroyed.push_front(UIDtoID(UID));
		}
		else
		{
			printf("(%d)Building wait for destruction, uws=%d, uis=%d, uwss=%d, uiss=%d.\n", UID, unitsWorking.size(), unitsInside.size(), unitsWorkingSubscribe.size(), unitsInsideSubscribe.size());
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
			printf("(%d)Building wait for upgrade, uws=%d, uis=%d, uwss=%d, uiss=%d.\n", UID, unitsWorking.size(), unitsInside.size(), unitsWorkingSubscribe.size(), unitsInsideSubscribe.size());
	}

	// TODO : save the knowledge weather or not the building is already in the Call list in the building
	if (unitsWorking.size()<(unsigned)maxUnitWorking)
	{
		// add itself in Call lists
		for (int i=0; i<NB_ABILITY; i++)
		{
			if (type->job[i])
				owner->job[i].push_front(this);
			if (type->attract[i])
				owner->attract[i].push_front(this);
		}
	}
	else
	{
		// delete itself from all Call lists
		for (int i=0; i<NB_ABILITY; i++)
		{
			if (type->job[i])
			{
				//if (owner->job[i].size()==1)
				//	printf("last job removed (ability=%d)\n", i);
				owner->job[i].remove(this);
			}

			if (type->attract[i])
			{
				//if (owner->attract[i].size()==1)
				//	printf("last attract removed (ability=%d)\n", i);
				owner->attract[i].remove(this);
			}

		}


		while (unitsWorking.size()>(unsigned)maxUnitWorking) // TODO : the same with insides units
		{
			int maxDistSquare=0;

			Unit *fu=NULL;
			std::list<Unit *>::iterator ittemp;
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
		for (int i=0; i<NB_ABILITY; i++)
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
		for (int i=0; i<NB_ABILITY; i++)
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
			for(int i=0; i<NB_RESSOURCES; i++)
				ressources[i]-=type->maxRessource[i];

			typeNum=type->nextLevelTypeNum;
			type=Game::buildingsTypes.getBuildingType(type->nextLevelTypeNum);

			// we don't need any worker any more
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
			owner->game->map.setMapDiscovered(posX-1, posY-1, type->width+2, type->height+2, owner->teamNumber);
			
			// we need to do an update again
			update();
		}
		else
		{
			owner->job[HARVEST].remove(this);
		}
	}
}

bool Building::tryToUpgradeRoom(void)
{
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;

	int nextLevelTypeNum=type->nextLevelTypeNum;
	BuildingType *nextBt=Game::buildingsTypes.getBuildingType(type->nextLevelTypeNum);
	int newPosX=midPosX+nextBt->decLeft;
	int newPosY=midPosY+nextBt->decTop;

	int newWidth=nextBt->width;
	int newHeight=nextBt->height;
	int lastNewPosX=newPosX+newWidth;
	int lastNewPosY=newPosY+newHeight;

	bool isRoom=true;

	for(int x=newPosX; x<lastNewPosX; x++)
		for(int y=newPosY; y<lastNewPosY; y++)
		{
			// TODO : put this code in map.cpp to optimise speed.
			Sint32 UID=owner->game->map.getUnit(x, y);
			if ( (!owner->game->map.isGrass(x, y)) || ((UID!=this->UID) && (UID!=NOUID)) )
				isRoom=false;
		}

	if (isRoom)
	{
		owner->game->map.setBuilding(posX, posY, type->decLeft, type->decLeft, NOUID);
		owner->game->map.setBuilding(newPosX, newPosY, newWidth, newHeight, UID);

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

		// flag usefull :
		unitStayRange=type->defaultUnitStayRange;
		unitStayRangeLocal=unitStayRange;

		// quality parameters
		hp=type->hpInit; // (Uint16)

		// prefered parameters
		productionTimeout=type->unitProductionTime;

		totalRatio=0;
		for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
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
	BuildingType *bt=Game::buildingsTypes.getBuildingType(nltn);
	int x=posX+bt->decLeft-type->decLeft;
	int y=posY+bt->decTop -type->decTop ;
	int w=bt->width;
	int h=bt->height;

	if (bt->isVirtual)
	{
		for (int dy=y; dy<y+h; dy++)
			for (int dx=x; dx<x+w; dx++)
			{
				Sint32 uid=owner->game->map.getUnit(dx, dy);
				if ((uid<0)&&(uid!=NOUID)&&(uid!=UID))
					return false;
			}
		return true;
	}
	else
	{
		for (int dy=y; dy<y+h; dy++)
			for (int dx=x; dx<x+w; dx++)
			{
				Sint32 uid=owner->game->map.getUnit(dx, dy);
				if (((uid<0)&&(uid!=NOUID)&&(uid!=UID))||(!(owner->game->map.isGrass(dx, dy))))
					return false;
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
	for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
	{
		(*it)->attachedBuilding=NULL;
		(*it)->subscribed=false;
		(*it)->activity=Unit::ACT_RANDOM;
		(*it)->needToRecheckMedical=true;
	}
	unitsWorkingSubscribe.clear();
	
	for (std::list<Unit *>::iterator it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
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

void Building::subscribeForWorkingStep()
{
	lastWorkingSubscribe++;
	if (fullWorking())
	{
		for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
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
		if ((signed)unitsWorking.size()<maxUnitWorking)
		{
			int mindist=owner->game->map.getW()*owner->game->map.getW();
			Unit *u=NULL;
			for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
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
				unitsWorkingSubscribe.remove(u);
				unitsWorking.push_back(u);
				u->unsubscribed();
				update();
			}
		}
		
		if ((signed)unitsWorking.size()>=maxUnitWorking)
		{
			for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
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

void Building::subscribeForInsideStep()
{
	lastInsideSubscribe++;
	if (fullInside())
	{
		for (std::list<Unit *>::iterator it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
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
			for (std::list<Unit *>::iterator it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
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
			for (std::list<Unit *>::iterator it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
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
	if (ressources[CORN]>=type->ressourceForOneUnit)
		productionTimeout--;

	if (productionTimeout<0)
	{
		// We find the kind of unit we have to create:
		float propotion;
		float minPropotion=1.0;
		int minType=-1;
		for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
		{
			propotion=((float)percentUsed[i])/((float)ratio[i]);
			if (propotion<=minPropotion)
			{
				minPropotion=propotion;
				minType=i;
			}
		}

		if (minType==-1)
			minType=0;
		assert(minType>=0);
		assert(minType<UnitType::NB_UNIT_TYPE);
		if (minType<0 || minType>=UnitType::NB_UNIT_TYPE)
			minType=0;

		// We get the unit UnitType:
		int posX, posY, dx, dy;
		UnitType *ut=owner->race.getUnitType((UnitType::TypeNum)minType, 0);
		Unit *u;

		// Is there a place to exit ?
		if (findExit(&posX, &posY, &dx, &dy, ut->performance[FLY]))
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
				for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
				{
					if (percentUsed[i]<ratio[i])
						allDone=false;
				}
				if (allDone)
					for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
						percentUsed[i]=0;
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

	unsigned range=type->shootingRange;
	if (shootingStep>=2*range)
		shootingStep=0;
	else
		shootingStep++;

	int pos=shootingStep>>1;
	int dir=shootingStep&0X1;
	int midX=getMidX();
	int midY=getMidY();
	Uint32 enemies=owner->enemies;
	bool targetFound=false;
	int width =type->width &0x1;
	int height=type->height&0x1;
	Map *map=&(owner->game->map);

	int targetX;
	int targetY;

	for (unsigned i=0; i<=range && !targetFound; i++)
	for (int k=0; k<2 && !targetFound; k++)
	for (int l=0; l<2 && !targetFound; l++)
	{
		targetX=midX+(k)*(  dir)*(pos+1)-(1-k)*(  dir)*(pos)+(l)*(1-dir)*(i+width +1)-(1-l)*(1-dir)*(i);
		targetY=midY+(k)*(1-dir)*(pos+1)-(1-k)*(1-dir)*(pos)+(l)*(  dir)*(i+height+1)-(1-l)*(  dir)*(i);
		//printf("midX=%d, targetX=%d, k=%d, l=%d, pos=%d, dir=%d, i=%d \n", midX, targetX, k, l, pos, dir, i);
		//printf("midY=%d, targetY=%d, k=%d, l=%d, pos=%d, dir=%d, i=%d \n", midY, targetY, k, l, pos, dir, i);

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

	if (targetFound)
	{
		//printf("%d found target found: (%d, %d) \n", (int)this, targetX, targetY);
		Sector *s=owner->game->map.getSector(midX, midY);

		int px, py;
		px=((posX)<<5)+((type->width)<<4);
		py=((posY)<<5)+((type->height)<<4);

		int speedX, speedY, ticksLeft;

		// TODO : shall we really uses shootSpeed ?
		int dpx=(targetX*32)+16-4-px; // 4 is the half size of the bullet
		int dpy=(targetY*32)+16-4-py;

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

		//printf("%d insert: (px=%d, py=%d, sx=%d, sy=%d, tl=%d, sd=%d) \n", (int)this, px, py, speedX, speedY, ticksLeft, type->shootDamage);
		s->bullets.push_front(b);

		shootingCooldown=SHOOTING_COOLDOWN_MAX;
	}

}

void Building::kill(void)
{
	for (std::list<Unit *>::iterator it=unitsInside.begin(); it!=unitsInside.end(); it++)
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

	for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); it++)
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
	bool found=false;

	if (!found)
	{
		testY=this->posY-1;
		for (testX=this->posX-1; (testX<=this->posX+type->width) ; testX++)
			if (owner->game->map.isFreeForUnit(testX, testY, canFly))
			{
				found=true;
				break;
			}
	}

	if (!found)
	{
		testY=this->posY+type->height;
		for (testX=this->posX-1; (testX<=this->posX+type->width) ; testX++)
			if (owner->game->map.isFreeForUnit(testX, testY, canFly))
			{
				found=true;
				break;
			}
	}

	if (!found)
	{
		testX=this->posX-1;
		for (testY=this->posY-1; (testY<=this->posY+type->height) ; testY++)
			if (owner->game->map.isFreeForUnit(testX, testY, canFly))
			{
				found=true;
				break;
			}
	}

	if (!found)
	{
		testX=this->posX+type->width;
		for (testY=this->posY-1; (testY<=this->posY+type->height) ; testY++)
			if (owner->game->map.isFreeForUnit(testX, testY, canFly))
			{
				found=true;
				break;
			}
	}

	if (found)
	{
		bool shouldBeTrue=owner->game->map.doesPosTouchUID(testX, testY, UID, dx, dy);
		assert(shouldBeTrue);
		*dx=-*dx;
		*dy=-*dy;
		*posX=testX & owner->game->map.getMaskW();
		*posY=testY & owner->game->map.getMaskH();
		return true;
	}
	return false;
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
	for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
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
	load(stream);
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

void Bullet::load(SDL_RWops *stream)
{
	px=SDL_ReadBE32(stream);
	py=SDL_ReadBE32(stream);
	speedX=SDL_ReadBE32(stream);
	speedY=SDL_ReadBE32(stream);
	ticksLeft=SDL_ReadBE32(stream);
	shootDamage=SDL_ReadBE32(stream);
	targetX=SDL_ReadBE32(stream);
	targetY=SDL_ReadBE32(stream);
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


