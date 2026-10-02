// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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
#include <Stream.h>

void Unit::load(GAGCore::InputStream *stream, Team *owner, Sint32 versionMinor)
{
	stream->readEnterSection("Unit");

	// unit specification
	// File-controlled types and identities reach fixed tables later. Validate
	// before indexing or converting raw integers into simulation enums.
	typeNum = stream->readSint32("typeNum");
	if (typeNum < 0 || typeNum >= NB_UNIT_TYPE) throw std::runtime_error("Invalid unit type");
	if (versionMinor < FILE_FORMAT_VERSION_DROP_UNIT_SKIN_NAME)
	{
		// Pre-v84 saves carried a per-unit skinName string; skin is now derived
		// from typeNum at render time, so read and discard for compatibility.
		stream->readText("skinName");
	}
	race = &(owner->race);
	assert(race);

	// identity
	gid = stream->readUint16("gid");
	if (gid >= MAX_COUNT * Team::MAX_COUNT || GIDtoTeam(gid) != owner->teamNumber)
		throw std::runtime_error("Invalid unit identity");
	scriptIdentity=versionMinor >= FILE_FORMAT_VERSION_JAVASCRIPT ? stream->readUint32("scriptIdentity") : owner->game->allocateScriptIdentity(false,gid);
	this->owner = owner;
	isDead = stream->readSint32("isDead");
	diagnosticDeathCause = GameplayMeasurements::UNKNOWN;
	if (versionMinor >= FILE_FORMAT_VERSION_GAMEPLAY_STATS)
	{
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		diagnosticDeathCause = stream->readSint32("diagnosticDeathCause");
		if (diagnosticDeathCause < 0 || diagnosticDeathCause >= GameplayMeasurements::DEATH_CAUSES)
			throw std::runtime_error("Invalid diagnostic death cause");
	}

	// position
	posX = stream->readSint32("posX");
	posY = stream->readSint32("posY");
	delta = stream->readSint32("delta");
	dx = stream->readSint32("dx");
	dy = stream->readSint32("dy");
	direction = stream->readSint32("direction");
	insideTimeout = stream->readSint32("insideTimeout");
	speed = stream->readSint32("speed");

	// states
	needToRecheckMedical = (bool)stream->readUint32("needToRecheckMedical");
	auto readState = [&](const char* name, Uint32 maximum) {
		const Uint32 value = stream->readUint32(name);
		if (value > maximum) throw std::runtime_error("Invalid unit action state");
		return value;
	};
	medical = static_cast<Medical>(readState("medical", MED_DAMAGED));
	activity = static_cast<Activity>(readState("activity", ACT_UPGRADING));
	displacement = static_cast<Displacement>(readState("displacement", DIS_EXITING_BUILDING));
	movement = static_cast<Movement>(readState("movement", MOV_ATTACKING_TARGET));
	action = static_cast<Abilities>(readState("action", NB_ABILITY - 1));
	if ((displacement % 2) != 0 || movement == 10 ||
		direction < 0 || direction > UNIT_DIRECTION_NONE || dx < -1 || dx > 1 || dy < -1 || dy > 1)
		throw std::runtime_error("Invalid unit action state");
	targetX = (Sint32)stream->readSint32("targetX");
	targetY = (Sint32)stream->readSint32("targetY");
	validTarget = (bool)stream->readSint32("validTarget");
	magicActionTimeout = stream->readSint32("magicActionTimeout");

	// under attack timer
	if(versionMinor >= FILE_FORMAT_VERSION_UNDER_ATTACK_TIMER)
		underAttackTimer = stream->readUint8("underAttackTimer");
	else
		underAttackTimer = 0;


	// trigger parameters
	hp = stream->readSint32("hp");
	trigHP = stream->readSint32("trigHP");

	// hungry
	hungry = stream->readSint32("hungry");
	hungriness = stream->readSint32("hungryness");
	trigHungry = stream->readSint32("trigHungry");
	trigHungryCarrying = HUNGRY_MAX/UNIT_HUNGRY_TRIG_DIVISOR_CARRYING;
	fruitMask = stream->readUint32("fruitMask");
	fruitCount = stream->readUint32("fruitCount");

	// quality parameters
	stream->readEnterSection("abilities");
	for (int i=0; i<NB_ABILITY; i++)
	{
		stream->readEnterSection(i);
		performance[i] = stream->readSint32("performance");
		level[i] = stream->readSint32("level");
		if (level[i] < 0 || level[i] >= NB_UNIT_LEVELS) throw std::runtime_error("Invalid unit level");
		canLearn[i] = (bool)stream->readUint32("canLearn");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	// Harvest and build are one worker level. A save from before that could
	// hold the two apart (the editor used to offer a box for each), so even
	// such a worker out to the higher of the two.
	if (versionMinor < FILE_FORMAT_VERSION_ONE_WORKER_LEVEL
		&& canLearn[BUILD] && level[HARVEST] != level[BUILD])
		setWorkerLevel(std::max(level[HARVEST], level[BUILD]));


	experience = stream->readSint32("experience");
	experienceLevel = stream->readSint32("experienceLevel");

	destinationPurpose = stream->readSint32("destinationPurpose");
	carriedResource = stream->readSint32("carriedRessource");
	if (carriedResource < -1 || carriedResource >= MAX_RESOURCES || destinationPurpose < -1 || destinationPurpose > FEED)
		throw std::runtime_error("Invalid unit resource or destination");
	if ((activity == ACT_FILLING && (destinationPurpose < 0 || destinationPurpose >= MAX_RESOURCES)) ||
		(activity == ACT_UPGRADING && destinationPurpose < 0))
		throw std::runtime_error("Invalid unit activity destination");

	jobTimer = stream->readSint32("jobTimer");

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

	// Old replay headers were originally loaded with a reset idle timer.
	// Keep that execution contract; new saves retain the timer read above.
	if (versionMinor < FILE_FORMAT_VERSION_SIMULATION_CONTINUATION)
		jobTimer = 0;

	// gui
	levelUpAnimation = 0;
	magicActionAnimation = 0;

	verbose = false;

	stream->readLeaveSection();
}

void Unit::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Unit");

	// unit specification
	// we drop the unittype pointer, we save only the number
	stream->writeSint32(typeNum, "typeNum");

	// identity
	stream->writeUint16(gid, "gid");
	stream->writeUint32(scriptIdentity, "scriptIdentity");
	stream->writeSint32(isDead, "isDead");
	stream->writeSint32(diagnosticDeathCause, "diagnosticDeathCause");

	// position
	stream->writeSint32(posX, "posX");
	stream->writeSint32(posY, "posY");
	stream->writeSint32(delta, "delta");
	stream->writeSint32(dx, "dx");
	stream->writeSint32(dy, "dy");
	stream->writeSint32(direction, "direction");
	stream->writeSint32(insideTimeout, "insideTimeout");
	stream->writeSint32(speed, "speed");

	// states
	stream->writeUint32((Uint32)needToRecheckMedical, "needToRecheckMedical");
	stream->writeUint32((Uint32)medical, "medical");
	stream->writeUint32((Uint32)activity, "activity");
	stream->writeUint32((Uint32)displacement, "displacement");
	stream->writeUint32((Uint32)movement, "movement");
	stream->writeUint32((Uint32)action, "action");
	stream->writeSint32(targetX, "targetX");
	stream->writeSint32(targetY, "targetY");
	stream->writeSint32(validTarget, "validTarget");
	stream->writeSint32(magicActionTimeout, "magicActionTimeout");

	// attack timer
	stream->writeUint8(underAttackTimer, "underAttackTimer");

	// trigger parameters
	stream->writeSint32(hp, "hp");
	stream->writeSint32(trigHP, "trigHP");

	// hungry
	stream->writeSint32(hungry, "hungry");
	stream->writeSint32(hungriness, "hungryness");
	stream->writeSint32(trigHungry, "trigHungry");
	stream->writeUint32(fruitMask, "fruitMask");
	stream->writeUint32(fruitCount, "fruitCount");

	// quality parameters
	stream->writeEnterSection("abilities");
	for (int i=0; i<NB_ABILITY; i++)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(performance[i], "performance");
		stream->writeUint32(level[i], "level");
		stream->writeUint32((Uint32)canLearn[i], "canLearn");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeSint32(experience, "experience");
	stream->writeSint32(experienceLevel, "experienceLevel");

	stream->writeSint32(destinationPurpose, "destinationPurpose");
	stream->writeSint32(carriedResource, "carriedRessource");
	stream->writeSint32(jobTimer, "jobTimer");
	stream->writeUint8(previousClearingArea.has_value(), "hasClearingClaim");
	if (previousClearingArea)
	{
		stream->writeUint32(previousClearingArea->x, "clearingClaimX");
		stream->writeUint32(previousClearingArea->y, "clearingClaimY");
	}
	stream->writeUint32(previousClearingAreaDistance, "clearingClaimDistance");


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
	checkInvariant(gid<32768);
	if (isDead)
		return true;

	if (!needToRecheckMedical)
	{
		checkInvariant(activity==ACT_UPGRADING);
		checkInvariant(destinationPurpose==HEAL || destinationPurpose==FEED);
	}
	return true;
}

Uint32 Unit::checkSum(std::vector<Uint32> *checkSumsVector)
{
	Uint32 cs=0;

	cs^=typeNum;
	if (checkSumsVector)
		checkSumsVector->push_back(typeNum);// [0]
	cs=rotl1(cs);

	cs^=isDead;
	if (checkSumsVector)
		checkSumsVector->push_back(isDead);// [1]
	cs=rotl1(cs);
	cs^=gid;
	if (checkSumsVector)
		checkSumsVector->push_back(gid);// [2]
	cs=rotl1(cs);

	cs^=posX;
	if (checkSumsVector)
		checkSumsVector->push_back(posX);// [3]
	cs=rotl1(cs);
	cs^=posY;
	if (checkSumsVector)
		checkSumsVector->push_back(posY);// [4]
	cs=rotl1(cs);
	cs^=delta;
	if (checkSumsVector)
		checkSumsVector->push_back(delta);// [5]
	cs=rotl1(cs);
	cs^=dx;
	if (checkSumsVector)
		checkSumsVector->push_back(dx);// [6]
	cs^=dy;
	if (checkSumsVector)
		checkSumsVector->push_back(dy);// [7]
	cs^=direction;
	if (checkSumsVector)
		checkSumsVector->push_back(direction);// [8]
	cs=rotl1(cs);
	cs^=insideTimeout;
	if (checkSumsVector)
		checkSumsVector->push_back(insideTimeout);// [9]
	cs=rotl1(cs);
	cs^=speed;
	if (checkSumsVector)
		checkSumsVector->push_back(speed);// [10]
	cs=rotl1(cs);

	cs^=(int)needToRecheckMedical;
	if (checkSumsVector)
		checkSumsVector->push_back(needToRecheckMedical);// [11]
	cs=rotl1(cs);
	cs^=medical;
	if (checkSumsVector)
		checkSumsVector->push_back(medical);// [12]
	cs^=activity;
	if (checkSumsVector)
		checkSumsVector->push_back(activity);// [13]
	cs^=displacement;
	if (checkSumsVector)
		checkSumsVector->push_back(displacement);// [14]
	cs^=movement;
	if (checkSumsVector)
		checkSumsVector->push_back(movement);// [15]
	cs^=action;
	if (checkSumsVector)
		checkSumsVector->push_back(action);// [16]
	cs=rotl1(cs);
	cs^=targetX;
	if (checkSumsVector)
		checkSumsVector->push_back(targetX);// [17]
	cs^=targetY;
	if (checkSumsVector)
		checkSumsVector->push_back(targetY);// [18]
	cs=rotl1(cs);

	cs^=hp;
	if (checkSumsVector)
		checkSumsVector->push_back(hp);// [19]
	cs^=trigHP;
	if (checkSumsVector)
		checkSumsVector->push_back(trigHP);// [20]
	cs=rotl1(cs);

	cs^=hungry;
	if (checkSumsVector)
		checkSumsVector->push_back(hungry);// [21]
	cs^=trigHungry;
	if (checkSumsVector)
		checkSumsVector->push_back(trigHungry);// [22]
	cs^=trigHungryCarrying;
	if (checkSumsVector)
		checkSumsVector->push_back(trigHungryCarrying);// [23]
	cs=rotl1(cs);

	cs^=fruitMask;
	if (checkSumsVector)
		checkSumsVector->push_back(fruitMask);// [24]
	cs^=fruitCount;
	if (checkSumsVector)
		checkSumsVector->push_back(fruitCount);// [25]
	cs=rotl1(cs);

	for (int i=0; i<NB_ABILITY; i++)
	{
		cs^=performance[i];
		cs=rotl1(cs);
		cs^=level[i];
		cs=rotl1(cs);
		cs^=(Uint32)canLearn[i];
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

	cs^=destinationPurpose;
	if (checkSumsVector)
		checkSumsVector->push_back(destinationPurpose);// [31]
	cs^=carriedResource;
	if (checkSumsVector)
		checkSumsVector->push_back(carriedResource);// [33]

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

	return cs;
}
