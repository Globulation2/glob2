/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
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

#include <list>
#include <math.h>
#include <Stream.h>
#include <stdlib.h>
#include <algorithm>

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

Building::Building(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, BuildingsTypes *types)
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
	subscriptionInsideTimer=0;
	subscriptionWorkingTimer=0;

	// position
	posX=x;
	posY=y;
	posXLocal=posX;
	posYLocal=posY;

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
	bullets=0;

	seenByMask=0;
	
	subscribeForInside=0;
	subscribeToBringRessources=0;
	subscribeForFlaging=0;
	canFeedUnit=0;
	canHealUnit=0;
	foodable=0;
	fillable=0;
	
	zonableWorkers[0]=0; 
	zonableWorkers[1]=0; 
	zonableExplorer=0; 
	zonableWarrior=0;
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

	// Flag specific
	unitStayRange = stream->readUint32("unitStayRange");
	unitStayRangeLocal = unitStayRange;
	if (versionMinor >= 27)
	{
		for (int i=0; i<BASIC_COUNT; i++)
		{
			std::ostringstream oss;
			oss << "clearingRessources[" << i << "]";
			clearingRessources[i] = (bool)stream->readSint32(oss.str().c_str());
		}
		assert(clearingRessources[STONE] == false);
	}
	else
	{
		for( int i=0; i<BASIC_COUNT; i++)
			clearingRessources[i] = true;
		clearingRessources[STONE] = false;
	}
	memcpy(clearingRessourcesLocal, clearingRessources, sizeof(bool)*BASIC_COUNT);
	
	if (versionMinor >= 33)
		minLevelToFlag = stream->readSint32("minLevelToFlag");
	else
		minLevelToFlag = 0;
	minLevelToFlagLocal = minLevelToFlag;
	
	// Building Specific
	for (int i=0; i<MAX_NB_RESSOURCES; i++)
	{
		std::ostringstream oss;
		oss << "ressources[" << i << "]";
		ressources[i] = stream->readSint32(oss.str().c_str());
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
	if (versionMinor >= 24)
		bullets = stream->readSint32("bullets");
	else
		bullets = 0;

	// type
	// FIXME : do not save typenum but name/isBuildingSite/level
	typeNum = stream->readSint32("typeNum");
	type = types->get(typeNum);
	assert(type);
	owner->prestige += type->prestige;
	
	seenByMask = stream->readUint32("seenByMaskk");
	
	subscribeForInside = 0;
	subscribeToBringRessources = 0;
	subscribeForFlaging = 0;
	canFeedUnit = 0;
	canHealUnit = 0;
	foodable = 0;
	fillable = 0;
	
	zonableWorkers[0] = 0; 
	zonableWorkers[1] = 0; 
	zonableExplorer = 0; 
	zonableWarrior = 0;
	for (int i=0; i<NB_ABILITY; i++)
		upgrade[i] = 0;
	
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
	
	verbose = false;
	stream->readLeaveSection();
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
		oss << "ressources[" << i << "]";
		stream->writeSint32(ressources[i], oss.str().c_str());
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

void Building::loadCrossRef(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner)
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

	unsigned nbWorkingSubscribe = stream->readUint32("nbWorkingSubscribe");
	fprintf(logFile, " nbWorkingSubscribe=%d\n", nbWorkingSubscribe);
	unitsWorkingSubscribe.clear();
	for (unsigned i=0; i<nbWorkingSubscribe; i++)
	{
		std::ostringstream oss;
		oss << "unitsWorkingSubscribe[" << i << "]";
		Unit *unit = owner->myUnits[Unit::GIDtoID(stream->readUint16(oss.str().c_str()))];
		assert(unit);
		unitsWorkingSubscribe.push_front(unit);
	}

	subscriptionWorkingTimer = stream->readSint32("subscriptionWorkingTimer");
	maxUnitWorking = stream->readSint32("maxUnitWorking");
	maxUnitWorkingPreferred = stream->readSint32("maxUnitWorkingPreferred");
	maxUnitWorkingLocal = maxUnitWorking;
	
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

	unsigned nbInsideSubscribe = stream->readUint32("nbInsideSubscribe");
	fprintf(logFile, " nbInsideSubscribe=%d\n", nbInsideSubscribe);
	unitsInsideSubscribe.clear();
	for (unsigned i=0; i<nbInsideSubscribe; i++)
	{
		std::ostringstream oss;
		oss << "unitsInsideSubscribe[" << i << "]";
		Unit *unit = owner->myUnits[Unit::GIDtoID(stream->readUint16(oss.str().c_str()))];
		assert(unit);
		unitsInsideSubscribe.push_front(unit);
	}
	subscriptionInsideTimer = stream->readSint32("subscriptionInsideTimer");
	
	stream->readLeaveSection();
}

void Building::saveCrossRef(GAGCore::OutputStream *stream)
{
	unsigned i;
	
	stream->writeEnterSection("Building");
	fprintf(logFile, "saveCrossRef (%d)\n", gid);
	
	// units
	stream->writeSint32(maxUnitInside, "maxUnitInside");
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

	stream->writeUint32(unitsWorkingSubscribe.size(), "nbWorkingSubscribe");
	fprintf(logFile, " nbWorkingSubscribe=%zd\n", unitsWorkingSubscribe.size());
	i = 0;
	for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		std::ostringstream oss;
		oss << "unitsWorkingSubscribe[" << i++ << "]";
		stream->writeUint16((*it)->gid, oss.str().c_str());
	}
	
	stream->writeSint32(subscriptionWorkingTimer, "subscriptionWorkingTimer");
	stream->writeSint32(maxUnitWorking, "maxUnitWorking");
	stream->writeSint32(maxUnitWorkingPreferred, "maxUnitWorkingPreferred");
	
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

	stream->writeUint32(unitsInsideSubscribe.size(), "nbInsideSubscribe");
	fprintf(logFile, " nbInsideSubscribe=%zd\n", unitsInsideSubscribe.size());
	i = 0;
	for (std::list<Unit *>::iterator  it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); ++it)
	{
		assert(*it);
		assert(owner->myUnits[Unit::GIDtoID((*it)->gid)]);
		std::ostringstream oss;
		oss << "unitsInsideSubscribe[" << i++ << "]";
		stream->writeUint16((*it)->gid, oss.str().c_str());
	}
	stream->writeSint32(subscriptionInsideTimer, "subscriptionInsideTimer");
	
	stream->writeLeaveSection();
}

bool Building::isRessourceFull(void)
{
	for (int i=0; i<BASIC_COUNT; i++)
		if (ressources[i]+type->multiplierRessource[i]<=type->maxRessource[i])
			return false;
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
		needs[ri] = (2 * (type->maxRessource[ri] - ressources[ri])) / (type->multiplierRessource[ri]);
	for (std::list<Unit *>::iterator ui = unitsWorking.begin(); ui != unitsWorking.end(); ++ui)
		if ((*ui)->destinationPurprose >= 0)
		{
			assert((*ui)->destinationPurprose < MAX_NB_RESSOURCES);
			needs[(*ui)->destinationPurprose]--;
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

void Building::launchConstruction(void)
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
	// we do not update owner->buildingsWaitingForDestruction because Team::syncStep will remove this building from the list
}

void Building::updateClearingFlag(bool canSwim)
{
	if (owner->map->updateLocalRessources(this, canSwim))
	{
		if (zonableWorkers[canSwim]!=1)
		{
			owner->zonableWorkers[canSwim].push_front(this);
			zonableWorkers[canSwim]=1;
		}
	}
	else
	{
		if (zonableWorkers[canSwim]!=2)
		{
			owner->zonableWorkers[canSwim].remove(this);
			zonableWorkers[canSwim]=2;
		}
		
		for (std::list<Unit *>::iterator it=unitsWorking.begin(); it!=unitsWorking.end(); ++it)
			(*it)->standardRandomActivity();
		unitsWorking.clear();
	}
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
		if (buildingState==ALIVE)
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
			
			if (type->zonable[WORKER])
				for (int canSwim=0; canSwim<2; canSwim++)
					if (anyRessourceToClear[canSwim]!=2 && zonableWorkers[canSwim]!=1)
					{
						owner->zonableWorkers[canSwim].push_front(this);
						zonableWorkers[canSwim]=1;
					}
			if (zonableExplorer!=1 && type->zonable[EXPLORER])
			{
				owner->zonableExplorer.push_front(this);
				zonableExplorer=1;
			}
			if (zonableWarrior!=1 && type->zonable[WARRIOR])
			{
				owner->zonableWarrior.push_front(this);
				zonableWarrior=1;
			}
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
		
		if (type->zonable[WORKER])
			for (int canSwim=0; canSwim<2; canSwim++)
				if (zonableWorkers[canSwim]!=2)
				{
					owner->zonableWorkers[canSwim].remove(this);
					zonableWorkers[canSwim]=2;
				}
		if (zonableExplorer!=2 && type->zonable[EXPLORER])
		{
			owner->zonableExplorer.remove(this);
			zonableExplorer=2;
		}
		if (zonableWarrior!=2 && type->zonable[WARRIOR])
		{
			owner->zonableWarrior.remove(this);
			zonableWarrior=2;
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
			if (buildingState!=WAITING_FOR_CONSTRUCTION_ROOM)
			{
				buildingState=WAITING_FOR_CONSTRUCTION_ROOM;
				owner->buildingsTryToBuildingSiteRoom.push_front(this);
				if (verbose)
					printf("bgid=%d, inserted in buildingsTryToBuildingSiteRoom\n", gid);
			}
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
		typeNum=type->nextLevel;
		type=globalContainer->buildingsTypes.get(type->nextLevel);
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
		owner->setEvent(getMidX(), getMidY(), Team::BUILDING_FINISHED_EVENT, gid, owner->teamNumber);

		// we need to do an update again
		updateCallLists();
	}
}

void Building::update(void)
{
	if (buildingState==DEAD)
		return;
	if (type->zonable[WORKER])
	{
		updateClearingFlag(0);
		updateClearingFlag(1);
	}
	updateCallLists();
	updateConstructionState();
	if (type->isBuildingSite)
		updateBuildingSite();
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
	return ((Sint32)unitsWorking.size() >= maxUnitWorking);
}

bool Building::enoughWorking(void)
{
	int neededRessourcesSum = 0;
	for (size_t ri = 0; ri < MAX_RESSOURCES; ri++)
	{
		int neededRessources = (type->maxRessource[ri] - ressources[ri]) / type->multiplierRessource[ri];
		if (neededRessources > 0)
			neededRessourcesSum += neededRessources;
	}
	return ((int)unitsWorking.size() >= 2 * neededRessourcesSum);
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
	if (subscriptionWorkingTimer>0)
		subscriptionWorkingTimer++;
	if (fullWorking() || enoughWorking())
	{
		for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
			(*it)->standardRandomActivity();
		unitsWorkingSubscribe.clear();
		if (verbose)
			printf("...enoughWorking()\n");
		return;
	}
	
	if (subscriptionWorkingTimer>32)
	{
		if (verbose)
			printf("bgid=%d, subscribeToBringRessourcesStep()...\n", gid);
		while (((Sint32)unitsWorking.size()<maxUnitWorking) && !unitsWorkingSubscribe.empty())
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
				int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
				if ((r>=0)&& neededRessource(r))
				{
					int dist;
					if (map->buildingAvailable(this, unit->performance[SWIM], unit->posX, unit->posY, &dist) && dist<timeLeft)
					{
						int value=dist-(timeLeft>>1);
						unit->destinationPurprose=r;
						fprintf(logFile, "[%d] bdp1 destinationPurprose=%d\n", unit->gid, unit->destinationPurprose);
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
				int needs[MAX_NB_RESSOURCES];
				wishedRessources(needs);
				int teamNumber=owner->teamNumber;
				for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); ++it)
				{
					Unit *unit=(*it);
					if (unit->caryedRessource<0)
					{
						int x=unit->posX;
						int y=unit->posY;
						bool canSwim=unit->performance[SWIM];
						int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
						int distUnitBuilding;
						if (map->buildingAvailable(this, canSwim, x, y, &distUnitBuilding) && distUnitBuilding<timeLeft)
						{
							for (int r=0; r<MAX_RESSOURCES; r++)
							{
								int need=needs[r];
								if (need>0)
								{
									int distUnitRessource;
									if (map->ressourceAvailable(teamNumber, r, canSwim, x, y, &distUnitRessource) && (distUnitRessource<timeLeft))
									{
										int value=((distUnitRessource+distUnitBuilding)<<8)/need;
										if (value<minValue)
										{
											unit->destinationPurprose=r;
											fprintf(logFile, "[%d] bdp2 destinationPurprose=%d\n", unit->gid, unit->destinationPurprose);
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
				// We compute all what's available from foreign ressources: (mask)
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
						int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
						int buildingDist;
						if (map->buildingAvailable(this, canSwim, x, y, &buildingDist)
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
												&& map->buildingAvailable(*fbi, canSwim, x, y, &foreignBuildingDist)
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
													fprintf(logFile, "[%d] bdp3 destinationPurprose=%d\n", unit->gid, unit->destinationPurprose);
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
				int needs[MAX_NB_RESSOURCES];
				wishedRessources(needs);
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
						int timeLeft=(unit->hungry-unit->trigHungry)/unit->race->hungryness;
						int distUnitBuilding;
						if (map->buildingAvailable(this, canSwim, x, y, &distUnitBuilding) && distUnitBuilding<timeLeft)
						{
							for (int r=0; r<MAX_RESSOURCES; r++)
							{
								int need=needs[r];
								if (need>0)
								{
									int distUnitRessource;
									if (map->ressourceAvailable(teamNumber, r, canSwim, x, y, &distUnitRessource) && (distUnitRessource<timeLeft))
									{
										int value=((distUnitRessource+distUnitBuilding)<<8)/need;
										if (value<minValue)
										{
											unit->destinationPurprose=r;
											fprintf(logFile, "[%d] bdp4 destinationPurprose=%d\n", unit->gid, unit->destinationPurprose);
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
				unitsWorking.push_back(choosen);
				choosen->subscriptionSuccess();
			}
			else
				break;
		}
		
		updateCallLists();
		for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
			(*it)->standardRandomActivity();
		unitsWorkingSubscribe.clear();
		subscriptionWorkingTimer=0;
		
		if (verbose)
			printf(" ...done\n");
	}
}

void Building::subscribeForFlagingStep()
{
	if (subscriptionWorkingTimer>0)
		subscriptionWorkingTimer++;
	if (fullWorking())
	{
		for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
			(*it)->standardRandomActivity();
		unitsWorkingSubscribe.clear();
		return;
	}
	
	if (subscriptionWorkingTimer>32)
	{
		while (((Sint32)unitsWorking.size()<maxUnitWorking) && !unitsWorkingSubscribe.empty())
		{
			int minValue=INT_MAX;
			Unit *choosen=NULL;
			Map *map=owner->map;
			
			/* To choose a good unit, we get a composition of things:
			1-the closest the unit is, the better it is.
			2-the less the unit is hungry, the better it is.
			3-the more hp the unit has, the better it is.
			*/
			if (type->zonable[EXPLORER])
			{
				for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
				{
					Unit *unit=(*it);
					int timeLeft=unit->hungry/unit->race->hungryness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					timeLeft*=timeLeft;
					hp*=hp;
					int dist=map->warpDistSquare(unit->posX, unit->posY, posX, posY);
					if (dist<timeLeft)
					{
						int value=dist-2*timeLeft-2*hp;
						if (value<minValue)
						{
							minValue=value;
							choosen=unit;
						}
					}
				}
			}
			else if (type->zonable[WARRIOR])
			{
				for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
				{
					Unit *unit=(*it);
					int timeLeft=unit->hungry/unit->race->hungryness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					int dist;
					if (map->buildingAvailable(this, unit->performance[SWIM]>0, unit->posX, unit->posY, &dist) && (dist<timeLeft))
					{
						int value=dist-2*timeLeft-2*hp;
						if (value<minValue)
						{
							minValue=value;
							choosen=unit;
						}
					}
				}
			}
			else if (type->zonable[WORKER])
			{
				for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
				{
					Unit *unit=(*it);
					int timeLeft=unit->hungry/unit->race->hungryness;
					int hp=(unit->hp<<4)/unit->race->unitTypes[0][0].performance[HP];
					int dist;
					bool canSwim=unit->performance[SWIM];
					if (anyRessourceToClear[canSwim]!=2
						&& map->buildingAvailable(this, canSwim, unit->posX, unit->posY, &dist)
						&& (dist<timeLeft))
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
				assert(false);
			
			if (choosen)
			{
				unitsWorkingSubscribe.remove(choosen);
				unitsWorking.push_back(choosen);
				choosen->subscriptionSuccess();
			}
			else
				break;
		}
		
		updateCallLists();
		for (std::list<Unit *>::iterator it=unitsWorkingSubscribe.begin(); it!=unitsWorkingSubscribe.end(); it++)
			(*it)->standardRandomActivity();
		unitsWorkingSubscribe.clear();
		subscriptionWorkingTimer=0;
	}
}

void Building::subscribeForInsideStep()
{
	if (subscriptionInsideTimer>0)
		subscriptionInsideTimer++;
	if (fullInside())
	{
		for (std::list<Unit *>::iterator it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
			(*it)->standardRandomActivity();
		unitsInsideSubscribe.clear();
		return;
	}
	
	if (subscriptionInsideTimer>32)
	{
		Sint32 maxRealUnitInside=maxUnitInside;
		if (type->canFeedUnit)
			maxRealUnitInside=std::min((Uint32)maxUnitInside,(Uint32)(ressources[CORN]-unitsInside.size()));
		else
			maxRealUnitInside=maxUnitInside;
		while (((Sint32)unitsInside.size()<maxRealUnitInside) && !unitsInsideSubscribe.empty())
		{
			int mindist=INT_MAX;
			Unit *choosen=NULL;
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
						choosen=*it;
					}
				}
				else
				{
					int dist=map->warpDistSquare((*it)->posX, (*it)->posY, posX, posY);
					if (map->buildingAvailable(this, unit->performance[SWIM], unit->posX, unit->posY, &dist) && (dist<mindist))
					{
						mindist=dist;
						choosen=*it;
					}
				}
			}
			if (choosen)
			{
				unitsInsideSubscribe.remove(choosen);
				unitsInside.push_back(choosen);
				choosen->subscriptionSuccess();
			}
			else
				break;
		}
		
		updateCallLists();
		for (std::list<Unit *>::iterator it=unitsInsideSubscribe.begin(); it!=unitsInsideSubscribe.end(); it++)
			(*it)->standardRandomActivity();
		unitsInsideSubscribe.clear();
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


void Building::turretStep(void)
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
		if (targetFound == TARGETTYPE_WARRIOR)
			break;
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
		}
	}

}

void Building::clearingFlagsStep(void)
{
	if (unitsWorking.size()<(unsigned)maxUnitWorking)
		for (int canSwim=0; canSwim<2; canSwim++)
			if (localRessourcesCleanTime[canSwim]++>512) // Update every 20.48[s]
				updateClearingFlag(canSwim);
}

void Building::kill(void)
{
	fprintf(logFile, "kill gid=%d buildingState=%d\n", gid, buildingState);
	if (buildingState==DEAD)
		return;
	
	
	fprintf(logFile, " still %zd unitsInside\n", unitsInside.size());
	for (std::list<Unit *>::iterator it=unitsInside.begin(); it!=unitsInside.end(); ++it)
	{
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

int Building::getLongLevel(void)
{
	return ((type->level)<<1)+1-type->isBuildingSite;
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

int Building::availableHappynessLevel(bool guarantee)
{
	int inside = (int)unitsInside.size();
	if (guarantee)
		inside += unitsInsideSubscribe.size();
	if (ressources[CORN] <= inside)
		return 0;
	int happyness = 1;
	for (int i = 0; i < HAPPYNESS_COUNT; i++)
		if (ressources[i + HAPPYNESS_BASE]  >inside)
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

	cs^=maxUnitWorking;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [2]
	cs^=maxUnitWorkingPreferred;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [3]
	cs^=unitsWorking.size();
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [4]
	cs^=maxUnitInside;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [5]
	cs^=unitsInside.size();
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [6]
	cs=(cs<<31)|(cs>>1);

	cs^=posX;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [7]
	cs^=posY;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [8]
	cs=(cs<<31)|(cs>>1);

	cs^=unitStayRange;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [9]

	for (int i=0; i<MAX_RESSOURCES; i++)
		cs^=ressources[i];
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [10]
	cs=(cs<<31)|(cs>>1);

	cs^=hp;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [11]

	cs^=productionTimeout;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [12]
	cs^=totalRatio;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [13]
	
	for (int i=0; i<NB_UNIT_TYPE; i++)
	{
		cs^=ratio[i];
		cs^=percentUsed[i];
		cs=(cs<<31)|(cs>>1);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [14]

	cs^=shootingStep;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [15]
	cs^=shootingCooldown;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [16]
	cs^=bullets;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [17]
	cs=(cs<<31)|(cs>>1);

	cs^=seenByMask;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [18]
	
	cs^=gid;
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [19]
	
	return cs;
}



