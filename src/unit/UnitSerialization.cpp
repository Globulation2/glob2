// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Material.h"
#include <algorithm>
#include "Unit.h"
#include <BinaryStream.h>
#include <stdexcept>
#include "Race.h"
#include "Team.h"
#include "Map.h"
#include "Game.h"

#include "Building.h"
#include "Integrity.h"

#include "FileFormatVersions.h"
#include "Utilities.h"
#include "EntityRandomIO.h"
#include <Stream.h>

void Unit::load(GAGCore::InputStream *stream, Team *owner, Sint32 versionMinor)
{
	stream->readEnterSection("Unit");

	// unit specification
	// File-controlled types and identities reach fixed tables later. Validate
	// before indexing or converting raw integers into simulation enums.
	const Sint32 savedType=stream->readSint32("typeNum");
	if (savedType < 0 || savedType >= int(owner->race.unitTypeCount())) throw std::runtime_error("Invalid unit type");
	core.typeNum=Uint16(savedType);
	if (versionMinor < FILE_FORMAT_VERSION_DROP_UNIT_SKIN_NAME)
	{
		// Pre-v84 saves carried a per-unit skinName string; skin is now derived
		// from typeNum at render time, so read and discard for compatibility.
		stream->readText("skinName");
	}
	race = &(owner->race);
	assert(race);
	core.capabilityFlags=race->getRuntime(core.typeNum).flags;
	detail.configuredFoodCapacity=race->getRuntime(core.typeNum).foodCapacity;
	detail.configuredVisionRadius=Uint8(race->getRuntime(core.typeNum).visionRadius);
	core.jobPurpose=UnitJobPurpose::None;
	detail.regenerationRemainder=0;
	detail.widePrimaryCargo=false;

	// identity
	core.gid = stream->readUint16("gid");
	if (core.gid >= MAX_COUNT * Team::MAX_COUNT || GIDtoTeam(core.gid) != owner->teamNumber)
		throw std::runtime_error("Invalid unit identity");
	core.scriptIdentity=versionMinor >= FILE_FORMAT_VERSION_JAVASCRIPT ? stream->readUint32("scriptIdentity") : owner->game->allocateScriptIdentity(false,core.gid);
	this->owner = owner;
	if (versionMinor >= FILE_FORMAT_VERSION_ENTITY_RANDOM)
		loadEntityRandom(stream, detail.entityRandom);
	else
		detail.entityRandom.initialize(owner->game->gameHeader.getRandomSeed(), EntityRandom::Kind::Unit, core.gid, core.scriptIdentity);
	core.isDead = stream->readSint32("isDead");
	diagnosticDeathCause = GameplayMeasurements::UNKNOWN;
	if (versionMinor >= FILE_FORMAT_VERSION_GAMEPLAY_STATS)
	{
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		diagnosticDeathCause = stream->readSint32("diagnosticDeathCause");
		if (diagnosticDeathCause < 0 || diagnosticDeathCause >= GameplayMeasurements::DEATH_CAUSES)
			throw std::runtime_error("Invalid diagnostic death cause");
	}

	// position
	core.posX = stream->readSint32("posX");
	core.posY = stream->readSint32("posY");
	core.delta = stream->readSint32("delta");
	core.dx = stream->readSint32("dx");
	core.dy = stream->readSint32("dy");
	core.direction = stream->readSint32("direction");
	detail.insideTimeout = stream->readSint32("insideTimeout");
	core.speed = stream->readSint32("speed");
	detail.terrainHealthRemainder = versionMinor >= FILE_FORMAT_VERSION_TERRAIN_PROPERTIES ? stream->readSint32("terrainHealthRemainder") : 0;
	if (detail.terrainHealthRemainder <= -256 || detail.terrainHealthRemainder >= 256) throw std::runtime_error("Invalid terrain exposure remainder");

	detail.areaServiceRemainders.fill(0);
	detail.areaLastPulseTick=Uint32(-1);
	if (versionMinor>=FILE_FORMAT_VERSION_AREA_EFFECTS) {
		detail.areaLastPulseTick=stream->readUint32("areaLastPulseTick");
		if (detail.areaLastPulseTick!=Uint32(-1) && (detail.areaLastPulseTick&15)) throw std::runtime_error("Invalid area pulse tick");
		for (int i=0;i<3;++i) {
			const auto remainder=stream->readUint16(("areaServiceRemainder"+std::to_string(i)).c_str());
			if (remainder>=256) throw std::runtime_error("Invalid area service remainder");
			detail.areaServiceRemainders[i]=Uint8(remainder);
		}
	}
	// states
	detail.needToRecheckMedical = (bool)stream->readUint32("needToRecheckMedical");
	auto readState = [&](const char* name, Uint32 maximum) {
		const Uint32 value = stream->readUint32(name);
		if (value > maximum) throw std::runtime_error("Invalid unit action state");
		return value;
	};
	core.medical = static_cast<Medical>(readState("medical", MED_DAMAGED));
	core.activity = static_cast<Activity>(readState("activity", ACT_UPGRADING));
	core.displacement = static_cast<Displacement>(readState("displacement", DIS_EXITING_BUILDING));
	detail.serviceResourcesReserved = versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG
		? readState("serviceResourcesReserved", 1) != 0
		: core.activity == ACT_UPGRADING && core.displacement != DIS_EXITING_BUILDING;
	core.movement = static_cast<Movement>(readState("movement", MOV_ATTACKING_TARGET));
	core.action = static_cast<Abilities>(readState("action", NB_ABILITY - 1));
	if ((core.displacement % 2) != 0 || core.movement == 10 ||
		core.direction < 0 || core.direction > UNIT_DIRECTION_NONE || core.dx < -1 || core.dx > 1 || core.dy < -1 || core.dy > 1)
		throw std::runtime_error("Invalid unit action state");
	core.targetX = (Sint32)stream->readSint32("targetX");
	core.targetY = (Sint32)stream->readSint32("targetY");
	core.validTarget = (bool)stream->readSint32("validTarget");
	detail.magicActionTimeout = stream->readSint32("magicActionTimeout");

	// under attack timer
	if(versionMinor >= FILE_FORMAT_VERSION_UNDER_ATTACK_TIMER)
		core.underAttackTimer = stream->readUint8("underAttackTimer");
	else
		core.underAttackTimer = 0;


	// trigger parameters
	core.hp = stream->readSint32("hp");
	detail.trigHP = stream->readSint32("trigHP");

	// hungry
	core.hungry = stream->readSint32("hungry");
	detail.hungriness = stream->readSint32("hungryness");
	detail.trigHungry = stream->readSint32("trigHungry");
	detail.trigHungryCarrying = Sint64(foodCapacity())*runtimeTraits().carryingTriggerNumerator/runtimeTraits().carryingTriggerDenominator;
	detail.fruitMask = stream->readUint32("fruitMask");
	detail.fruitCount = stream->readUint32("fruitCount");

	// quality parameters
	stream->readEnterSection("abilities");
	for (int i=0; i<NB_ABILITY; i++)
	{
		stream->readEnterSection(i);
		performance(i) = stream->readSint32("performance");
		detail.level[i] = stream->readSint32("level");
		if (versionMinor>=FILE_FORMAT_VERSION_UNIT_CATALOG && (performance(i)<0 || performance(i)>1000000
            || (i==HP && performance(i)==0 && !hasCapability(UnitRuntimeTraits::LegacyPerformancePolicies))))
			throw std::runtime_error("Invalid cached unit ability");
		if (detail.level[i] < 0 || detail.level[i] >= NB_UNIT_LEVELS) throw std::runtime_error("Invalid unit level");
		detail.canLearn[i] = (bool)stream->readUint32("canLearn");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	// Harvest and build are one worker level. A save from before that could
	// hold the two apart (the editor used to offer a box for each), so even
	// such a worker out to the higher of the two.
	if (versionMinor < FILE_FORMAT_VERSION_ONE_WORKER_LEVEL
		&& detail.canLearn[BUILD] && detail.level[HARVEST] != detail.level[BUILD])
		setWorkerLevel(std::max(detail.level[HARVEST], detail.level[BUILD]));

	core.constructionLevel = versionMinor >= FILE_FORMAT_VERSION_BUILDING_CATALOG
		? stream->readSint32("constructionLevel") : detail.level[BUILD];
	if (core.constructionLevel < 0 || core.constructionLevel >= NB_UNIT_LEVELS) throw std::runtime_error("Invalid construction qualification");

	detail.experience = stream->readSint32("experience");
	core.experienceLevel = stream->readSint32("experienceLevel");
	if (versionMinor>=FILE_FORMAT_VERSION_UNIT_CATALOG && (detail.experience<0 || core.experienceLevel<0 || core.speed<0 || core.delta<0 || detail.fruitCount>3))
		throw std::runtime_error("Invalid cached unit clock or combat state");

	core.destinationPurpose = stream->readSint32("destinationPurpose");
	core.carriedMaterial = stream->readSint32("carriedRessource");
	detail.carriedPacket={};
	if (versionMinor>=FILE_FORMAT_VERSION_BUILDING_CATALOG)
	{
		detail.carriedPacket.numerator=stream->readUint32("carriedNumerator");
		detail.carriedPacket.denominator=stream->readUint32("carriedDenominator");
		if (!detail.carriedPacket.numerator || !detail.carriedPacket.denominator || detail.carriedPacket.denominator>1000000 || detail.carriedPacket.numerator>detail.carriedPacket.denominator)
			throw std::runtime_error("Invalid carried material packet");
	}
	if (core.carriedMaterial < -1 || core.carriedMaterial >= int(MaterialCount) || core.destinationPurpose < -1 || core.destinationPurpose > FEED)
		throw std::runtime_error("Invalid unit material or destination");
	if ((core.activity == ACT_FILLING && (core.destinationPurpose < 0 || core.destinationPurpose >= MaterialCount)) ||
		(core.activity == ACT_UPGRADING && core.destinationPurpose < 0))
		throw std::runtime_error("Invalid unit activity destination");

	detail.jobTimer = stream->readSint32("jobTimer");

	previousClearingArea=std::nullopt;
	previousClearingAreaDistance=0;
	if (versionMinor >= FILE_FORMAT_VERSION_SIMULATION_CONTINUATION)
	{
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		const Uint8 claimed = stream->readUint8("hasClearingClaim");
		if (claimed > 1) throw std::runtime_error("Invalid unit clearing claim");
		if (claimed)
		{
			const Uint32 x = stream->readUint32("clearingClaimX");
			const Uint32 y = stream->readUint32("clearingClaimY");
			previousClearingArea = ClearingAreaClaim{x, y};
		}
		previousClearingAreaDistance = stream->readUint32("clearingClaimDistance");
	}

	owner->game->unitCargo.erase(core.gid);
	if (versionMinor>=FILE_FORMAT_VERSION_UNIT_CATALOG) {
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		const unsigned purpose=stream->readUint8("jobPurpose");
		if (purpose>unsigned(UnitJobPurpose::Defend)) throw std::runtime_error("Invalid unit job purpose");
		core.jobPurpose=UnitJobPurpose(purpose);
		if ((core.activity==ACT_FLAG && (core.jobPurpose==UnitJobPurpose::None || core.jobPurpose==UnitJobPurpose::Transport))
			|| (core.activity==ACT_FILLING && core.jobPurpose!=UnitJobPurpose::Transport)
			|| ((core.activity==ACT_RANDOM || core.activity==ACT_UPGRADING) && core.jobPurpose!=UnitJobPurpose::None))
			throw std::runtime_error("Saved unit assignment does not match its activity");
		detail.regenerationRemainder=stream->readUint8("regenerationRemainder");
		detail.widePrimaryCargo=readState("widePrimaryCargo",1)!=0;
		const unsigned count=stream->readUint32("cargoOverflowCount");
		if (count>unsigned(std::max(0,runtimeTraits().cargoCapacity-1+(detail.widePrimaryCargo?1:0))) || (count && core.carriedMaterial<0) || (detail.widePrimaryCargo && !count))
			throw std::runtime_error("Invalid unit cargo count");
		const Uint64 packets=Uint64(count)+(core.carriedMaterial>=0?1:0)-(detail.widePrimaryCargo?1:0);
		if (packets>unsigned(runtimeTraits().cargoCapacity) || (core.carriedMaterial>=0 && runtimeTraits().cargoKinds==0))
			throw std::runtime_error("Saved unit inventory exceeds its definition");
		for (unsigned i=0;i<count;++i) {
			stream->readEnterSection(i);
			const int material=stream->readSint32("material");
			const auto read64=[&](const char* name) { stream->readEnterSection(name); const Uint64 hi=stream->readUint32("high"),lo=stream->readUint32("low"); stream->readLeaveSection(); return (hi<<32)|lo; };
			const WideMaterialPacket packet{read64("numerator"),read64("denominator")};
			stream->readLeaveSection();
			// The first wide row is the already-counted primary packet, not an
			// additional pickup. In the inline capacity-one path its material
			// marker already consumes capacity before this row is restored.
			const bool primary=detail.widePrimaryCargo && i==0;
			if (material<0 || material>=int(MaterialCount) || !packet.numerator || !packet.denominator || packet.numerator>packet.denominator || (!primary && !canCarryMaterial(material)) || (primary && material!=core.carriedMaterial))
				throw std::runtime_error("Invalid unit cargo packet");
			owner->game->unitCargo.overflow(core.gid).push_back({material,packet});
		}
	} else if (core.activity==ACT_FILLING) core.jobPurpose=UnitJobPurpose::Transport;
	else if (core.activity==ACT_FLAG) core.jobPurpose=hasCapability(UnitRuntimeTraits::Clear)?UnitJobPurpose::Clear:
		hasCapability(UnitRuntimeTraits::Explore)?UnitJobPurpose::Explore:UnitJobPurpose::Defend;

	// Old replay headers were originally loaded with a reset idle timer.
	// Keep that execution contract; new saves retain the timer read above.
	if (versionMinor < FILE_FORMAT_VERSION_SIMULATION_CONTINUATION)
		detail.jobTimer = 0;

	// gui
	core.levelUpAnimation = 0;
	core.magicActionAnimation = 0;

	verbose = false;

	stream->readLeaveSection();
}

void Unit::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Unit");

	// unit specification
	// we drop the unittype pointer, we save only the number
	stream->writeSint32(core.typeNum, "typeNum");

	// identity
	stream->writeUint16(core.gid, "gid");
	stream->writeUint32(core.scriptIdentity, "scriptIdentity");
	saveEntityRandom(stream, detail.entityRandom);
	stream->writeSint32(core.isDead, "isDead");
	stream->writeSint32(diagnosticDeathCause, "diagnosticDeathCause");

	// position
	stream->writeSint32(core.posX, "posX");
	stream->writeSint32(core.posY, "posY");
	stream->writeSint32(core.delta, "delta");
	stream->writeSint32(core.dx, "dx");
	stream->writeSint32(core.dy, "dy");
	stream->writeSint32(core.direction, "direction");
	stream->writeSint32(detail.insideTimeout, "insideTimeout");
	stream->writeSint32(core.speed, "speed");
	stream->writeSint32(detail.terrainHealthRemainder, "terrainHealthRemainder");
	stream->writeUint32(detail.areaLastPulseTick,"areaLastPulseTick");
	for (int i=0;i<3;++i) stream->writeUint16(detail.areaServiceRemainders[i],("areaServiceRemainder"+std::to_string(i)).c_str());

	// states
	stream->writeUint32((Uint32)detail.needToRecheckMedical, "needToRecheckMedical");
	stream->writeUint32((Uint32)core.medical, "medical");
	stream->writeUint32((Uint32)core.activity, "activity");
	stream->writeUint32((Uint32)core.displacement, "displacement");
	stream->writeUint32(detail.serviceResourcesReserved, "serviceResourcesReserved");
	stream->writeUint32((Uint32)core.movement, "movement");
	stream->writeUint32((Uint32)core.action, "action");
	stream->writeSint32(core.targetX, "targetX");
	stream->writeSint32(core.targetY, "targetY");
	stream->writeSint32(core.validTarget, "validTarget");
	stream->writeSint32(detail.magicActionTimeout, "magicActionTimeout");

	// attack timer
	stream->writeUint8(core.underAttackTimer, "underAttackTimer");

	// trigger parameters
	stream->writeSint32(core.hp, "hp");
	stream->writeSint32(detail.trigHP, "trigHP");

	// hungry
	stream->writeSint32(core.hungry, "hungry");
	stream->writeSint32(detail.hungriness, "hungryness");
	stream->writeSint32(detail.trigHungry, "trigHungry");
	stream->writeUint32(detail.fruitMask, "fruitMask");
	stream->writeUint32(detail.fruitCount, "fruitCount");

	// quality parameters
	stream->writeEnterSection("abilities");
	for (int i=0; i<NB_ABILITY; i++)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(performance(i), "performance");
		stream->writeUint32(detail.level[i], "level");
		stream->writeUint32((Uint32)detail.canLearn[i], "canLearn");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeSint32(core.constructionLevel, "constructionLevel");
	stream->writeSint32(detail.experience, "experience");
	stream->writeSint32(core.experienceLevel, "experienceLevel");

	stream->writeSint32(core.destinationPurpose, "destinationPurpose");
	stream->writeSint32(core.carriedMaterial, "carriedRessource");
	stream->writeUint32(detail.carriedPacket.numerator,"carriedNumerator");
	stream->writeUint32(detail.carriedPacket.denominator,"carriedDenominator");
	stream->writeSint32(detail.jobTimer, "jobTimer");
	stream->writeUint8(previousClearingArea.has_value(), "hasClearingClaim");
	if (previousClearingArea)
	{
		stream->writeUint32(previousClearingArea->x, "clearingClaimX");
		stream->writeUint32(previousClearingArea->y, "clearingClaimY");
	}
	stream->writeUint32(previousClearingAreaDistance, "clearingClaimDistance");

	stream->writeUint8(Uint8(core.jobPurpose),"jobPurpose");
	stream->writeUint8(detail.regenerationRemainder,"regenerationRemainder");
	stream->writeUint32(detail.widePrimaryCargo,"widePrimaryCargo");
	const auto* extra=owner->game->unitCargo.find(core.gid);
	stream->writeUint32(extra?extra->size():0,"cargoOverflowCount");
	if (extra) for (unsigned i=0;i<extra->size();++i) {
		stream->writeEnterSection(i);
		const auto& entry=(*extra)[i];
		stream->writeSint32(entry.material,"material");
		const auto write64=[&](const char* name,Uint64 value) { stream->writeEnterSection(name); stream->writeUint32(value>>32,"high"); stream->writeUint32(value,"low"); stream->writeLeaveSection(); };
		write64("numerator",entry.packet.numerator); write64("denominator",entry.packet.denominator);
		stream->writeLeaveSection();
	}


	stream->writeLeaveSection();
}

void Unit::loadCrossRef(GAGCore::InputStream *stream, Team *owner, Sint32 versionMinor)
{
	stream->readEnterSection("Unit");
	auto readBuilding = [&](const char* name) -> Building* {
		const Uint16 gid = stream->readUint16(name);
		if (gid == NOGBID) return nullptr;
		if (gid >= Building::MAX_COUNT * Team::MAX_COUNT || Building::GIDtoTeam(gid) != owner->teamNumber ||
			!owner->myBuildings[Building::GIDtoID(gid)])
			throw std::runtime_error("Invalid unit building reference");
		return owner->myBuildings[Building::GIDtoID(gid)];
	};
	attachedBuilding = readBuilding("attachedBuilding");
	targetBuilding = readBuilding("targetBuilding");
	ownExchangeBuilding = readBuilding("ownExchangeBuilding");

	stream->readLeaveSection();
}

void Unit::saveCrossRef(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Unit");

	if (attachedBuilding)
		stream->writeUint16(attachedBuilding->gid, "attachedBuilding");
	else
		stream->writeUint16(NOGBID, "attachedBuilding");

	if (targetBuilding)
		stream->writeUint16(targetBuilding->gid, "targetBuilding");
	else
		stream->writeUint16(NOGBID, "targetBuilding");

	if (ownExchangeBuilding)
		stream->writeUint16(ownExchangeBuilding->gid, "ownExchangeBuilding");
	else
		stream->writeUint16(NOGBID, "ownExchangeBuilding");

	stream->writeLeaveSection();
}

bool Unit::integrity()
{
	checkInvariant(core.gid<32768);
	if (core.isDead)
		return true;

	if (!detail.needToRecheckMedical)
	{
		checkInvariant(core.activity==ACT_UPGRADING);
		checkInvariant(core.destinationPurpose==HEAL || core.destinationPurpose==FEED);
	}
	return true;
}

Uint32 Unit::checkSum(std::vector<Uint32> *checkSumsVector, bool legacy152)
{
	Uint32 cs=(detail.serviceResourcesReserved ? 0x73657276u : 0) ^ (Uint32(core.constructionLevel) << 20);

	if(detail.areaLastPulseTick!=Uint32(-1)) cs ^= detail.areaLastPulseTick ^ 0x70756c73u;
	cs ^= Uint32(detail.areaServiceRemainders[0]) | (Uint32(detail.areaServiceRemainders[1])<<8) | (Uint32(detail.areaServiceRemainders[2])<<16);
	cs^=core.typeNum;
	if (checkSumsVector)
		checkSumsVector->push_back(core.typeNum);// [0]
	cs=rotl1(cs);

	cs^=core.isDead;
	if (checkSumsVector)
		checkSumsVector->push_back(core.isDead);// [1]
	cs=rotl1(cs);
	cs^=core.gid;
	if (checkSumsVector)
		checkSumsVector->push_back(core.gid);// [2]
	cs=rotl1(cs);

	cs^=core.posX;
	if (checkSumsVector)
		checkSumsVector->push_back(core.posX);// [3]
	cs=rotl1(cs);
	cs^=core.posY;
	if (checkSumsVector)
		checkSumsVector->push_back(core.posY);// [4]
	cs=rotl1(cs);
	cs^=core.delta;
	if (checkSumsVector)
		checkSumsVector->push_back(core.delta);// [5]
	cs=rotl1(cs);
	cs^=core.dx;
	if (checkSumsVector)
		checkSumsVector->push_back(core.dx);// [6]
	cs^=core.dy;
	if (checkSumsVector)
		checkSumsVector->push_back(core.dy);// [7]
	cs^=core.direction;
	if (checkSumsVector)
		checkSumsVector->push_back(core.direction);// [8]
	cs=rotl1(cs);
	cs^=detail.insideTimeout;
	if (checkSumsVector)
		checkSumsVector->push_back(detail.insideTimeout);// [9]
	cs=rotl1(cs);
	cs^=core.speed;
	cs ^= static_cast<Uint32>(detail.terrainHealthRemainder) * 0x9e3779b9u;
	if (checkSumsVector)
		checkSumsVector->push_back(core.speed);// [10]
	cs=rotl1(cs);

	cs^=(int)detail.needToRecheckMedical;
	if (checkSumsVector)
		checkSumsVector->push_back(detail.needToRecheckMedical);// [11]
	cs=rotl1(cs);
	cs^=core.medical;
	if (checkSumsVector)
		checkSumsVector->push_back(core.medical);// [12]
	cs^=core.activity;
	if (checkSumsVector)
		checkSumsVector->push_back(core.activity);// [13]
	cs^=core.displacement;
	if (checkSumsVector)
		checkSumsVector->push_back(core.displacement);// [14]
	cs^=core.movement;
	if (checkSumsVector)
		checkSumsVector->push_back(core.movement);// [15]
	cs^=core.action;
	if (checkSumsVector)
		checkSumsVector->push_back(core.action);// [16]
	cs=rotl1(cs);
	cs^=core.targetX;
	if (checkSumsVector)
		checkSumsVector->push_back(core.targetX);// [17]
	cs^=core.targetY;
	if (checkSumsVector)
		checkSumsVector->push_back(core.targetY);// [18]
	cs=rotl1(cs);

	cs^=core.hp;
	if (checkSumsVector)
		checkSumsVector->push_back(core.hp);// [19]
	cs^=detail.trigHP;
	if (checkSumsVector)
		checkSumsVector->push_back(detail.trigHP);// [20]
	cs=rotl1(cs);

	cs^=core.hungry;
	if (checkSumsVector)
		checkSumsVector->push_back(core.hungry);// [21]
	cs^=detail.trigHungry;
	if (checkSumsVector)
		checkSumsVector->push_back(detail.trigHungry);// [22]
	cs^=detail.trigHungryCarrying;
	if (checkSumsVector)
		checkSumsVector->push_back(detail.trigHungryCarrying);// [23]
	cs=rotl1(cs);

	cs^=detail.fruitMask;
	if (checkSumsVector)
		checkSumsVector->push_back(detail.fruitMask);// [24]
	cs^=detail.fruitCount;
	if (checkSumsVector)
		checkSumsVector->push_back(detail.fruitCount);// [25]
	cs=rotl1(cs);

	for (int i=0; i<NB_ABILITY; i++)
	{
		cs^=performance(i);
		cs=rotl1(cs);
		cs^=detail.level[i];
		cs=rotl1(cs);
		cs^=(Uint32)detail.canLearn[i];
		cs=rotl1(cs);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs);// [26]
	cs=rotl1(cs);

	cs^=(attachedBuilding!=NULL ? 1:0);
	if (checkSumsVector)
		checkSumsVector->push_back((attachedBuilding!=NULL ? 1:0));// [27]
	cs=rotl1(cs);
	cs^=(targetBuilding!=NULL ? 1:0);
	if (checkSumsVector)
		checkSumsVector->push_back((targetBuilding!=NULL ? 1:0));// [28]
	cs^=(ownExchangeBuilding!=NULL ? 2:0);
	if (checkSumsVector)
		checkSumsVector->push_back((ownExchangeBuilding!=NULL ? 1:0));// [29]
	cs=rotl1(cs);

	cs^=core.destinationPurpose;
	if (checkSumsVector)
		checkSumsVector->push_back(core.destinationPurpose);// [31]
	cs^=core.carriedMaterial;
	cs=rotl1(cs); cs^=detail.carriedPacket.numerator;
	cs=rotl1(cs); cs^=detail.carriedPacket.denominator;
	if (checkSumsVector)
		checkSumsVector->push_back(core.carriedMaterial);// [33]

	if (checkSumsVector)
		checkSumsVector->push_back(0);// [34]
	if (checkSumsVector)
		checkSumsVector->push_back(0);// [35]
	if (checkSumsVector)
		checkSumsVector->push_back(0);// [36]
	if (checkSumsVector)
		checkSumsVector->push_back(0);// [37]
	if (checkSumsVector)
		checkSumsVector->push_back(0);// [38]
	if (checkSumsVector)
		checkSumsVector->push_back(0);// [39]

	if (checkSumsVector)
	{
		const auto random = detail.entityRandom.exportState();
		checkSumsVector->push_back(Uint32(random.value >> 32));
		checkSumsVector->push_back(Uint32(random.value));
		checkSumsVector->push_back(Uint32(random.increment >> 32));
		checkSumsVector->push_back(Uint32(random.increment));
	}

	if (!legacy152) {
		const auto append=[&](Uint32 value) { cs=rotl1(cs)^value; if (checkSumsVector) checkSumsVector->push_back(value); };
		append(Uint8(core.jobPurpose)|(Uint32(detail.regenerationRemainder)<<8)|(Uint32(detail.widePrimaryCargo)<<16));
		append(detail.configuredFoodCapacity); append(core.capabilityFlags);
		if (hasCapability(UnitRuntimeTraits::ExtendedCargo) || detail.widePrimaryCargo) {
			const auto* extra=owner->game->unitCargo.find(core.gid); append(extra?extra->size():0);
			if (extra) for (const auto& entry:*extra) {
				append(entry.material); append(entry.packet.numerator>>32); append(entry.packet.numerator);
				append(entry.packet.denominator>>32); append(entry.packet.denominator);
			}
		}
	}
	cs ^= detail.entityRandom.checksum();
	return cs;
}
