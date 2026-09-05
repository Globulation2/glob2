// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Unit.h"
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
	typeNum = stream->readSint32("typeNum");
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
	this->owner = owner;
	isDead = stream->readSint32("isDead");

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
	medical = (Medical)stream->readUint32("medical");
	activity = (Activity)stream->readUint32("activity");
	displacement = (Displacement)stream->readUint32("displacement");
	movement = (Movement)stream->readUint32("movement");
	action = (Abilities)stream->readUint32("action");
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
	hungryness = stream->readSint32("hungryness");
	trigHungry = stream->readSint32("trigHungry");
	trigHungryCarying = HUNGRY_MAX/UNIT_HUNGRY_TRIG_DIVISOR_CARRYING;
	fruitMask = stream->readUint32("fruitMask");
	fruitCount = stream->readUint32("fruitCount");

	// quality parameters
	stream->readEnterSection("abilities");
	for (int i=0; i<NB_ABILITY; i++)
	{
		stream->readEnterSection(i);
		performance[i] = stream->readSint32("performance");
		level[i] = stream->readSint32("level");
		canLearn[i] = (bool)stream->readUint32("canLearn");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	experience = stream->readSint32("experience");
	experienceLevel = stream->readSint32("experienceLevel");

	destinationPurpose = stream->readSint32("destinationPurpose");
	carriedRessource = stream->readSint32("carriedRessource");

	jobTimer = stream->readSint32("jobTimer");

	previousClearingArea=std::nullopt;
	previousClearingAreaDistance=0;

	// gui
	levelUpAnimation = 0;
	magicActionAnimation = 0;
	jobTimer = 0;

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
	stream->writeSint32(isDead, "isDead");

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
	stream->writeSint32(hungryness, "hungryness");
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
	stream->writeSint32(carriedRessource, "carriedRessource");
	stream->writeSint32(jobTimer, "jobTimer");


	stream->writeLeaveSection();
}

void Unit::loadCrossRef(GAGCore::InputStream *stream, Team *owner, Sint32 versionMinor)
{
	stream->readEnterSection("Unit");
	Uint16 gbid;

	gbid = stream->readUint16("attachedBuilding");
	if (gbid == NOGBID)
		attachedBuilding = NULL;
	else
		attachedBuilding = owner->myBuildings[Building::GIDtoID(gbid)];

	gbid = stream->readUint16("targetBuilding");
	if (gbid == NOGBID)
		targetBuilding = NULL;
	else
		targetBuilding = owner->myBuildings[Building::GIDtoID(gbid)];

	gbid = stream->readUint16("ownExchangeBuilding");
	if (gbid == NOGBID)
		ownExchangeBuilding = NULL;
	else
		ownExchangeBuilding = owner->myBuildings[Building::GIDtoID(gbid)];

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
	cs^=trigHungryCarying;
	if (checkSumsVector)
		checkSumsVector->push_back(trigHungryCarying);// [23]
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
	cs^=carriedRessource;
	if (checkSumsVector)
		checkSumsVector->push_back(carriedRessource);// [33]

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
