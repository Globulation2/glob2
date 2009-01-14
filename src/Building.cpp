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

Building::Building(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor)
{
	for (int i=0; i<2; i++)
	{
		globalGradient[i]=NULL;
		localRessources[i]=NULL;
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

	// flag usefull :
	unitStayRange=type->defaultUnitStayRange;
	unitStayRangeLocal=unitStayRange;
	for(int i=0; i<BASIC_COUNT; i++)
		clearingRessources[i]=true;
	clearingRessources[STONE]=false;
	memcpy(clearingRessourcesLocal, clearingRessources, sizeof(bool)*BASIC_COUNT);
	minLevelToFlag=0;
	minLevelToFlagLocal=minLevelToFlag;
	
	// building specific :
	for(int i=0; i<MAX_NB_RESSOURCES; i++)
		localRessource[i]=0;
	updateRessourcesPointer();

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
	bullets=0;

	seenByMask=0;
	
	canFeedUnit=0;
	canHealUnit=0;
	callListState=0;

	for (int i=0; i<NB_ABILITY; i++)
		upgrade[i]=0;
	
	for (int i=0; i<2; i++)
	{
		globalGradient[i]=NULL;
		localRessources[i]=NULL;
		dirtyLocalGradient[i]=true;
		locked[i]=false;
		lastGlobalGradientUpdateStepCounter[i]=0;
		
		localRessources[i]=0;
		localRessourcesCleanTime[i]=0;
		anyRessourceToClear[i]=0;
	}
	
	verbose=false;
	
	lastShootStep = 0xFFFFFFFF;
	lastShootSpeedX = 0;
	lastShootSpeedY = 0;
	
	for(int i=0; i<UnitCantWorkReasonSize; ++i)
	{
		unitsFailingRequirements[i]=0;
	}
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
		if (localRessources[i])
		{
			delete[] localRessources[i];
			localRessources[i] = NULL;
		}
		dirtyLocalGradient[i] = true;
		locked[i] = false;
		lastGlobalGradientUpdateStepCounter[i] = 0;
		
		localRessourcesCleanTime[i] = 0;
		anyRessourceToClear[i] = 0;
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
		clearingRessources[i] = (bool)stream->readSint32(oss.str().c_str());
	}
	assert(clearingRessources[STONE] == false);

	memcpy(clearingRessourcesLocal, clearingRessources, sizeof(bool)*BASIC_COUNT);
	
	minLevelToFlag = stream->readSint32("minLevelToFlag");
	minLevelToFlagLocal = minLevelToFlag;
	
	// Building Specific
	for (int i=0; i<MAX_NB_RESSOURCES; i++)
	{
		std::ostringstream oss;
		oss << "localRessource[" << i << "]";
		localRessource[i] = stream->readSint32(oss.str().c_str());
	}

	// quality parameters
	hp = stream->readSint32("hp");

	// prefered parameters
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

	receiveRessourceMask = stream->readUint32("receiveRessourceMask");
	sendRessourceMask = stream->readUint32("sendRessourceMask");
	receiveRessourceMaskLocal = receiveRessourceMask;
	sendRessourceMaskLocal = sendRessourceMask;

	shootingStep = stream->readUint32("shootingStep");
	shootingCooldown = stream->readSint32("shootingCooldown");
	bullets = stream->readSint32("bullets");

	// type
	// FIXME : do not save typenum but name/isBuildingSite/level
	typeNum = stream->readSint32("typeNum");
	type = types->get(typeNum);
	assert(type);
	updateRessourcesPointer();
	
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
	
	canFeedUnit = 0;
	canHealUnit = 0;
	callListState = 0;
	
	for (int i=0; i<NB_ABILITY; i++)
		upgrade[i] = 0;
	
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
		stream->writeSint32(clearingRessources[i], oss.str().c_str());
	}
	stream->writeSint32(minLevelToFlag, "minLevelToFlag");
	
	// Building Specific
	for (int i=0; i<MAX_NB_RESSOURCES; i++)
	{
		std::ostringstream oss;
		oss << "localRessource[" << i << "]";
		stream->writeSint32(localRessource[i], oss.str().c_str());
	}

	// quality parameters
	stream->writeSint32(hp, "hp");

	// prefered parameters
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

	stream->writeUint32(receiveRessourceMask, "receiveRessourceMask");
	stream->writeUint32(sendRessourceMask, "sendRessourceMask");

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
	//steph and nuage suggested to store and update size in a variable
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
		needs[ri]=type->maxRessource[ri]-ressources[ri]+1-type->multiplierRessource[ri];
}

void Building::wishedRessources(int needs[MAX_NB_RESSOURCES])
{
	 // we balance the system with Units working on it:
	for (int ri = 0; ri < MAX_NB_RESSOURCES; ri++)
		needs[ri] = (4 * (type->maxRessource[ri] - ressources[ri])) / (type->multiplierRessource[ri] * 3);
	for (std::list<Unit *>::iterator ui = unitsWorking.begin(); ui != unitsWorking.end(); ++ui)
		if ((*ui)->destinationPurprose >= 0)
		{
			assert((*ui)->destinationPurprose < MAX_NB_RESSOURCES);
			needs[(*ui)->destinationPurprose]--;
		}
}

void Building::computeWishedRessources()
{
	 // we balance the system with Units working on it:
	for (int ri = 0; ri < MAX_NB_RESSOURCES; ri++)
		wishedResources[ri] = (4 * (type->maxRessource[ri] - ressources[ri])) / (type->multiplierRessource[ri] * 3);
	for (std::list<Unit *>::iterator ui = unitsWorking.begin(); ui != unitsWorking.end(); ++ui)
		if ((*ui)->destinationPurprose >= 0)
		{
			assert((*ui)->destinationPurprose < MAX_NB_RESSOURCES);
			wishedResources[(*ui)->destinationPurprose]--;
		}
}

int Building::neededRessource(int r)
{
	assert(r >= 0);
	int need = type->maxRessource[r] - ressources[r] + 1 - type->multiplierRessource[r];
	if (need > 0)
		return need;
	else
		return 0;
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
					//A Building newly getting available to feed is locked to conversion for 150 frames
					canNotConvertUnitTimer=150;
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

		// this is for Unit healing
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
	
	if (isRessourceFull() && (buildingState!=WAITING_FOR_DESTRUCTION))
	{
		// we really uses the resources of the buildingsite:
		for(int i=0; i<MAX_RESSOURCES; i++)
			ressources[i]-=type->maxRessource[i];

		owner->prestige-=type->prestige;
		typeNum=type->nextLevel;
		type=globalContainer->buildingsTypes.get(type->nextLevel);
		assert(constructionResultState!=NO_CONSTRUCTION);
		constructionResultState=NO_CONSTRUCTION;
		owner->prestige+=type->prestige;

		//Update the pointer ressources to the newly changed type
		updateRessourcesPointer();
	

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
			std::list<Unit *>::iterator ittemp;

			// First choice: free an unit who has a not needed ressource..
			for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end();)
			{
				int r=(*it)->caryedRessource;
				if (r>=0 && !neededRessource(r))
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
			// Second choice: free an unit who has no ressource..
			if (fu==NULL)
			{
				int minDistSquare=INT_MAX;
				for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
				{
					int r=(*it)->caryedRessource;
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
		std::list<Unit *>::iterator thisIt = it;
		Unit* u = *thisIt;
		++it;
		
		if ((buildingState != ALIVE) || (owner->sharedVisionExchange & u->owner->me == 0))
		{
			u->attachedBuilding->removeUnitFromWorking(u);
			u->standardRandomActivity();
			unitsHarvesting.erase(thisIt);
		}
	}
}

void Building::update(void)
{
	computeWishedRessources();
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

void Building::getRessourceCountToRepair(int ressources[BASIC_COUNT])
{
	assert(!type->isBuildingSite);
	int repairLevelTypeNum=type->prevLevel;
	BuildingType *repairBt=globalContainer->buildingsTypes.get(repairLevelTypeNum);
	assert(repairBt);
	Sint32 fDestructionRatio=(hp<<16)/type->hpMax;
	Sint32 fTotErr=0;
	for (int i=0; i<BASIC_COUNT; i++)
	{
		int fVal=fDestructionRatio*repairBt->maxRessource[i];
		int iVal=(fVal>>16);
		fTotErr+=fVal&65535;
		if (fTotErr>=65536)
		{
			fTotErr-=65536;
			iVal++;
		}
		ressources[i]=repairBt->maxRessource[i]-iVal;
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
			for (int i=0; i<MAX_RESSOURCES; i++)
			{
				int fVal=fDestructionRatio*targetBt->maxRessource[i];
				int iVal=(fVal>>16);
				fTotErr+=fVal&65535;
				if (fTotErr>=65536)
				{
					fTotErr-=65536;
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

		//Update the pointer ressources to the newly changed type
		updateRessourcesPointer();
	
		buildingState=ALIVE;
		owner->addToStaticAbilitiesLists(this);
		
		// towers may already have some stone!
		if (constructionResultState==UPGRADE)
			for (int i=0; i<MAX_NB_RESSOURCES; i++)
			{
				int res=ressources[i];
				int resMax=type->maxRessource[i];
				if (res>0 && resMax>0)
				{
					if (res>resMax)
						res=resMax;
					if (verbose)
						printf("using %d ressources[%d] for fast constr (hp+=%d)\n", res, i, res*type->hpInc);
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
	int tltn=-1;
	if (constructionResultState==UPGRADE)
		tltn=type->nextLevel;
	else if (constructionResultState==REPAIR)
		tltn=type->prevLevel;
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

bool Building::fullInside(void)
{
	if ((type->canFeedUnit) && (ressources[CORN]<=(int)unitsInside.size()))
		return true;
	else
		return ((signed)unitsInside.size()>=maxUnitInside);
}


int Building::desiredNumberOfWorkers(void)
{
	//If its virtual, than this building is a flag and always gets
	//full ressources
	if(type->isVirtual)
	{
		return maxUnitWorking;
	}
	//Otherwise, this building gets what the user desires, up to a limit of 2 units per 1 needed ressource,
	//thus if no ressources are needed, then no units will be working here.
	int neededRessourcesSum = 0;
	for (size_t ri = 0; ri < MAX_RESSOURCES; ri++)
	{
		int neededRessources = (type->maxRessource[ri] - ressources[ri]) / type->multiplierRessource[ri];
		if (neededRessources > 0)
			neededRessourcesSum += neededRessources;
	}
	int user_num = maxUnitWorking;
	int max_considering_ressources = (4 * neededRessourcesSum) / 3;
	return std::min(user_num, max_considering_ressources);
}


void Building::step(void)
{
	computeWishedRessources();
	
	updateCallLists();
	if(underAttackTimer>0)
		underAttackTimer--;
	if(canNotConvertUnitTimer>0)
		canNotConvertUnitTimer--;
	// NOTE : Unit needs to update itself when it is in a building
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
		3-if the unit has a needed ressource, this is better.
		4-if the unit as a not needed ressource, this is worse.
		5-if the unit is close of a needed ressource, this is better

		score_to_max=(rightRes*100/d+noRes*80/(d+dr)+wrongRes*25/(d+dr))/walk+sign(timeleft>>2 - (d+dr))*500+100/harvest
		*/
		/*
		int maxValue=-INT_MAX;
		for(int n=0; n<1024; ++n)
		{
			Unit* unit=owner->myUnits[n];
			if(unit==NULL
			|| unit->activity != Unit::ACT_RANDOM
			|| unit->medical != Unit::MED_FREE
			|| !unit->performance[HARVEST])
				continue;
			if(!canUnitWorkHere(unit))
				continue;

			int r=unit->caryedRessource;
			int dist;
			if(!map->buildingAvailable(this, unit->performance[SWIM], unit->posX, unit->posY, &dist))
			{
				//std::cout << ":" << std::flush;
				continue; //also to fill dist
			}
			int distUnitRessource;
			int nr;
			for (nr=0; nr<MAX_RESSOURCES; nr++)
			{
				if (neededRessource(nr)>0)
				{
					if(map->ressourceAvailable(owner->teamNumber, nr, unit->performance[SWIM], unit->posX, unit->posY, &distUnitRessource)) //to fill distUnitRessource
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
			if(rightRes==1 && (unit->hungry-unit->trigHungry)/unit->race->hungryness/2<dist)
				continue;
			else if(rightRes!=1 && (unit->hungry-unit->trigHungry)/unit->race->hungryness/2<(dist+distUnitRessource))
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
			unit->destinationPurprose=(rightRes>0?r:nr);
			fprintf(logFile, "[%d] bdp1 destinationPurprose=%d\n", unit->gid, unit->destinationPurprose);
			if (value>maxValue)
			{
				maxValue=value;
				choosen=unit;
			}
		}
*/
		// Compute the list of candidate units
		Unit* possibleUnits[1024];
		int distances[1024];
		int resource[1024];
		int teamNumber=owner->teamNumber;
		for(int n=0; n<1024; ++n)
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
					int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
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
						int unitr = unit->caryedRessource;
						if((unitr>=0) && neededRessource(unitr))
						{
							possibleUnits[n] = unit;
							distances[n] = distBuilding;
							resource[n] = unitr;
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
									if (map->ressourceAvailable(teamNumber, r, canSwim, x, y, &distResource))
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
		for(int n=0; n<1024; ++n)
		{
			Unit* unit=possibleUnits[n];
			if(unit==NULL)
				continue;

			int r=unit->caryedRessource;
			int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
			if ((r>=0) && neededRessource(r))
			{
				int dist = distances[n];
				int value=dist-(timeLeft>>1);
				int level = unit->level[HARVEST]*10 + unit->level[WALK];
				unit->destinationPurprose=r;
				if ((level>maxLevel) || (level==maxLevel && value<minValue))
				{
					minValue=value;
					maxLevel=level;
					choosen=unit;
				}
			}
		}
		
		//Second: we look for an unit who is not carying a ressource:
		if (choosen==NULL)
		{
			int teamNumber=owner->teamNumber;
			for(int n=0; n<1024; ++n)
			{
				Unit* unit=possibleUnits[n];
				if(unit==NULL)
					continue;

				if (unit->caryedRessource<0)
				{
					int r = resource[n];
					int value=distances[n];
					int level = unit->level[HARVEST]*10 + unit->level[WALK];
					if ((level>maxLevel) || (level==maxLevel && value<minValue))
					{
						minValue=value;
						maxLevel=level;
						choosen=unit;
						unit->destinationPurprose=r;
					}
				}
			}
		}

		//Third: we look for an unit who is carrying an unwanted resource:
		if (choosen==NULL)
		{
			int teamNumber=owner->teamNumber;
			for(int n=0; n<1024; ++n)
			{
				Unit* unit=possibleUnits[n];
				if(unit==NULL)
					continue;

				int r2=unit->caryedRessource;
				if ((r2>=0) && !neededRessource(r2))
				{
					int r = resource[n];
					int value=distances[n];
					int level = unit->level[HARVEST]*10 + unit->level[WALK];
					if ((level>maxLevel) || (level==maxLevel && value<minValue))
					{
						minValue=value;
						maxLevel=level;
						choosen=unit;
						unit->destinationPurprose=r;
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
			Unit* possibleUnits[1024];
			int distances[1024];
			int teamNumber=owner->teamNumber;
			for(int n=0; n<1024; ++n)
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
						int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
						int directdist=owner->map->warpDistSquare(unit->posX, unit->posY, posX, posY);
						bool canSwim=unit->performance[SWIM];
						if(type->zonable[EXPLORER] && timeLeft*timeLeft < directdist)
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
						else if(type->zonable[WORKER] && anyRessourceToClear[canSwim]==2)
						{
							unitsFailingRequirements[UnitCantAccessResource] += 1;
						}
						else
						{
							if(type->zonable[EXPLORER])
								distances[n]=directdist;
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
			Map *map=owner->map;
			
			/* To choose a good unit, we get a composition of things:
			1-the closer the unit is, the better it is.
			2-the less the unit is hungry, the better it is.
			3-the more hp the unit has, the better it is.
			*/
			if (type->zonable[EXPLORER])
			{
				for(int n=0; n<1024; ++n)
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
				for(int n=0; n<1024; ++n)
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
				for(int n=0; n<1024; ++n)
				{
					Unit* unit=possibleUnits[n];
					if(unit==NULL)
						continue;

					int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					int dist = distances[n];
					bool canSwim=unit->performance[SWIM];
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
	if ((ressources[CORN]>=type->ressourceForOneUnit)&&(ratio[0]|ratio[1]|ratio[2]))
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
			else if (verbose)
				printf("WARNING, no more UNIT ID free for team %d\n", owner->teamNumber);
		}
	}
}


void Building::turretStep(Uint32 stepCounter)
{
	// create bullet from stones in stock
	if (ressources[STONE]>0 && (bullets<=(type->maxBullets-type->multiplierStoneToBullets)))
	{
		ressources[STONE]--;
		bullets += type->multiplierStoneToBullets;
		
		// we need to be stone-feeded
		updateCallLists();
	}
	
	// compute cooldown
	if (shootingCooldown > 0)
	{
		shootingCooldown -= type->shootRythme;
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
	enum TargetType
	{
		TARGETTYPE_NONE,
		TARGETTYPE_BUILDING,
		TARGETTYPE_WORKER,
		TARGETTYPE_WARRIOR,
		TARGETTYPE_EXPLORER,
	};
	// The type of the best target we have found up to now
	TargetType targetFound = TARGETTYPE_NONE;
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
								int targetWeakeness = 0; // 0 to 512
								if (testUnit->hp > 0)
								{
									if (testUnit->hp < type->shootDamage) // hahaha, how mean!
										targetWeakeness = 512;
									else
										targetWeakeness = 256 / testUnit->hp;
								}
								int targetProximity = 0; // 0 to 512
								if (i <= 0)
									targetProximity = 512;
								else
									targetProximity = (256 / i);
								int targetScore = targetOffense + targetWeakeness + targetProximity;
								// lower scores are overriden
								if (targetScore > bestScore)
								{
									bestScore = targetScore;
									bestTicks = targetTicks;
									bestTargetX = targetX;
									bestTargetY = targetY;
									targetFound = TARGETTYPE_WARRIOR;
								}
							}
							else if ((targetFound != TARGETTYPE_WARRIOR) && (testUnit->typeNum == WORKER))
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
									targetFound = TARGETTYPE_WORKER;
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
								targetFound = TARGETTYPE_EXPLORER;
							}
						}
					}
				}

				// shoot building only if no unit is found
				if (targetFound == TARGETTYPE_NONE)
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
								targetFound = TARGETTYPE_BUILDING;
							}
						}
					}
				}
			}
		}
		if (targetFound == TARGETTYPE_EXPLORER)
			break;//specifying explorers as high priority
	}

	if (targetFound != TARGETTYPE_NONE)
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
			if (localRessourcesCleanTime[canSwim]++>125) // Update every 5[s]
			{
				if (!owner->map->updateLocalRessources(this, canSwim))
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
			(unitsInside.size()<ressources[CORN]) && 
			(unitsInside.size()<maxUnitInside);
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
	
	assert(unitsInside.size()>=0);
	assert(unitsInside.size()<=1024);
	for (std::list<Unit *>::iterator  it=unitsInside.begin(); it!=unitsInside.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		assert((*it)->attachedBuilding==this);
	}
	for (std::list<Unit *>::iterator  it=unitsHarvesting.begin(); it!=unitsHarvesting.end(); ++it)
	{
		assert(*it);
		assert((*it)->targetBuilding==this);
	}
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
