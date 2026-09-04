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
#include "Game.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include "Order.h"
#include "Bullet.h"
#include "Integrity.h"

void Building::kill(void)
{
	fprintf(logFile, "kill gid=%d buildingState=%d\n", gid, buildingState);
	if (buildingState==DEAD)
		return;


	fprintf(logFile, " still %zd unitsInside\n", unitsInside.size());
	for (std::list<Unit *>::iterator it=unitsInside.begin(); it!=unitsInside.end(); ++it)
	{
		//TODO: We should somehow try to save their lives. In training buildings they should just drop out untrained etc.
		Unit *u=*it;
		fprintf(logFile, "  guid=%d\n", u->gid);
		if (u->displacement==Unit::DIS_INSIDE)
			u->isDead=true;

		if (u->displacement==Unit::DIS_ENTERING_BUILDING)
		{
			if (u->performance[FLY])
				owner->map->setAirUnit(u->posX-u->dx, u->posY-u->dy, NOGUID);
			else
				owner->map->setGroundUnit(u->posX-u->dx, u->posY-u->dy, NOGUID);
			//printf("(%x)Building:: Unit(uid%d)(id%d) killed while entering. dis=%d, mov=%d, ab=%x, ito=%d \n",this, u->gid, Unit::UIDtoID(u->gid), u->displacement, u->movement, (int)u->attachedBuilding, u->insideTimeout);
			u->isDead=true;
		}
		u->standardRandomActivity();
	}
	unitsInside.clear();

	fprintf(logFile, " still %zd unitsWorking\n", unitsInside.size());
	for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
	{
		assert(*it);
		(*it)->standardRandomActivity();
	}
	unitsWorking.clear();

	maxUnitWorking=0;
	maxUnitWorkingLocal=0;
	maxUnitInside=0;
	desiredMaxUnitWorking = 0;
	updateCallLists();


	if (!type->isVirtual)
	{
		owner->map->setBuilding(posX, posY, type->width, type->height, NOGBID);
		owner->dirtyGlobalGradient();
		owner->map->updateForbiddenGradient(owner->teamNumber);
		owner->map->updateGuardAreasGradient(owner->teamNumber);
		owner->map->updateClearAreasGradient(owner->teamNumber);
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
	
	updateUnitsHarvesting();
	
	owner->prestige-=type->prestige;

	owner->buildingsToBeDestroyed.push_front(this);
}


bool Building::canUnitWorkHere(Unit* unit)
{
	if(type->isVirtual)
	{
		if(type->zonable[unit->typeNum])
		{
			if (unit->typeNum == WARRIOR)
			{
				int level=std::min(unit->level[ATTACK_SPEED], unit->level[ATTACK_STRENGTH]);
				if(minLevelToFlag<=level)
					return true;
			}
			else if (unit->typeNum == EXPLORER)
			{
				if(minLevelToFlag && !unit->level[MAGIC_ATTACK_GROUND])
					return false;
				else
					return true;
			}
			else if (unit->typeNum == WORKER)
			{
				return true;
			}

		}
	}
	else if(unit->typeNum ==  WORKER)
	{
		int actLevel=unit->level[HARVEST];
		if(type->level <= actLevel)
			return true;
	}
	return false;

}



void Building::removeUnitFromWorking(Unit* unit)
{
	unitsWorking.remove(unit);
	updateCallLists();
}

void Building::insertUnitToHarvesting(Unit* unit)
{
	unitsHarvesting.push_front(unit);
}


void Building::removeUnitFromHarvesting(Unit* unit)
{
	unitsHarvesting.remove(unit);
}


void Building::removeUnitFromInside(Unit* unit)
{
	unitsInside.remove(unit);
	updateCallLists();
}



void Building::updateRessourcesPointer()
{
	if(!type->useTeamRessources)
	{
		ressources=localRessource;
	}
	else
	{
		ressources=owner->teamRessources;
	}
}



void Building::addRessourceIntoBuilding(int ressourceType)
{
	ressources[ressourceType]+=type->multiplierRessource[ressourceType];
	//You can not exceed the maximum amount
	ressources[ressourceType] = std::min(ressources[ressourceType], type->maxRessource[ressourceType]);
	switch (constructionResultState)
	{
		case NO_CONSTRUCTION:
		break;
		case NEW_BUILDING:
		case UPGRADE:
		{
			hp+=type->hpInc;
			hp = std::min(hp, type->hpMax);
		}
		break;

		case REPAIR:
		{
			int totRessources=0;
			for (unsigned i=0; i<MAX_NB_RESSOURCES; i++)
				totRessources+=type->maxRessource[i];
			hp += type->hpMax/totRessources;
			hp = std::min(hp, type->hpMax);
		}
		break;

		default:
			assert(false);
	}
	update();
}



void Building::removeRessourceFromBuilding(int ressourceType)
{
	ressources[ressourceType]-=type->multiplierRessource[ressourceType];
	ressources[ressourceType]= std::max(ressources[ressourceType], 0);
	updateCallLists();
}



int Building::getMidX(void)
{
	return ((posX-type->decLeft)&owner->map->getMaskW());
}

int Building::getMidY(void)
{
	return ((posY-type->decTop)&owner->map->getMaskH());
}

bool Building::findGroundExit(int *posX, int *posY, int *dx, int *dy, bool canSwim)
{
	int testX, testY;
	int exitQuality=0;
	int oldQuality;
	int exitX=0, exitY=0;

	// TODO: Introduce a border iterator for rectangles

	// if (exitQuality<4)
	{
		testY=this->posY-1;
		oldQuality=0;
		for (testX=this->posX-1; testX<=this->posX+type->width ; testX++)
			checkGroundExitQuality(testX,testY,testX,testY-1,exitX,exitY,exitQuality,oldQuality,canSwim);
	}
	if (exitQuality<4)
	{
		testY=this->posY+type->height;
		oldQuality=0;
		for (testX=this->posX-1; (testX<=this->posX+type->width) ; testX++)
			checkGroundExitQuality(testX,testY,testX,testY+1,exitX,exitY,exitQuality,oldQuality,canSwim);
	}
	if (exitQuality<4)
	{
		oldQuality=0;
		testX=this->posX-1;
		for (testY=this->posY-1; (testY<=this->posY+type->height) ; testY++)
			checkGroundExitQuality(testX,testY,testX-1,testY,exitX,exitY,exitQuality,oldQuality,canSwim);
	}
	if (exitQuality<4)
	{
		oldQuality=0;
		testX=this->posX+type->width;
		for (testY=this->posY-1; (testY<=this->posY+type->height) ; testY++)
			checkGroundExitQuality(testX,testY,testX+1,testY,exitX,exitY,exitQuality,oldQuality,canSwim);
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

void Building::checkGroundExitQuality(
		const int testX,
		const int testY,
		const int extraTestX,
		const int extraTestY,
		int & exitX,
		int & exitY,
		int & exitQuality,
		int & oldQuality,
		bool canSwim)
{
	Uint32 me=owner->me;
	if (owner->map->isFreeForGroundUnit(testX, testY, canSwim, me))
	{
		if (owner->map->isFreeForGroundUnit(extraTestX, extraTestY, canSwim, me))
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

int Building::getLongLevel(void)
{
	return ((type->level)<<1)+1-type->isBuildingSite;
}

void Building::computeFlagStatLocal(int *goingTo, int *onSpot)
{
	*goingTo = 0;
	*onSpot = 0;

	Sint32 unitStayRangeLocalSquare = (1+unitStayRangeLocal)*(1+unitStayRangeLocal);
	for (std::list<Unit *>::iterator ui=unitsWorking.begin(); ui!=unitsWorking.end(); ++ui)
	{
		Sint32 distSquareLocal = owner->map->warpDistSquare(posXLocal, posYLocal, (*ui)->posX, (*ui)->posY);
		if (distSquareLocal < unitStayRangeLocalSquare)
			(*onSpot)++;
		else
			(*goingTo)++;
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

int Building::availableHappynessLevel()
{
	int inside = (int)unitsInside.size();
	if (ressources[CORN] <= inside)
		return 0;
	int happyness = 1;
	for (int i = 0; i < HAPPYNESS_COUNT; i++)
		if (ressources[i + HAPPYNESS_BASE]  >inside)
			happyness++;
	return happyness;
}

bool Building::canConvertUnit(void)
{
	assert(type->canFeedUnit);
	return
			canNotConvertUnitTimer<=0 &&
			((int)unitsInside.size()<ressources[CORN]) && 
			((int)unitsInside.size()<maxUnitInside);
}

bool Building::integrity()
{
	checkInvariant(unitsWorking.size()>=0);
	checkInvariant((int)unitsWorking.size()<=Unit::MAX_COUNT);
	for (std::list<Unit *>::iterator  it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
	{
		checkInvariant(*it);
		checkInvariant(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		checkInvariant((*it)->attachedBuilding==this);
	}

	checkInvariant(unitsInside.size()>=0);
	checkInvariant((int)unitsInside.size()<=Unit::MAX_COUNT);
	for (std::list<Unit *>::iterator  it=unitsInside.begin(); it!=unitsInside.end(); ++it)
	{
		checkInvariant(*it);
		checkInvariant(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		checkInvariant((*it)->attachedBuilding==this);
	}
	for (std::list<Unit *>::iterator  it=unitsHarvesting.begin(); it!=unitsHarvesting.end(); ++it)
	{
		checkInvariant(*it);
		checkInvariant((*it)->targetBuilding==this);
	}
	return true;
}

Uint32 Building::checkSum(std::vector<Uint32> *checkSumsVector)
{
	int cs=0;

	cs^=typeNum;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [0]

	cs^=buildingState;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [1]
	cs=(cs<<31)|(cs>>1);

	cs^=constructionResultState;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [2]
	cs=(cs<<31)|(cs>>1);

	cs^=maxUnitWorking;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [3]

	cs^=maxUnitWorkingFuture;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [4]

	cs^=maxUnitWorkingPreferred;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [5]

	cs^=maxUnitWorkingPrevious;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [7]

	cs^=desiredMaxUnitWorking;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [8]

	cs^=unitsWorking.size();
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [9]

	cs^=subscriptionWorkingTimer;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [10]

	cs^=unitsInside.size();
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [11]
	cs=(cs<<31)|(cs>>1);

	cs^=posX;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [12]

	cs^=posY;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [13]
	cs=(cs<<31)|(cs>>1);

	cs^=unitStayRange;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [14]

	for (int i=0; i<MAX_RESSOURCES; i++)
		cs^=localRessource[i];
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [15]
	cs=(cs<<31)|(cs>>1);

	cs^=hp;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [16]

	cs^=productionTimeout;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [17]


	cs^=totalRatio;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [18]


	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		cs^=ratio[i];
		cs^=percentUsed[i];
		cs=(cs<<31)|(cs>>1);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [19]

	cs^=shootingStep;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [20]


	cs^=shootingCooldown;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [21]


	cs^=bullets;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [22]
	cs=(cs<<31)|(cs>>1);

	cs^=seenByMask;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [23]

	cs^=gid;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [24]

	
	cs^=unitsHarvesting.size();
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [25]
	
	return cs;
}
