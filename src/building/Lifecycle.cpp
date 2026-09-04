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

	inCanFeedUnit=LS_UNKNOWN;
	inCanHealUnit=LS_UNKNOWN;
	callListState=0;

	for (int i=0; i<NB_ABILITY; i++)
		inUpgrade[i]=LS_UNKNOWN;

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

