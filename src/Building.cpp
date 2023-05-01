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

Building::Building(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor)
{
	for (int i=0; i<2; i++)
	{
		globalGradient[i]=NULL;
		localResources[i]=NULL;
	}
	logFile = globalContainer->logFileManager->getFile("Building.log");
	load(stream, types, owner, versionMinor);
}

Building::Building(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, BuildingsTypes *types, Sint32 unitWorking, Sint32 unitWorkingFuture)
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
	shortTypeNum = type->shortTypeNum;
	maxUnitInside = type->maxUnitInside;
	maxUnitWorking = unitWorking;
	maxUnitWorkingLocal = maxUnitWorking;
	maxUnitWorkingPreferred = maxUnitWorking;
	maxUnitWorkingFuture = unitWorkingFuture;
	maxUnitWorkingPrevious = 0;
	desiredMaxUnitWorking = maxUnitWorking;
	subscriptionWorkingTimer = 0;
	priority = 0;
	priorityLocal = 0;
	oldPriority = 0;

	// position
	posX=x;
	posY=y;
	posXLocal=posX;
	posYLocal=posY;

	underAttackTimer=0;
	canNotConvertUnitTimer=0;

	// flag useful :
	unitStayRange=type->defaultUnitStayRange;
	unitStayRangeLocal=unitStayRange;
	for(int i=0; i<BASIC_COUNT; i++)
		clearingResources[i]=true;
	clearingResources[STONE]=false;
	memcpy(clearingResourcesLocal, clearingResources, sizeof(bool)*BASIC_COUNT);
	minLevelToFlag=0;
	minLevelToFlagLocal=minLevelToFlag;

	// building specific :
	for(int i=0; i<MAX_NB_RESOURCES; i++)
		localResource[i]=0;
	updateResourcesPointer();

	// quality parameters
	hp=type->hpInit; // (Uint16)

	// preferred parameters

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

	receiveResourceMask=0;
	sendResourceMask=0;
	receiveResourceMaskLocal=0;
	sendResourceMaskLocal=0;

	shootingStep=0;
	shootingCooldown=SHOOTING_COOLDOWN_MAX;
	bullets=0;

	seenByMask=0;

	inCanFeedUnit=LS_UNKNOWN;
	inCanHealUnit=LS_UNKNOWN;
	callListState=0;

	for (int i=0; i<NB_ABILITY; i++)
		inUpgrade[i]=LS_UNKNOWN;

	for (int i=0; i<2; i++)
	{
		globalGradient[i]=NULL;
		localResources[i]=NULL;
		dirtyLocalGradient[i]=true;
		locked[i]=false;
		lastGlobalGradientUpdateStepCounter[i]=0;

		localResources[i]=0;
		localResourcesCleanTime[i]=0;
		anyResourceToClear[i]=0;
	}

	verbose=false;

	lastShootStep = 0xFFFFFFFF;
	lastShootSpeedX = 0;
	lastShootSpeedY = 0;

	for(int i=0; i<UnitCantWorkReasonSize; ++i)
	{
		unitsFailingRequirements[i]=0;
	}
	unitsHarvesting.clear();
}

Building::~Building()
{
	freeGradients();
}

void Building::freeGradients()
{
	for (int i=0; i<2; i++)
	{
		if (globalGradient[i])
		{
			delete[] globalGradient[i];
			globalGradient[i] = NULL;
		}
		if (localResources[i])
		{
			delete[] localResources[i];
			localResources[i] = NULL;
		}
		dirtyLocalGradient[i] = true;
		locked[i] = false;
		lastGlobalGradientUpdateStepCounter[i] = 0;

		localResourcesCleanTime[i] = 0;
		anyResourceToClear[i] = 0;
	}
}

void Building::load(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor)
{
	stream->readEnterSection("Building");

	// construction state
	buildingState = (BuildingState)stream->readUint32("buildingState");
	constructionResultState = (ConstructionResultState)stream->readUint32("constructionResultState");

	// identity
	gid = stream->readUint16("gid");
	this->owner = owner;

	// position
	posX = stream->readSint32("posX");
	posY = stream->readSint32("posY");
	posXLocal = posX;
	posYLocal = posY;

	if(versionMinor>=61)
		underAttackTimer = stream->readUint8("underAttackTimer");
	else
		underAttackTimer = 0;
	if(versionMinor>=81)
		canNotConvertUnitTimer = stream->readUint8("canNotConvertUnitTimer");
	else
		canNotConvertUnitTimer = 150;

	// priority
	if(versionMinor>=79)
	{
		priority = stream->readSint32("priority");
		priorityLocal = stream->readSint32("priorityLocal");
		oldPriority = priority;
	}
	else
	{
		priority = 0;
		priorityLocal = 0;
		oldPriority = 0;
	}

	// Flag specific
	unitStayRange = stream->readUint32("unitStayRange");
	unitStayRangeLocal = unitStayRange;

	for (int i=0; i<BASIC_COUNT; i++)
	{
		std::ostringstream oss;
		oss << "clearingRessources[" << i << "]";
		clearingResources[i] = (bool)stream->readSint32(oss.str().c_str());
	}
	assert(clearingResources[STONE] == false);

	memcpy(clearingResourcesLocal, clearingResources, sizeof(bool)*BASIC_COUNT);

	minLevelToFlag = stream->readSint32("minLevelToFlag");
	minLevelToFlagLocal = minLevelToFlag;

	// Building Specific
	for (int i=0; i<MAX_NB_RESOURCES; i++)
	{
		std::ostringstream oss;
		oss << "localRessource[" << i << "]";
		localResource[i] = stream->readSint32(oss.str().c_str());
	}

	// quality parameters
	hp = stream->readSint32("hp");

	// preferred parameters
	productionTimeout = stream->readSint32("productionTimeout");
	totalRatio = stream->readSint32("totalRatio");
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		{
			std::ostringstream oss;
			oss << "ratio[" << i << "]";
			ratioLocal[i] = ratio[i] = stream->readSint32(oss.str().c_str());
		}
		{
			std::ostringstream oss;
			oss << "percentUsed[" << i << "]";
			percentUsed[i] = stream->readSint32(oss.str().c_str());
		}
	}

	receiveResourceMask = stream->readUint32("receiveRessourceMask");
	sendResourceMask = stream->readUint32("sendRessourceMask");
	receiveResourceMaskLocal = receiveResourceMask;
	sendResourceMaskLocal = sendResourceMask;

	shootingStep = stream->readUint32("shootingStep");
	shootingCooldown = stream->readSint32("shootingCooldown");
	bullets = stream->readSint32("bullets");

	// type
	// FIXME : do not save typenum but name/isBuildingSite/level
	typeNum = stream->readSint32("typeNum");
	type = types->get(typeNum);
	assert(type);
	updateResourcesPointer();

	// reload data from type
	shortTypeNum = type->shortTypeNum;
	maxUnitInside = type->maxUnitInside;
	maxUnitWorking = type->maxUnitWorking;

	// init data not loaded
	maxUnitWorkingLocal = maxUnitWorking;
	maxUnitWorkingPreferred = 1;
	maxUnitWorkingFuture = 1;
	desiredMaxUnitWorking = maxUnitWorking;
	subscriptionWorkingTimer = 0;

	owner->prestige += type->prestige;

	seenByMask = stream->readUint32("seenByMaskk");

	inCanFeedUnit=LS_UNKNOWN;
	inCanHealUnit=LS_UNKNOWN;
	callListState = 0;

	for (int i=0; i<NB_ABILITY; i++)
		inUpgrade[i] = LS_UNKNOWN;

	freeGradients();

	verbose = false;
	stream->readLeaveSection();

	lastShootStep = 0xFFFFFFFF;
	lastShootSpeedX = 0;
	lastShootSpeedY = 0;


	for(int i=0; i<UnitCantWorkReasonSize; ++i)
	{
		unitsFailingRequirements[i]=0;
	}
}

void Building::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Building");

	// construction state
	stream->writeUint32((Uint32)buildingState, "buildingState");
	stream->writeUint32((Uint32)constructionResultState, "constructionResultState");

	// identity
	stream->writeUint16(gid, "gid");
	// we drop team

	// position
	stream->writeSint32(posX, "posX");
	stream->writeSint32(posY, "posY");

	stream->writeUint8(underAttackTimer, "underAttackTimer");
	stream->writeUint8(canNotConvertUnitTimer, "canNotConvertUnitTimer");

	// priority
	stream->writeSint32(priority, "priority");
	stream->writeSint32(priorityLocal, "priorityLocal");

	// Flag specific
	stream->writeUint32(unitStayRange, "unitStayRange");
	for(int i=0; i<BASIC_COUNT; i++)
	{
		std::ostringstream oss;
		oss << "clearingRessources[" << i << "]";
		stream->writeSint32(clearingResources[i], oss.str().c_str());
	}
	stream->writeSint32(minLevelToFlag, "minLevelToFlag");

	// Building Specific
	for (int i=0; i<MAX_NB_RESOURCES; i++)
	{
		std::ostringstream oss;
		oss << "localRessource[" << i << "]";
		stream->writeSint32(localResource[i], oss.str().c_str());
	}

	// quality parameters
	stream->writeSint32(hp, "hp");

	// preferred parameters
	stream->writeSint32(productionTimeout, "productionTimeout");
	stream->writeSint32(totalRatio, "totalRatio");
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		{
			std::ostringstream oss;
			oss << "ratio[" << i << "]";
			stream->writeSint32(ratio[i], oss.str().c_str());
		}
		{
			std::ostringstream oss;
			oss << "percentUsed[" << i << "]";
			stream->writeSint32(percentUsed[i], oss.str().c_str());
		}
	}

	stream->writeUint32(receiveResourceMask, "receiveRessourceMask");
	stream->writeUint32(sendResourceMask, "sendRessourceMask");

	stream->writeUint32(shootingStep, "shootingStep");
	stream->writeSint32(shootingCooldown, "shootingCooldown");
	stream->writeSint32(bullets, "bullets");

	// type
	stream->writeUint32(typeNum, "typeNum");
	// we drop type

	stream->writeUint32(seenByMask, "seenByMask");

	stream->writeLeaveSection();
}

void Building::loadCrossRef(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor)
{
	stream->readEnterSection("Building");
	fprintf(logFile, "loadCrossRef (%d)\n", gid);

	// units
	maxUnitInside = stream->readSint32("maxUnitInside");
	assert(maxUnitInside < 65536);

	unsigned nbWorking = stream->readUint32("nbWorking");
	fprintf(logFile, " nbWorking=%d\n", nbWorking);
	unitsWorking.clear();
	for (unsigned i=0; i<nbWorking; i++)
	{
		std::ostringstream oss;
		oss << "unitsWorking[" << i << "]";
		Unit *unit = owner->myUnits[Unit::GIDtoID(stream->readUint16(oss.str().c_str()))];
		assert(unit);
		unitsWorking.push_front(unit);
	}

	subscriptionWorkingTimer = stream->readSint32("subscriptionWorkingTimer");
	maxUnitWorking = stream->readSint32("maxUnitWorking");
	maxUnitWorkingPreferred = stream->readSint32("maxUnitWorkingPreferred");
	if(versionMinor>=65)
		maxUnitWorkingPrevious = stream->readSint32("maxUnitWorkingPrevious");
	else
		maxUnitWorkingPrevious = maxUnitWorkingPreferred;
	if(versionMinor>=70)
		maxUnitWorkingFuture = stream->readSint32("maxUnitWorkingFuture");
	maxUnitWorkingLocal = maxUnitWorking;
	desiredMaxUnitWorking = maxUnitWorking;

	if(versionMinor>=74 && versionMinor<77)
	{
		stream->readSint32("unitsFailingRequirements");
	}
	else if(versionMinor>=77)
	{
		stream->readEnterSection("unitsFailingRequirements");
		for(int i=0; i<UnitCantWorkReasonSize; ++i)
		{
			stream->readEnterSection(i);
			unitsFailingRequirements[i]=stream->readUint32("unitsFailingRequirements");
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}

	unsigned nbInside = stream->readUint32("nbInside");
	fprintf(logFile, " nbInside=%d\n", nbInside);
	unitsInside.clear();
	for (unsigned i=0; i<nbInside; i++)
	{
		std::ostringstream oss;
		oss << "unitsInside[" << i << "]";
		Unit *unit = owner->myUnits[Unit::GIDtoID(stream->readUint16(oss.str().c_str()))];
		assert(unit);
		unitsInside.push_front(unit);
	}
	
	if (versionMinor>=80)
	{
		unsigned nbHarvesting = stream->readUint32("nbHarvesting");
		fprintf(logFile, " nbHarvesting=%d\n", nbHarvesting);
		unitsHarvesting.clear();
		for (unsigned i=0; i<nbHarvesting; i++)
		{
			std::ostringstream oss;
			oss << "unitsHarvesting[" << i << "]";
			Unit *unit = owner->myUnits[Unit::GIDtoID(stream->readUint16(oss.str().c_str()))];
			assert(unit);
			unitsHarvesting.push_front(unit);
		}
	}

	stream->readLeaveSection();
}

void Building::saveCrossRef(GAGCore::OutputStream *stream)
{
	unsigned i;

	stream->writeEnterSection("Building");
	fprintf(logFile, "saveCrossRef (%d)\n", gid);

	// units
	stream->writeSint32(maxUnitInside, "maxUnitInside");
	//TODO: std::list::size() is O(n). We should investigate
	//if our intense use of this has an impact on overall performance.
	//Stephane and nuage suggested to store and update size in a variable
	//what is faster but also more error prone.
	stream->writeUint32(unitsWorking.size(), "nbWorking");
	fprintf(logFile, " nbWorking=%zd\n", unitsWorking.size());
	i = 0;
	for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		std::ostringstream oss;
		oss << "unitsWorking[" << i++ << "]";
		stream->writeUint16((*it)->gid, oss.str().c_str());
	}

	stream->writeSint32(subscriptionWorkingTimer, "subscriptionWorkingTimer");
	stream->writeSint32(maxUnitWorking, "maxUnitWorking");
	stream->writeSint32(maxUnitWorkingPreferred, "maxUnitWorkingPreferred");
	stream->writeSint32(maxUnitWorkingPrevious, "maxUnitWorkingPrevious");
	stream->writeSint32(maxUnitWorkingFuture, "maxUnitWorkingFuture");

	stream->writeEnterSection("unitsFailingRequirements");
	for(int i=0; i<UnitCantWorkReasonSize; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(unitsFailingRequirements[i], "unitsFailingRequirements");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();


	stream->writeUint32(unitsInside.size(), "nbInside");
	fprintf(logFile, " nbInside=%zd\n", unitsInside.size());
	i = 0;
	for (std::list<Unit *>::iterator  it=unitsInside.begin(); it!=unitsInside.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		std::ostringstream oss;
		oss << "unitsInside[" << i++ << "]";
		stream->writeUint16((*it)->gid, oss.str().c_str());
	}
	
	stream->writeUint32(unitsHarvesting.size(), "nbHarvesting");
	fprintf(logFile, " nbHarvesting=%zd\n", unitsHarvesting.size());
	i = 0;
	for (std::list<Unit *>::iterator  it=unitsHarvesting.begin(); it!=unitsHarvesting.end(); ++it)
	{
		assert(*it);
		std::ostringstream oss;
		oss << "unitsHarvesting[" << i++ << "]";
		stream->writeUint16((*it)->gid, oss.str().c_str());
	}

	stream->writeLeaveSection();
}

bool Building::isResourceFull(void)
{
	for (int i=0; i<MAX_NB_RESOURCES; i++)
	{
		if (resources[i]+type->multiplierResource[i]<=type->maxResource[i])
			return false;
	}
	return true;
}

int Building::neededResource(void)
{
	Sint32 minProportion=0x7FFFFFFF;
	int minType=-1;
	int deci=syncRand()%MAX_RESOURCES;
	for (int ib=0; ib<MAX_RESOURCES; ib++)
	{
		int i=(ib+deci)%MAX_RESOURCES;
		int maxR=type->maxResource[i];
		if (maxR)
		{
			Sint32 proportion=(resources[i]<<16)/maxR;
			if (proportion<minProportion)
			{
				minProportion=proportion;
				minType=i;
			}
		}
	}
	return minType;
}

void Building::neededResources(int needs[MAX_NB_RESOURCES])
{
	for (int ri=0; ri<MAX_NB_RESOURCES; ri++)
		needs[ri]=Building::neededResource(ri);
}

void Building::computedNeededResources(int needs[MAX_NB_RESOURCES])
{
	 // we balance the system with Units working on it:
	for (int ri = 0; ri < MAX_NB_RESOURCES; ri++)
		needs[ri] = (4 * (type->maxResource[ri] - resources[ri])) / (type->multiplierResource[ri] * 3);
	for (std::list<Unit *>::iterator ui = unitsWorking.begin(); ui != unitsWorking.end(); ++ui)
		if ((*ui)->destinationPurpose >= 0)
		{
			assert((*ui)->destinationPurpose < MAX_NB_RESOURCES);
			needs[(*ui)->destinationPurpose]--;
		}
}

void Building::computeWishedResources()
{
	 // we balance the system with Units working on it:
	for (int ri = 0; ri < MAX_NB_RESOURCES; ri++)
		wishedResources[ri] = (4 * (type->maxResource[ri] - resources[ri])) / (type->multiplierResource[ri] * 3);
	for (std::list<Unit *>::iterator ui = unitsWorking.begin(); ui != unitsWorking.end(); ++ui)
		if ((*ui)->destinationPurpose >= 0)
		{
			assert((*ui)->destinationPurpose < MAX_NB_RESOURCES);
			wishedResources[(*ui)->destinationPurpose]--;
		}
}

int Building::neededResource(int r)
{
	assert(r >= 0);
	int need = type->maxResource[r] - resources[r] + 1 - type->multiplierResource[r];
	return std::max(need,0);
}


int Building::totalWishedResource()
{
	int sum=0;
	for (int ri = 0; ri < MAX_NB_RESOURCES; ri++)
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
		// Notice that the algorithm is not fast but clean.
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

	//Update the pointer resources to the newly changed type
	updateResourcesPointer();

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
	bool resourceFull=isResourceFull();
	if (resourceFull && !(type->canExchange && owner->openMarket()))
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
			if (resources[CORN]>(int)unitsInside.size())
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
		for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); it++)
			(*it)->standardRandomActivity();
		unitsWorking.clear();

		if (type->maxUnitWorking)
		{
			maxUnitWorking = maxUnitWorkingFuture;
			maxUnitWorkingFuture = 0;
		}
		else
			maxUnitWorking=0;
		maxUnitWorkingLocal=maxUnitWorking;

		// The working units still works for us, but
		// we don't have any unit in buildings
		assert(unitsInside.size()==0);
		maxUnitInside=type->maxUnitInside;

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

		setMapDiscovered();
		boost::shared_ptr<GameEvent> event(new BuildingCompletedEvent(owner->game->stepCounter, getMidX(), getMidY(), shortTypeNum));
		owner->pushGameEvent(event);

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
		// This is only a special optimisation case:
		for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
			(*it)->standardRandomActivity();
		unitsWorking.clear();
	}
	else
	{
		while (unitsWorking.size()>(unsigned)desiredMaxUnitWorking)
		{
			int maxDistSquare=0;

			Unit *fu=NULL;
			std::list<Unit *>::iterator itTemp;

			// First choice: free an unit who has a not needed resource..
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
			// Second choice: free an unit who has no resource..
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
							itTemp=it;
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
						itTemp=it;
					}
				}

			if (fu!=NULL)
			{
				if (verbose)
					printf("bgid=%d, we free the unit gid=%d\n", gid, fu->gid);
				// We free the unit.
				fu->standardRandomActivity();
				unitsWorking.erase(itTemp);
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
	computeWishedResources();
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
	Sint32 fDestructionRatio=(hp<<16)/type->hpMax;
	Sint32 fTotErr=0;
	for (int i=0; i<BASIC_COUNT; i++)
	{
		int fVal=fDestructionRatio*repairBt->maxResource[i];
		int iVal=(fVal>>16);
		fTotErr+=fVal&65535;
		if (fTotErr>=65536)
		{
			fTotErr-=65536;
			iVal++;
		}
		resources[i]=repairBt->maxResource[i]-iVal;
	}
}

bool Building::tryToBuildingSiteRoom(void)
{
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;

	int targetLevelTypeNum=-1;
	if (constructionResultState==UPGRADE)
		targetLevelTypeNum=type->nextLevel;
	else if (constructionResultState==REPAIR)
		targetLevelTypeNum=type->prevLevel;
	else
		assert(false);

	if (targetLevelTypeNum==-1)
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
			Sint32 fDestructionRatio=(hp<<16)/type->hpMax;
			Sint32 fTotErr=0;
			for (int i=0; i<MAX_RESOURCES; i++)
			{
				int fVal=fDestructionRatio*targetBt->maxResource[i];
				int iVal=(fVal>>16);
				fTotErr+=fVal&65535;
				if (fTotErr>=65536)
				{
					fTotErr-=65536;
					iVal++;
				}
				resources[i]=iVal;
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
				}
			}

		// units
		if (verbose)
			printf("bgid=%d, uses maxUnitWorkingPreferred=%d\n", gid, maxUnitWorkingPreferred);
		maxUnitWorking=maxUnitWorkingPreferred;
		maxUnitWorkingLocal=maxUnitWorking;
		maxUnitInside=type->maxUnitInside;
		updateCallLists();
		updateUnitsWorking();
		// no unit harvesting at that point

		// position
		posX=newPosX;
		posY=newPosY;
		posXLocal=posX;
		posYLocal=posY;

		// flag useful :
		unitStayRange=type->defaultUnitStayRange;
		unitStayRangeLocal=unitStayRange;

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

#include "GameGUI.h"

void Building::addForbiddenZoneToUpgradeArea(void)
{
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;

	int targetLevelTypeNum=-1;
	targetLevelTypeNum=type->nextLevel;

	BuildingType *targetBt=globalContainer->buildingsTypes.get(targetLevelTypeNum);
	int newPosX=midPosX+targetBt->decLeft;
	int newPosY=midPosY+targetBt->decTop;
	int newWidth=targetBt->width;
	int newHeight=targetBt->height;

	for(int x=newPosX; x<(newPosX+newWidth); ++x)
	{
		for(int y=newPosY; y<(newPosY+newHeight); ++y)
		{
			owner->map->addForbidden(x, y, owner->teamNumber);
		}
	}
	if(owner == owner->game->gui->getLocalTeam())
		owner->map->computeLocalForbidden(owner->teamNumber);
	owner->map->updateForbiddenGradient(owner->teamNumber);
}



void Building::removeForbiddenZoneFromUpgradeArea(void)
{
	int midPosX=posX-type->decLeft;
	int midPosY=posY-type->decTop;

	int targetLevelTypeNum=-1;
	targetLevelTypeNum=type->nextLevel;

	BuildingType *targetBt=globalContainer->buildingsTypes.get(targetLevelTypeNum);
	int newPosX=midPosX+targetBt->decLeft;
	int newPosY=midPosY+targetBt->decTop;
	int newWidth=targetBt->width;
	int newHeight=targetBt->height;

	for(int x=newPosX; x<(newPosX+newWidth); ++x)
	{
		for(int y=newPosY; y<(newPosY+newHeight); ++y)
		{
			owner->map->removeForbidden(x, y, owner->teamNumber);
		}
	}
	if(owner == owner->game->gui->getLocalTeam())
		owner->map->computeLocalForbidden(owner->teamNumber);
	owner->map->updateForbiddenGradient(owner->teamNumber);
}



bool Building::isHardSpaceForBuildingSite(void)
{
	return isHardSpaceForBuildingSite(constructionResultState);
}

bool Building::isHardSpaceForBuildingSite(ConstructionResultState constructionResultState)
{
	int tlTn=-1;
	if (constructionResultState==UPGRADE)
		tlTn=type->nextLevel;
	else if (constructionResultState==REPAIR)
		tlTn=type->prevLevel;
	else
		assert(false);

	if (tlTn==-1)
		return true;
	BuildingType *bt=globalContainer->buildingsTypes.get(tlTn);
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
	//If its virtual, than this building is a flag and always gets
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
	int max_considering_resources = (4 * neededResourcesSum) / 3;
	return std::min(user_num, max_considering_resources);
}


void Building::step(void)
{
	computeWishedResources();

	updateCallLists();
	if(underAttackTimer>0)
		underAttackTimer--;
	if(canNotConvertUnitTimer>0)
		canNotConvertUnitTimer--;
	// NOTE : Unit needs to update itself when it is in a building
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
	if (((Sint32)unitsWorking.size()<desiredMaxUnitWorking) /* && !unitsWorkingSubscribe.empty() */ )
	{
		Unit *choosen=NULL;
		Map *map=owner->map;
		for(int i=0; i<UnitCantWorkReasonSize; ++i)
		{
			unitsFailingRequirements[i]=0;
		}
		/* To choose a good unit, we get a composition of things:
		1-the closest the unit is, the better it is.
		2-the less the unit is hungry, the better it is.
		3-if the unit has a needed resource, this is better.
		4-if the unit as a not needed resource, this is worse.
		5-if the unit is close of a needed resource, this is better

		score_to_max=(rightRes*100/d+noRes*80/(d+dr)+wrongRes*25/(d+dr))/walk+sign(timeLeft>>2 - (d+dr))*500+100/harvest
		*/
		/*
		int maxValue=-INT_MAX;
		for(int n=0; n<Building::MAX_COUNT; ++n)
		{
			Unit* unit=owner->myUnits[n];
			if(unit==NULL
			|| unit->activity != Unit::ACT_RANDOM
			|| unit->medical != Unit::MED_FREE
			|| !unit->performance[HARVEST])
				continue;
			if(!canUnitWorkHere(unit))
				continue;

			int r=unit->carriedRessource;
			int dist;
			if(!map->buildingAvailable(this, unit->performance[SWIM], unit->posX, unit->posY, &dist))
			{
				//std::cout << ":" << std::flush;
				continue; //also to fill dist
			}
			int distUnitRessource;
			int nr;
			for (nr=0; nr<MAX_RESOURCES; nr++)
			{
				if (neededRessource(nr)>0)
				{
					if(map->resourceAvailable(owner->teamNumber, nr, unit->performance[SWIM], unit->posX, unit->posY, &distUnitRessource)) //to fill distUnitRessource
						break;
					else
						continue;
				}
			}
			if (neededRessource(nr)<=0)
			{
				//std::cout << "," << std::flush;
				continue;
			}
			int rightRes=(((r>=0) && neededRessource(r))?1:0);
			if(rightRes==1 && (unit->hungry-unit->trigHungry)/unit->race->hungriness/2<dist)
				continue;
			else if(rightRes!=1 && (unit->hungry-unit->trigHungry)/unit->race->hungriness/2<(dist+distUnitRessource))
				continue;
			int noRes=(r<0?1:0);
			int wrongRes=(((r>=0) && !neededRessource(r))?1:0);
			int value = (
				rightRes*10*(512-dist)+
				noRes*8*(512-dist-distUnitRessource)+
				wrongRes*2*(512-dist-distUnitRessource)
			)*(unit->level[WALK]+1)+
			//enoughTimeLeft*5000+
			50*(unit->level[HARVEST]+1)+
			(unit->level[SWIM]>0?-200:0);//swimmer's penalty to keep them free for swimmer tasks
			//std::cout << "d" << dist << " dr" << distUnitRessource << " rr" << rightRes << " nr" << noRes << " wr" << wrongRes << " wa" << unit->level[WALK] << " ha" << unit->level[HARVEST] << " va" << value << std::endl << std::flush;
			unit->destinationPurpose=(rightRes>0?r:nr);
			fprintf(logFile, "[%d] bdp1 destinationPurpose=%d\n", unit->gid, unit->destinationPurpose);
			if (value>maxValue)
			{
				maxValue=value;
				choosen=unit;
			}
		}
*/
		// Compute the list of candidate units
		Unit* possibleUnits[Unit::MAX_COUNT];
		int distances[Unit::MAX_COUNT];
		int resource[Unit::MAX_COUNT];
		int teamNumber=owner->teamNumber;
		for(int n=0; n<Unit::MAX_COUNT; ++n)
		{
			possibleUnits[n]=NULL;
			distances[n] = 0;
			resource[n] = -1;
			Unit* unit=owner->myUnits[n];
			if(unit)
			{
				if(!unit->performance[HARVEST])
				{
					continue;
				}
				else if(unit->attachedBuilding == this && unit->activity == Unit::ACT_FILLING)
				{
					continue;
				}
				else if(unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
				{
					unitsFailingRequirements[UnitNotAvailable] += 1;
				}
				else if(!canUnitWorkHere(unit))
				{
					unitsFailingRequirements[UnitTooLowLevel] += 1;
				}
				else
				{
					int distBuilding=0;
					int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungriness;
					bool canSwim=unit->performance[SWIM];
					if(!map->buildingAvailable(this, canSwim, unit->posX, unit->posY, &distBuilding))
					{
						unitsFailingRequirements[UnitCantAccessBuilding] += 1;
					}
					else if(distBuilding >= timeLeft)
					{
						unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
					}
					else
					{
						int unitR = unit->carriedResource;
						if((unitR>=0) && neededResource(unitR))
						{
							possibleUnits[n] = unit;
							distances[n] = distBuilding;
							resource[n] = unitR;
						}
						else
						{
							int bestDist = 100000;
							int bestResource = -1;
							bool regularFound=false;
							bool fruitFound=false;
							bool regularFoundTooFar=false;
							bool fruitFoundTooFar=false;
							int x=unit->posX;
							int y=unit->posY;
							for(int r=0; r<MAX_NB_RESOURCES; ++r)
							{
								int need = neededResource(r);
								if(need>0)
								{
									if(r<BASIC_COUNT)
										regularFound=true;
									else
										fruitFound=true;
									int distResource = 0;
									if (map->resourceAvailable(teamNumber, r, canSwim, x, y, &distResource))
									{
										if(distResource<timeLeft)
										{
											int dist = (distBuilding + distResource)<<8;
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
							if(bestResource == -1)
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
							}
							else
							{
								resource[n] = bestResource;
								distances[n] = bestDist;
								possibleUnits[n]=unit;
							}
						}
					}
				}
			}
		}

		int maxLevel = -1;
		int minValue = INT_MAX;
		//First: we look only for units with a needed resource:
		for(int n=0; n<Unit::MAX_COUNT; ++n)
		{
			Unit* unit=possibleUnits[n];
			if(unit==NULL)
				continue;

			int r=unit->carriedResource;
			int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungriness;
			if ((r>=0) && neededResource(r))
			{
				int dist = distances[n];
				int value=dist-(timeLeft>>1);
				int level = unit->level[HARVEST]*10 + unit->level[WALK];
				unit->destinationPurpose=r;
				if ((level>maxLevel) || (level==maxLevel && value<minValue))
				{
					minValue=value;
					maxLevel=level;
					choosen=unit;
				}
			}
		}

		//Second: we look for an unit who is not carrying a resource:
		if (choosen==NULL)
		{
			for(int n=0; n<Unit::MAX_COUNT; ++n)
			{
				Unit* unit=possibleUnits[n];
				if(unit==NULL)
					continue;

				if (unit->carriedResource<0)
				{
					int r = resource[n];
					int value=distances[n];
					int level = unit->level[HARVEST]*10 + unit->level[WALK];
					if ((level>maxLevel) || (level==maxLevel && value<minValue))
					{
						minValue=value;
						maxLevel=level;
						choosen=unit;
						unit->destinationPurpose=r;
					}
				}
			}
		}

		//Third: we look for an unit who is carrying an unwanted resource:
		if (choosen==NULL)
		{
			for(int n=0; n<Unit::MAX_COUNT; ++n)
			{
				Unit* unit=possibleUnits[n];
				if(unit==NULL)
					continue;

				int r2=unit->carriedResource;
				if ((r2>=0) && !neededResource(r2))
				{
					int r = resource[n];
					int value=distances[n];
					int level = unit->level[HARVEST]*10 + unit->level[WALK];
					if ((level>maxLevel) || (level==maxLevel && value<minValue))
					{
						minValue=value;
						maxLevel=level;
						choosen=unit;
						unit->destinationPurpose=r;
					}
				}
			}
		}
		if (choosen)
		{
			unitsWorking.push_back(choosen);
			choosen->subscriptionSuccess(this, false);
			hired=true;
		}
	}

	updateCallLists();

	if (verbose)
		printf(" ...done\n");
	return hired;
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
		for(int i=0; i<UnitCantWorkReasonSize; ++i)
		{
			unitsFailingRequirements[i]=0;
		}
		while (((Sint32)unitsWorking.size()<desiredMaxUnitWorking))
		{
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
				if(unit)
				{
					if(unit->attachedBuilding == this)
					{
						continue;
					}
					else if(type->zonable[EXPLORER] && unit->typeNum != EXPLORER)
					{
						continue;
					}
					else if(type->zonable[WORKER] && unit->typeNum != WORKER)
					{
						continue;
					}
					else if(type->zonable[WARRIOR] && unit->typeNum != WARRIOR)
					{
						continue;
					}
					else if(unit->activity != Unit::ACT_RANDOM || unit->medical != Unit::MED_FREE)
					{
						unitsFailingRequirements[UnitNotAvailable] += 1;
					}
					else if(!canUnitWorkHere(unit))
					{
						unitsFailingRequirements[UnitTooLowLevel] += 1;
					}
					else if(type->zonable[WARRIOR] && unit->movement == Unit::MOV_ATTACKING_TARGET)
					{
						unitsFailingRequirements[UnitNotAvailable] += 1;
					}
					else
					{
						int distBuilding=0;
						int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungriness;
						timeLeft*=timeLeft;
						int directDist=owner->map->warpDistSquare(unit->posX, unit->posY, posX, posY);
						bool canSwim=unit->performance[SWIM];
						if(type->zonable[EXPLORER] && timeLeft < directDist)
						{
							unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
						}
						else if(!type->zonable[EXPLORER] && !owner->map->buildingAvailable(this, canSwim, unit->posX, unit->posY, &distBuilding))
						{
							unitsFailingRequirements[UnitCantAccessBuilding] += 1;
						}
						else if(!type->zonable[EXPLORER] && distBuilding >= timeLeft)
						{
							unitsFailingRequirements[UnitTooFarFromBuilding] += 1;
						}
						else if(type->zonable[WORKER] && anyResourceToClear[canSwim]==2)
						{
							unitsFailingRequirements[UnitCantAccessResource] += 1;
						}
						else
						{
							if(type->zonable[EXPLORER])
								distances[n]=directDist;
							else
								distances[n]=distBuilding;
							possibleUnits[n]=unit;
						}
					}
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


void Building::swarmStep(void)
{
	// increase HP
	if (hp<type->hpMax)
		hp++;
	assert(NB_UNIT_TYPE==3);
	if ((resources[CORN]>=type->resourceForOneUnit)&&(ratio[0]|ratio[1]|ratio[2]))
		productionTimeout--;

	if (productionTimeout<0)
	{
		// We find the kind of unit we have to create:
		Sint32 fProportion;
		Sint32 fMinProportion=0x7FFFFFFF;
		int minType=-1;
		for (int i=0; i<NB_UNIT_TYPE; i++)
			if (ratio[i]!=0)
			{
				fProportion=(percentUsed[i]<<16)/ratio[i];
				if (fProportion<=fMinProportion)
				{
					fMinProportion=fProportion;
					minType=i;
				}
			}

		if (minType==-1)
			minType=0;
		assert(minType>=0);
		assert(minType<NB_UNIT_TYPE);
		if (minType<0 || minType>=NB_UNIT_TYPE)
			minType=0;

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
				resources[CORN]-=type->resourceForOneUnit;
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
			else if (verbose)
				printf("WARNING, no more UNIT ID free for team %d\n", owner->teamNumber);
		}
	}
}


void Building::turretStep(Uint32 stepCounter)
{
	// create bullet from stones in stock
	if (resources[STONE]>0 && (bullets<=(type->maxBullets-type->multiplierStoneToBullets)))
	{
		resources[STONE]--;
		bullets += type->multiplierStoneToBullets;

		// we need to be stone-fed
		updateCallLists();
	}

	// compute cooldown
	if (shootingCooldown > 0)
	{
		shootingCooldown -= type->shootRhythm;
		return;
	}

	// if we have no bullet, don't try to shoot
	if (bullets <= 0)
		return;

	//for some reason, any turret that is not 2x2 makes no sense at all to the game
	assert(type->width ==2);
	assert(type->height==2);

	int range = type->shootingRange;
	shootingStep = (shootingStep+1)&0x7;

	Uint32 enemies = owner->enemies;
	Map *map = owner->map;
	assert(map);

	// the type of target we have found
	enum class TargetType
	{
		NONE,
		BUILDING,
		WORKER,
		WARRIOR,
		EXPLORER,
	};
	// The type of the best target we have found up to now
	TargetType targetFound = TargetType::NONE;
	// The score of the best target we have found up to now
	int bestScore = INT_MIN;
	// The number of ticks before the unit may move away
	int bestTicks = 0;
	// The position of the best target we have found up to now
	int bestTargetX = 0, bestTargetY=0;

	for (int i=0; i<=range ; i++)
	{
		// The number of ticks before the bullet hits the target at range "i".
		int ticksToHit = ((i << 5) + ((type->width) << 4)) / (type->shootSpeed>>8);
		for (int j=0; j<=i ; j++)
		{
			for (int k=0; k<8; k++)
			{
				int targetX, targetY;
				switch (k)
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
					targetX=0;
					targetY=0;
					break;
				}
				int targetGUID = map->getGroundUnit(targetX, targetY);
				int airTargetGUID = map->getAirUnit(targetX, targetY);
				if (targetGUID != NOGUID)
				{
					Sint32 otherTeam = Unit::GIDtoTeam(targetGUID);
					Sint32 targetID = Unit::GIDtoID(targetGUID);
					Uint32 otherTeamMask = 1<<otherTeam;
					if (enemies & otherTeamMask)
					{
						Unit *testUnit = owner->game->teams[otherTeam]->myUnits[targetID];
						if ((owner->sharedVisionExchange & otherTeamMask) == 0)
						{
							int targetTicks = (256 - testUnit->delta) / testUnit->speed;
							// skip this unit if it will move away too soon.
							if (targetTicks <= ticksToHit)
								continue;
							// shoot warrior first, then workers if no warrior
							if (testUnit->typeNum == WARRIOR)
							{
								int targetOffense = (testUnit->getRealAttackStrength() * testUnit->performance[ATTACK_SPEED]); // 88 to 1024
								int targetWeakness = 0; // 0 to 512
								if (testUnit->hp > 0)
								{
									if (testUnit->hp < type->shootDamage) // hahaha, how mean!
										targetWeakness = 512;
									else
										targetWeakness = 256 / testUnit->hp;
								}
								int targetProximity = 0; // 0 to 512
								if (i <= 0)
									targetProximity = 512;
								else
									targetProximity = (256 / i);
								int targetScore = targetOffense + targetWeakness + targetProximity;
								// lower scores are overriden
								if (targetScore > bestScore)
								{
									bestScore = targetScore;
									bestTicks = targetTicks;
									bestTargetX = targetX;
									bestTargetY = targetY;
									targetFound = TargetType::WARRIOR;
								}
							}
							else if ((targetFound != TargetType::WARRIOR) && (testUnit->typeNum == WORKER))
							{
								// adjust score for range
								int targetScore = - testUnit->hp;
								// lower scores are overriden
								if (targetScore > bestScore)
								{
									bestScore = targetScore;
									bestTicks = targetTicks;
									bestTargetX = targetX;
									bestTargetY = targetY;
									targetFound = TargetType::WORKER;
								}
							}
						}
					}
				}
				//explorers are now priority targets as defined later

				if (airTargetGUID != NOGUID)
				{
					Sint32 otherTeam = Unit::GIDtoTeam(airTargetGUID);
					Sint32 targetID = Unit::GIDtoID(airTargetGUID);
					Uint32 otherTeamMask = 1<<otherTeam;
					if (enemies & otherTeamMask)
					{
						Unit *testUnit = owner->game->teams[otherTeam]->myUnits[targetID];
						if ((owner->sharedVisionExchange & otherTeamMask) == 0)
						{
							int targetTicks = (256 - testUnit->delta) / testUnit->speed;
							// skip this unit if it will move away too soon.
							if (targetTicks <= ticksToHit)
								continue;
							//Using simple calculation for now (should always shoot ground-attackers first, probably)
							// adjust score for range
							int targetScore = - testUnit->hp;
							// lower scores are overriden
							if (targetScore > bestScore)
							{
								bestScore = targetScore;
								bestTicks = targetTicks;
								bestTargetX = targetX;
								bestTargetY = targetY;
								targetFound = TargetType::EXPLORER;
							}
						}
					}
				}

				// shoot building only if no unit is found
				if (targetFound == TargetType::NONE)
				{
					Uint16 targetGBID = map->getBuilding(targetX, targetY);
					if (targetGBID != NOGBID)
					{
						Sint32 otherTeam = Building::GIDtoTeam(targetGBID);
						//int otherID = Building::GIDtoID(targetGBID);
						Uint32 otherTeamMask = 1<<otherTeam;
						if (enemies & otherTeamMask)
						{
							// adjust score for range
							int targetScore = - i;
							if (targetScore > bestScore)
							{
								bestScore = targetScore;
								bestTicks = 256;
								bestTargetX = targetX;
								bestTargetY = targetY;
								targetFound = TargetType::BUILDING;
							}
						}
					}
				}
			}
		}
		if (targetFound == TargetType::EXPLORER)
			break;//specifying explorers as high priority
	}

	if (targetFound != TargetType::NONE)
	{
		shootingStep = 0;

		//printf("%d found target found: (%d, %d) \n", gid, targetX, targetY);
		Sector *s=owner->map->getSector(getMidX(), getMidY());

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
		if (abs(dpx)>abs(dpy)) //we avoid a square root, since all distances are squares length.
		{
			mdp=abs(dpx);
			speedX=((dpx*type->shootSpeed)/(mdp<<8));
			speedY=((dpy*type->shootSpeed)/(mdp<<8));
			assert(speedX!=0);
			ticksLeft=abs(mdp/speedX);
		}
		else
		{
			mdp=abs(dpy);
			speedX=((dpx*type->shootSpeed)/(mdp<<8));
			speedY=((dpy*type->shootSpeed)/(mdp<<8));
			assert(speedY!=0);
			ticksLeft=abs(mdp/speedY);
		}

		if (ticksLeft < bestTicks)
		{
			Bullet *b = new Bullet(px, py, speedX, speedY, ticksLeft, type->shootDamage, bestTargetX, bestTargetY, posX-1, posY-1, type->width+2, type->height+2);
			s->bullets.push_front(b);
			bullets--;
			shootingCooldown = SHOOTING_COOLDOWN_MAX;
			lastShootStep = stepCounter;
			lastShootSpeedX = speedX;
			lastShootSpeedY = speedY;
		}
	}

}



void Building::clearingFlagStep()
{
	if (unitsWorking.size()<(unsigned)maxUnitWorking)
		for (int canSwim=0; canSwim<2; canSwim++)
			if (localResourcesCleanTime[canSwim]++>125) // Update every 5[s]
			{
				if (!owner->map->updateLocalResources(this, canSwim))
				{
					for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
						(*it)->standardRandomActivity();
					unitsWorking.clear();
				}
			}
}



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
				if (resources[r]>0)
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



void Building::updateResourcesPointer()
{
	if(!type->useTeamResources)
	{
		resources=localResource;
	}
	else
	{
		resources=owner->teamResources;
	}
}



void Building::addResourceIntoBuilding(int resourceType)
{
	resources[resourceType]+=type->multiplierResource[resourceType];
	//You can not exceed the maximum amount
	resources[resourceType] = std::min(resources[resourceType], type->maxResource[resourceType]);
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
			int totResources=0;
			for (unsigned i=0; i<MAX_NB_RESOURCES; i++)
				totResources+=type->maxResource[i];
			hp += type->hpMax/totResources;
			hp = std::min(hp, type->hpMax);
		}
		break;

		default:
			assert(false);
	}
	update();
}



void Building::removeResourceFromBuilding(int resourceType)
{
	resources[resourceType]-=type->multiplierResource[resourceType];
	resources[resourceType]= std::max(resources[resourceType], 0);
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
		if (owner->map->isResource(testX, testY-1))
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
	resources[CORN]--;
	assert(resources[CORN]>=0);
	Uint32 fruitMask=0;
	Uint32 fruitCount=0;
	for (int i=0; i<HAPPYNESS_COUNT; i++)
	{
		int resId=i+HAPPYNESS_BASE;
		if (resources[resId])
		{
			resources[resId]--;
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
	if (resources[CORN] <= inside)
		return 0;
	int happyness = 1;
	for (int i = 0; i < HAPPYNESS_COUNT; i++)
		if (resources[i + HAPPYNESS_BASE]  >inside)
			happyness++;
	return happyness;
}

bool Building::canConvertUnit(void)
{
	assert(type->canFeedUnit);
	return
			canNotConvertUnitTimer<=0 &&
			((int)unitsInside.size()<resources[CORN]) && 
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

	for (int i=0; i<MAX_RESOURCES; i++)
		cs^=localResource[i];
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
