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
#include <BinaryStream.h>
#include <stdexcept>
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include "Bullet.h"
#include "BuildingGradientSearch.h"

Building::Building(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor)
{
	for (int i=0; i<BUILDING_GRADIENT_COUNT; ++i) globalGradient[i]=NULL;
	for (int i=0; i<SWIM_CLASS_COUNT; i++)
	{
		for (int r=0; r<MAX_NB_RESOURCES; r++)
			roundTripGradient[r][i]=NULL;
	}
	freeGradients();
	load(stream, types, owner, versionMinor);
}

Building::Building(int x, int y, Uint16 gid, Sint32 typeNum, Team *team, BuildingsTypes *types, Sint32 unitWorking, Sint32 unitWorkingFuture)
{
	// identity
	this->gid=gid;
	owner=team;
	scriptIdentity=owner->game->allocateScriptIdentity(true,gid);

	// type
	bindType(typeNum,types);
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


	if (type->isBuildingSite)
	{
		constructionBudget = type->semantics.constructionCost;
		siteCompletionPending = constructionBudget == BuildingResourceCost{};
	}

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

	// flag useful :
	unitStayRange=type->defaultUnitStayRange;
	for(int i=0; i<BASIC_COUNT; i++)
		clearingResources[i]=true;
	clearingResources[STONE]=false;
	minLevelToFlag=0;
	minWorkerLevelToFlag=0;
	explorersRequireBombing=false;

	// building specific :
	for(int i=0; i<MAX_NB_RESOURCES; i++)
	{
		localResource[i]=0;
		wishedResources[i]=0;
	}
	updateResourcesPointer();

	// quality parameters
	hp=getEffectiveInitHp(); // (Uint16)

	// preferred parameters

	resetProduction();
	totalRatio = 0;
	for (int i = 0; i < NB_UNIT_TYPE; ++i)
	{
		ratio[i] = type->semantics.production.initialRatios[i];
		totalRatio += ratio[i];
		percentUsed[i] = 0;
	}

	receiveResourceMask=0;
	sendResourceMask=0;

	shootingStep=0;
	shootingCooldown=SHOOTING_COOLDOWN_MAX;
	bullets=0;

	seenByMask=0;

	inCanFeedUnit=LS_UNKNOWN;
	inCanHealUnit=LS_UNKNOWN;
	callListState=0;

	for (int i=0; i<NB_ABILITY; i++)
		inUpgrade[i]=LS_UNKNOWN;

	for (int i=0; i<BUILDING_GRADIENT_COUNT; ++i) globalGradient[i]=NULL;
	for (int i=0; i<SWIM_CLASS_COUNT; i++)
	{
		for (int r=0; r<MAX_NB_RESOURCES; r++)
			roundTripGradient[r][i]=NULL;
	}
	freeGradients();

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

BuildingRoute Building::resolveRoute(BuildingRoute route) const
{
	if (route != BuildingRoute::Automatic) return route;
	if (!type->semantics.occupiesGround)
	{
		if (type->zonable[WORKER]) return BuildingRoute::Clearing;
		if (type->zonable[WARRIOR]) return BuildingRoute::Combat;
	}
	return BuildingRoute::Footprint;
}

void Building::dirtyGradients()
{
	for (int i=0; i<BUILDING_GRADIENT_COUNT; i++)
		dirtyGradient[i] = true;
	for (int i=0; i<BUILDING_ACCESS_COUNT; i++)
		locked[i] = false;
}

void Building::resetPathfindGradients()
{
	dirtyGradients();
	for (int i=0; i<BUILDING_GRADIENT_COUNT; i++)
	{
		recycleBuildingGradientSearch(std::move(globalGradientSearch[i]));
		owner->game->map.recycleBuildingGradientBuffer(globalGradient[i]);
		globalGradient[i] = NULL;
		gradientGeneration[i] = 0;
	}
	resetRoundTripGradients();
}

void Building::resetRoundTripGradients()
{
	for (int i=0; i<SWIM_CLASS_COUNT; i++)
	{
		for (int r=0; r<MAX_NB_RESOURCES; r++)
		{
			owner->game->map.recycleBuildingGradientBuffer(roundTripGradient[r][i]);
			roundTripGradient[r][i] = NULL;
			roundTripGradientStep[r][i] = 0;
			roundTripGradientUsedStep[r][i] = 0;
		}
	}
}

void Building::freeIdleGradients()
{
	// Units keep a gradient alive by reading it; 500 ticks after the last one, it goes.
	constexpr Uint32 IDLE_TICKS = 500;
	Uint32 now = owner->game->stepCounter;
	for (int c=0; c<BUILDING_GRADIENT_COUNT; c++)
	{
		if (globalGradient[c] && globalGradientUsedStep[c]+IDLE_TICKS<now)
		{
			recycleBuildingGradientSearch(std::move(globalGradientSearch[c]));
			owner->game->map.recycleBuildingGradientBuffer(globalGradient[c]);
			globalGradient[c] = NULL;
		}
	}
	for (int c=0; c<SWIM_CLASS_COUNT; c++)
	{
		for (int r=0; r<MAX_NB_RESOURCES; r++)
			if (roundTripGradient[r][c] && roundTripGradientUsedStep[r][c]+IDLE_TICKS<now)
			{
				owner->game->map.recycleBuildingGradientBuffer(roundTripGradient[r][c]);
				roundTripGradient[r][c] = NULL;
			}
	}
}

void Building::freeGradients()
{
	// Construction, reload and teardown may have no usable owner/map, or the
	// map may have changed size. Only live invalidations recycle storage.
	dirtyGradients();
	for (int i=0; i<BUILDING_GRADIENT_COUNT; i++)
	{
		globalGradientSearch[i].reset();
		delete[] globalGradient[i];
		globalGradient[i] = NULL;
		gradientGeneration[i] = 0;
	}
	for (int i=0; i<SWIM_CLASS_COUNT; i++)
	{
		for (int r=0; r<MAX_NB_RESOURCES; r++)
		{
			delete[] roundTripGradient[r][i];
			roundTripGradient[r][i] = NULL;
			roundTripGradientStep[r][i] = 0;
			roundTripGradientUsedStep[r][i] = 0;
		}
	}
	for (int i=0; i<BUILDING_GRADIENT_COUNT; i++)
	{
		lastGlobalGradientUpdateStepCounter[i] = 0;
		globalGradientUsedStep[i] = 0;
	}
	for (int i=0; i<SWIM_VARIANT_COUNT; i++)
		anyResourceToClear[i] = 0;
}

void Building::load(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor)
{
	stream->readEnterSection("Building");

	// Validate raw file values before enum casts or table indexing: simulation
	// code assumes named states and identities belonging to the owning team.
	const Uint32 savedState = stream->readUint32("buildingState");
	const Uint32 savedResult = stream->readUint32("constructionResultState");
	if (savedState > WAITING_FOR_CONSTRUCTION_ROOM || savedResult > REPAIR)
		throw std::runtime_error("Invalid building construction state");
	buildingState = static_cast<BuildingState>(savedState);
	constructionResultState = static_cast<ConstructionResultState>(savedResult);

	// identity
	gid = stream->readUint16("gid");
	if (gid >= MAX_COUNT * Team::MAX_COUNT || GIDtoTeam(gid) != owner->teamNumber)
		throw std::runtime_error("Invalid building identity");
	scriptIdentity = versionMinor >= FILE_FORMAT_VERSION_JAVASCRIPT ? stream->readUint32("scriptIdentity") : owner->game->allocateScriptIdentity(true,gid);
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
	if (unitStayRange < 0 || unitStayRange > 32767) throw std::runtime_error("Invalid flag range");

	for (int i=0; i<BASIC_COUNT; i++)
	{
		std::ostringstream oss;
		oss << "clearingRessources[" << i << "]";
		clearingResources[i] = (bool)stream->readSint32(oss.str().c_str());
	}
	if (clearingResources[STONE]) throw std::runtime_error("Invalid stone clearing flag");

	minLevelToFlag = stream->readSint32("minLevelToFlag");
	if (minLevelToFlag < 0 || minLevelToFlag >= NB_UNIT_LEVELS) throw std::runtime_error("Invalid flag level");

	// Building Specific
	for (int i=0; i<MAX_NB_RESOURCES; i++)
	{
		std::ostringstream oss;
		oss << "localRessource[" << i << "]";
		localResource[i] = stream->readSint32(oss.str().c_str());
		if (localResource[i]<0) throw std::runtime_error("Invalid negative building inventory");
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
			ratio[i] = stream->readSint32(oss.str().c_str());
			if (ratio[i] < 0 || ratio[i] > 32767) throw std::runtime_error("Invalid swarm ratio");
		}
		{
			std::ostringstream oss;
			oss << "percentUsed[" << i << "]";
			percentUsed[i] = stream->readSint32(oss.str().c_str());
			if (percentUsed[i] < 0 || percentUsed[i] > 32767) throw std::runtime_error("Invalid swarm production state");
		}
	}

	receiveResourceMask = stream->readUint32("receiveRessourceMask");
	sendResourceMask = stream->readUint32("sendRessourceMask");

	shootingStep = stream->readUint32("shootingStep");
	shootingCooldown = stream->readSint32("shootingCooldown");
	bullets = stream->readSint32("bullets");

	// type
	typeNum = stream->readSint32("typeNum");
	if (typeNum < 0 || static_cast<size_t>(typeNum) >= types->size())
		throw std::runtime_error("Invalid building type");
	bindType(typeNum,types);
	assert(type);
	updateResourcesPointer();

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

	minWorkerLevelToFlag = 0;
	explorersRequireBombing = type->zonable[EXPLORER] && minLevelToFlag != 0;
	siteCompletionPending = false;
	productionUnit = -1;
	if (versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG)
	{
		minWorkerLevelToFlag = stream->readSint32("minWorkerLevelToFlag");
		if (minWorkerLevelToFlag < 0 || minWorkerLevelToFlag >= NB_UNIT_LEVELS) throw std::runtime_error("Invalid worker flag qualification");
		explorersRequireBombing = stream->readUint8("explorersRequireBombing") != 0;
		siteCompletionPending = stream->readUint8("siteCompletionPending") != 0;
		for (int unit=0; unit<NB_UNIT_TYPE; ++unit) {
			constructionOriginRatios[unit]=stream->readSint32(("constructionOriginRatio["+std::to_string(unit)+"]").c_str());
			if (constructionOriginRatios[unit]<0 || constructionOriginRatios[unit]>32767) throw std::runtime_error("Invalid saved construction production preference");
		}
		constructionOriginTypeNum = stream->readSint32("constructionOriginTypeNum");
		if (constructionOriginTypeNum < -1 || constructionOriginTypeNum >= int(types->size())) throw std::runtime_error("Invalid saved construction origin");
		const bool pending = buildingState==WAITING_FOR_CONSTRUCTION || buildingState==WAITING_FOR_CONSTRUCTION_ROOM;
		bool validOrigin=false;
		if (constructionResultState==NO_CONSTRUCTION)
			validOrigin=!type->isBuildingSite && !pending && constructionOriginTypeNum==-1;
		else if (constructionResultState==NEW_BUILDING)
			validOrigin=type->isBuildingSite && !pending && constructionOriginTypeNum==-1;
		else if (constructionOriginTypeNum>=0)
		{
			const BuildingType* origin=types->get(constructionOriginTypeNum);
			const int site=constructionResultState==REPAIR ? origin->prevLevel : origin->nextLevel;
			validOrigin=!origin->isBuildingSite && site>=0 && types->get(site)->isBuildingSite
				&& (constructionResultState!=REPAIR || origin->semantics.repairable)
				&& (type->isBuildingSite ? !pending && site==typeNum
					: (pending || buildingState==DEAD) && constructionOriginTypeNum==typeNum);
		}
		if (!validOrigin) throw std::runtime_error("Saved construction origin does not match its job and stage");
		repairInitialDeficit = stream->readSint32("repairInitialDeficit");
		repairHealthGranted = stream->readSint32("repairHealthGranted");
		if (repairInitialDeficit<0 || repairInitialDeficit>getEffectiveMaxHp() || repairHealthGranted<0 || repairHealthGranted>repairInitialDeficit)
			throw std::runtime_error("Invalid saved repair progress");
		for (int r=0; r<MAX_NB_RESOURCES; ++r)
		{
			constructionBudget[r] = stream->readSint32(("constructionBudget["+std::to_string(r)+"]").c_str());
			constructionReserved[r] = stream->readSint32(("constructionReserved["+std::to_string(r)+"]").c_str());
			if (constructionBudget[r]<0 || constructionBudget[r]>1000000 || constructionReserved[r]<0 || constructionReserved[r]>constructionBudget[r])
				throw std::runtime_error("Invalid saved construction budget");
		}
		productionUnit = stream->readSint32("productionUnit");
		if (productionUnit < -1 || productionUnit >= NB_UNIT_TYPE)
			throw std::runtime_error("Invalid saved production recipe");
	}
	if (versionMinor < FILE_FORMAT_VERSION_BUILDING_CATALOG && constructionResultState != NO_CONSTRUCTION)
	{
		std::copy_n(ratio,NB_UNIT_TYPE,constructionOriginRatios.begin());
		constructionOriginTypeNum = type->isBuildingSite ? (constructionResultState == UPGRADE ? type->prevLevel : constructionResultState == REPAIR ? type->nextLevel : -1) : typeNum;
		if (type->isBuildingSite)
		{
			constructionBudget = type->semantics.constructionCost;
			if (constructionResultState == REPAIR) repairInitialDeficit=std::max(0,getEffectiveMaxHp()-hp);
			for (int r=0; r<MAX_NB_RESOURCES; ++r)
				if (constructionResultState == REPAIR)
				{
					constructionBudget[r] = std::max(0, constructionBudget[r]-localResource[r]);
					localResource[r] = 0; // Old repair credits were never real inventory.
				}
				else constructionReserved[r] = std::min(constructionBudget[r], localResource[r]);
		}
	}
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
	stream->writeUint32(scriptIdentity, "scriptIdentity");
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

	stream->writeSint32(minWorkerLevelToFlag, "minWorkerLevelToFlag");
	stream->writeUint8(explorersRequireBombing, "explorersRequireBombing");
	stream->writeUint8(siteCompletionPending, "siteCompletionPending");
	for (int unit=0; unit<NB_UNIT_TYPE; ++unit)
		stream->writeSint32(constructionOriginRatios[unit],("constructionOriginRatio["+std::to_string(unit)+"]").c_str());
	stream->writeSint32(constructionOriginTypeNum, "constructionOriginTypeNum");
	stream->writeSint32(repairInitialDeficit, "repairInitialDeficit");
	stream->writeSint32(repairHealthGranted, "repairHealthGranted");
	for (int r=0; r<MAX_NB_RESOURCES; ++r)
	{
		stream->writeSint32(constructionBudget[r], ("constructionBudget["+std::to_string(r)+"]").c_str());
		stream->writeSint32(constructionReserved[r], ("constructionReserved["+std::to_string(r)+"]").c_str());
	}
	stream->writeSint32(productionUnit, "productionUnit");
	stream->writeUint32(seenByMask, "seenByMask");

	stream->writeLeaveSection();
}

void Building::loadCrossRef(GAGCore::InputStream *stream, BuildingsTypes *types, Team *owner, Sint32 versionMinor)
{
	stream->readEnterSection("Building");

	// units
	maxUnitInside = stream->readSint32("maxUnitInside");
	if (maxUnitInside < 0 || maxUnitInside >= MAX_UNIT_INSIDE_LIMIT)
		throw std::runtime_error("Invalid building capacity");

	unsigned nbWorking = stream->readUint32("nbWorking");
	if (nbWorking > Unit::MAX_COUNT) throw std::runtime_error("Invalid building unit list size");
	unitsWorking.clear();
	for (unsigned i=0; i<nbWorking; i++)
	{
		std::ostringstream oss;
		oss << "unitsWorking[" << i << "]";
		const Uint16 unitGid = stream->readUint16(oss.str().c_str());
		if (unitGid >= Unit::MAX_COUNT * Team::MAX_COUNT || Unit::GIDtoTeam(unitGid) != owner->teamNumber || !owner->myUnits[Unit::GIDtoID(unitGid)])
			throw std::runtime_error("Invalid building unit reference");
		Unit *unit = owner->myUnits[Unit::GIDtoID(unitGid)];
		if (versionMinor >= FILE_FORMAT_VERSION_SIMULATION_CONTINUATION) unitsWorking.push_back(unit);
		else unitsWorking.push_front(unit);
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
	if (nbInside > Unit::MAX_COUNT) throw std::runtime_error("Invalid building unit list size");
	unitsInside.clear();
	for (unsigned i=0; i<nbInside; i++)
	{
		std::ostringstream oss;
		oss << "unitsInside[" << i << "]";
		const Uint16 unitGid = stream->readUint16(oss.str().c_str());
		if (unitGid >= Unit::MAX_COUNT * Team::MAX_COUNT || Unit::GIDtoTeam(unitGid) != owner->teamNumber || !owner->myUnits[Unit::GIDtoID(unitGid)])
			throw std::runtime_error("Invalid building unit reference");
		Unit *unit = owner->myUnits[Unit::GIDtoID(unitGid)];
		if (versionMinor >= FILE_FORMAT_VERSION_SIMULATION_CONTINUATION) unitsInside.push_back(unit);
		else unitsInside.push_front(unit);
	}
	
	if (versionMinor>=FILE_FORMAT_VERSION_UNITS_HARVESTING_LIST)
	{
		unsigned nbHarvesting = stream->readUint32("nbHarvesting");
		if (nbHarvesting > Unit::MAX_COUNT) throw std::runtime_error("Invalid building unit list size");
		unitsHarvesting.clear();
		for (unsigned i=0; i<nbHarvesting; i++)
		{
			std::ostringstream oss;
			oss << "unitsHarvesting[" << i << "]";
			const Uint16 unitGid = stream->readUint16(oss.str().c_str());
			if (unitGid >= Unit::MAX_COUNT * Team::MAX_COUNT || Unit::GIDtoTeam(unitGid) != owner->teamNumber || !owner->myUnits[Unit::GIDtoID(unitGid)])
				throw std::runtime_error("Invalid building unit reference");
			Unit *unit = owner->myUnits[Unit::GIDtoID(unitGid)];
			if (versionMinor >= FILE_FORMAT_VERSION_SIMULATION_CONTINUATION) unitsHarvesting.push_back(unit);
			else unitsHarvesting.push_front(unit);
		}
	}

	if (versionMinor >= FILE_FORMAT_VERSION_SIMULATION_CONTINUATION)
	{
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		desiredMaxUnitWorking = stream->readSint32("desiredMaxUnitWorking");
		oldPriority = stream->readSint32("oldPriority");
		callListState = stream->readUint8("callListState");
		if (callListState > 1) throw std::runtime_error("Invalid building call list state");
		auto readListState = [&](const char* name) {
			const Uint8 value = stream->readUint8(name);
			if (value > LS_OUT) throw std::runtime_error("Invalid building service list state");
			return static_cast<InListState>(value);
		};
		inCanFeedUnit = readListState("inCanFeedUnit");
		inCanHealUnit = readListState("inCanHealUnit");
		for (int i=0; i<NB_ABILITY; ++i)
		{
			stream->readEnterSection(i);
			inUpgrade[i] = readListState("inUpgrade");
			stream->readLeaveSection();
		}
		for (int i=0; i<MAX_NB_RESOURCES; ++i)
		{
			stream->readEnterSection(i);
			wishedResources[i] = stream->readSint32("wishedResource");
			stream->readLeaveSection();
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

	stream->writeSint32(desiredMaxUnitWorking, "desiredMaxUnitWorking");
	stream->writeSint32(oldPriority, "oldPriority");
	stream->writeUint8(callListState, "callListState");
	stream->writeUint8(inCanFeedUnit, "inCanFeedUnit");
	stream->writeUint8(inCanHealUnit, "inCanHealUnit");
	for (int i=0; i<NB_ABILITY; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeUint8(inUpgrade[i], "inUpgrade");
		stream->writeLeaveSection();
	}
	for (int i=0; i<MAX_NB_RESOURCES; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeSint32(wishedResources[i], "wishedResource");
		stream->writeLeaveSection();
	}

	stream->writeLeaveSection();
}



void Building::bindType(Sint32 id, BuildingsTypes* catalog)
{
    if (!catalog) catalog=&owner->game->buildingsTypes;
    typeNum=id;
    type=catalog->get(id);
    runtime=catalog->getRuntime(id);
}
