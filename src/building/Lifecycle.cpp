// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <list>
#include <math.h>
#include <sstream>
#include <Stream.h>
#include <stdlib.h>

#include "Building.h"
#include "BuildingType.h"
#include "EngineTiming.h"
#include "FileFormatVersions.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include "Bullet.h"

Building::Building(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor)
{
	for (int i=0; i<SWIM_VARIANT_COUNT; i++)
	{
		globalGradient[i]=NULL;
		localRessources[i]=NULL;
	}
	load(stream, types, owner, versionMinor);
}

Building::Building(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, BuildingsTypes *types, Sint32 unitWorking, Sint32 unitWorkingFuture)
{
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
	maxUnitWorkingPreferred = maxUnitWorking;
	maxUnitWorkingFuture = unitWorkingFuture;
	maxUnitWorkingPrevious = 0;
	desiredMaxUnitWorking = maxUnitWorking;
	subscriptionWorkingTimer = 0;
	priority = 0;
	oldPriority = 0;

	// position
	posX=x;
	posY=y;

	underAttackTimer=0;
	canNotConvertUnitTimer=0;

	// flag usefull :
	unitStayRange=type->defaultUnitStayRange;
	for(int i=0; i<BASIC_COUNT; i++)
		clearingRessources[i]=true;
	clearingRessources[STONE]=false;
	minLevelToFlag=0;

	// building specific :
	for(int i=0; i<MAX_NB_RESSOURCES; i++)
		localRessource[i]=0;
	updateRessourcesPointer();

	// quality parameters
	hp=type->hpInit; // (Uint16)

	// prefered parameters

	productionTimeout=type->unitProductionTime;

	totalRatio=0;
	ratio[0]=1;
	totalRatio++;
	percentUsed[0]=0;
	for (int i=1; i<NB_UNIT_TYPE; i++)
	{
		ratio[i]=0;
		//totalRatio++;
		percentUsed[i]=0;
	}

	receiveRessourceMask=0;
	sendRessourceMask=0;

	shootingStep=0;
	shootingCooldown=SHOOTING_COOLDOWN_MAX;
	bullets=0;

	seenByMask=0;

	inCanFeedUnit=LS_UNKNOWN;
	inCanHealUnit=LS_UNKNOWN;
	callListState=0;

	for (int i=0; i<NB_ABILITY; i++)
		inUpgrade[i]=LS_UNKNOWN;

	for (int i=0; i<SWIM_VARIANT_COUNT; i++)
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

	lastShootStep = LAST_SHOOT_STEP_NEVER;
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

void Building::resetLocalRessources()
{
	for (int i=0; i<SWIM_VARIANT_COUNT; i++)
	{
		dirtyLocalGradient[i] = true;
		locked[i] = false;
		delete[] localRessources[i];
		localRessources[i] = NULL;
	}
}

void Building::resetPathfindGradients()
{
	for (int i=0; i<SWIM_VARIANT_COUNT; i++)
	{
		dirtyLocalGradient[i] = true;
		locked[i] = false;
		delete[] globalGradient[i];
		globalGradient[i] = NULL;
		delete[] localRessources[i];
		localRessources[i] = NULL;
	}
}

void Building::freeGradients()
{
	resetPathfindGradients();
	for (int i=0; i<SWIM_VARIANT_COUNT; i++)
	{
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

	if(versionMinor>=FILE_FORMAT_VERSION_UNDER_ATTACK_TIMER)
		underAttackTimer = stream->readUint8("underAttackTimer");
	else
		underAttackTimer = 0;
	if(versionMinor>=FILE_FORMAT_VERSION_CANNOT_CONVERT_TIMER)
		canNotConvertUnitTimer = stream->readUint8("canNotConvertUnitTimer");
	else
		canNotConvertUnitTimer = CANNOT_CONVERT_TIMER_INIT;

	// priority
	if(versionMinor>=FILE_FORMAT_VERSION_BUILDING_PRIORITY_FIELD)
	{
		priority = stream->readSint32("priority");
		// Legacy "priorityLocal" slot — was a per-viewer GUI shadow that now
		// lives in BuildingGuiState. Read and discard to keep the on-disk
		// format byte-equivalent for replay-baseline determinism.
		(void)stream->readSint32("priorityLocal");
		oldPriority = priority;
	}
	else
	{
		priority = 0;
		oldPriority = 0;
	}

	// Flag specific
	unitStayRange = stream->readUint32("unitStayRange");

	for (int i=0; i<BASIC_COUNT; i++)
	{
		std::ostringstream oss;
		oss << "clearingRessources[" << i << "]";
		clearingRessources[i] = (bool)stream->readSint32(oss.str().c_str());
	}
	assert(clearingRessources[STONE] == false);

	minLevelToFlag = stream->readSint32("minLevelToFlag");

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
			ratio[i] = stream->readSint32(oss.str().c_str());
		}
		{
			std::ostringstream oss;
			oss << "percentUsed[" << i << "]";
			percentUsed[i] = stream->readSint32(oss.str().c_str());
		}
	}

	receiveRessourceMask = stream->readUint32("receiveRessourceMask");
	sendRessourceMask = stream->readUint32("sendRessourceMask");

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
	maxUnitWorkingPreferred = 1;
	maxUnitWorkingFuture = 1;
	desiredMaxUnitWorking = maxUnitWorking;
	subscriptionWorkingTimer = 0;

	owner->prestige += type->prestige;

	seenByMask = stream->readUint32("seenByMask");

	inCanFeedUnit=LS_UNKNOWN;
	inCanHealUnit=LS_UNKNOWN;
	callListState = 0;

	for (int i=0; i<NB_ABILITY; i++)
		inUpgrade[i] = LS_UNKNOWN;

	freeGradients();

	verbose = false;
	stream->readLeaveSection();

	lastShootStep = LAST_SHOOT_STEP_NEVER;
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
	// Legacy "priorityLocal" slot, preserved for save-format compatibility.
	// The per-viewer GUI shadow lives in BuildingGuiState now; the slot is
	// filled with `priority` so on-disk bytes are unchanged during headless
	// determinism baselines (no GUI is ever attached, so the old value would
	// always have equalled `priority` anyway).
	stream->writeSint32(priority, "priorityLocal");

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

	// units
	maxUnitInside = stream->readSint32("maxUnitInside");
	assert(maxUnitInside < MAX_UNIT_INSIDE_LIMIT);

	unsigned nbWorking = stream->readUint32("nbWorking");
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
	if(versionMinor>=FILE_FORMAT_VERSION_MAX_UNIT_WORKING_PREVIOUS)
		maxUnitWorkingPrevious = stream->readSint32("maxUnitWorkingPrevious");
	else
		maxUnitWorkingPrevious = maxUnitWorkingPreferred;
	if(versionMinor>=FILE_FORMAT_VERSION_MAX_UNIT_WORKING_FUTURE)
		maxUnitWorkingFuture = stream->readSint32("maxUnitWorkingFuture");
	desiredMaxUnitWorking = maxUnitWorking;

	if(versionMinor>=FILE_FORMAT_VERSION_UNITS_FAILING_REQUIREMENTS_INT && versionMinor<FILE_FORMAT_VERSION_UNITS_FAILING_REQUIREMENTS_ARRAY)
	{
		stream->readSint32("unitsFailingRequirements");
	}
	else if(versionMinor>=FILE_FORMAT_VERSION_UNITS_FAILING_REQUIREMENTS_ARRAY)
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
	unitsInside.clear();
	for (unsigned i=0; i<nbInside; i++)
	{
		std::ostringstream oss;
		oss << "unitsInside[" << i << "]";
		Unit *unit = owner->myUnits[Unit::GIDtoID(stream->readUint16(oss.str().c_str()))];
		assert(unit);
		unitsInside.push_front(unit);
	}
	
	if (versionMinor>=FILE_FORMAT_VERSION_UNITS_HARVESTING_LIST)
	{
		unsigned nbHarvesting = stream->readUint32("nbHarvesting");
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

	// units
	stream->writeSint32(maxUnitInside, "maxUnitInside");
	//TODO: std::list::size() is O(n). We should investigate
	//if our intense use of this has an impact on overall performance.
	//steph and nuage suggested to store and update size in a variable
	//what is faster but also more error prone.
	stream->writeUint32(unitsWorking.size(), "nbWorking");
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

