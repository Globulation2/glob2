// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BuildingType.h"
#include "FileFormatVersions.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"

bool Team::load(GAGCore::InputStream *stream, BuildingsTypes *buildingstypes, Sint32 versionMinor)
{
	assert(stream);
	assert(buildingsToBeDestroyed.size()==0);
	buildingsTryToBuildingSiteRoom.clear();

	// loading baseteam
	if(!BaseTeam::load(stream, versionMinor))
		return false;

	stream->readEnterSection("Team");

	// normal load
	stream->readEnterSection("myUnits");
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		if (myUnits[i])
			delete myUnits[i];

		stream->readEnterSection(i);
		Uint32 isUsed = stream->readUint32("isUsed");
		if (isUsed)
			myUnits[i] = new Unit(stream, this, versionMinor);
		else
			myUnits[i] = NULL;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	swarms.clear();
	turrets.clear();
	canExchange.clear();
	virtualBuildings.clear();
	clearingFlags.clear();

	prestige = 0;
	stream->readEnterSection("myBuildings");
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		if (myBuildings[i])
			delete myBuildings[i];

		stream->readEnterSection(i);
		Uint32 isUsed = stream->readUint32("isUsed");
		if (isUsed)
		{
			myBuildings[i] = new Building(stream, buildingstypes, this, versionMinor);
			if (myBuildings[i]->type->unitProductionTime)
				swarms.push_back(myBuildings[i]);
			if (myBuildings[i]->type->shootingRange)
				turrets.push_back(myBuildings[i]);
			if (myBuildings[i]->type->canExchange)
				canExchange.push_back(myBuildings[i]);
			if (myBuildings[i]->type->isVirtual)
				virtualBuildings.push_back(myBuildings[i]);
			if (myBuildings[i]->type->zonable[WORKER])
				clearingFlags.push_back(myBuildings[i]);
		}
		else
			myBuildings[i] = NULL;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	// resolve cross reference
	stream->readEnterSection("myUnits");
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		if (myUnits[i])
		{
			stream->readEnterSection(i);
			myUnits[i]->loadCrossRef(stream, this, versionMinor);
			stream->readLeaveSection();
		}
	}
	stream->readLeaveSection();

	stream->readEnterSection("myBuildings");
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		if (myBuildings[i])
		{
			stream->readEnterSection(i);
			myBuildings[i]->loadCrossRef(stream, buildingstypes, this, versionMinor);
			if (myBuildings[i]->type->canExchange)
				canExchange.push_back(myBuildings[i]);
			stream->readLeaveSection();
		}
	}
	stream->readLeaveSection();

	allies = stream->readUint32("allies");
	enemies = stream->readUint32("enemies");
	sharedVisionExchange = stream->readUint32("sharedVisionExchange");
	sharedVisionFood = stream->readUint32("sharedVisionFood");
	sharedVisionOther = stream->readUint32("sharedVisionOther");
	me = stream->readUint32("me");
	startPosX = stream->readSint32("startPosX");
	startPosY = stream->readSint32("startPosY");
	startPosSet = stream->readSint32("startPosSet");
	unitConversionLost = stream->readSint32("unitConversionLost");
	unitConversionGained = stream->readSint32("unitConversionGained");

	stream->readEnterSection("teamRessources");
	for (unsigned int i=0; i<MAX_NB_RESSOURCES; ++i)
	{
		stream->readEnterSection(i);
		teamRessources[i] = stream->readUint32("teamRessources");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	for(int i=0; i<GESize; ++i)
		eventCooldownTimers[i]=0;

	if (!stats.load(stream, versionMinor))
	{
		stream->readLeaveSection();
		return false;
	}
	stats.step(this, true);

	if(versionMinor >= FILE_FORMAT_VERSION_RACE_FIELD)
	{
		if(!race.load(stream, versionMinor))
		{
			stream->readLeaveSection();
			return false;
		}
	}
	else
	{
		race.load();
	}

	isAlive = true;

	stream->readLeaveSection();
	return true;
}




void Team::save(GAGCore::OutputStream *stream)
{
	// saving baseteam
	BaseTeam::save(stream);

	stream->writeEnterSection("Team");

	// saving team
	stream->writeEnterSection("myUnits");
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		stream->writeEnterSection(i);
		if (myUnits[i])
		{
			stream->writeUint32(true, "isUsed");
			myUnits[i]->save(stream);
		}
		else
		{
			stream->writeUint32(false, "isUsed");
		}
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("myBuildings");
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		stream->writeEnterSection(i);
		if (myBuildings[i])
		{
			stream->writeUint32(true, "isUsed");
			myBuildings[i]->save(stream);
		}
		else
		{
			stream->writeUint32(false, "isUsed");
		}
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	// save cross reference
	stream->writeEnterSection("myUnits");
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		if (myUnits[i])
		{
			stream->writeEnterSection(i);
			myUnits[i]->saveCrossRef(stream);
			stream->writeLeaveSection();
		}
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("myBuildings");
	for (int i=0; i<Building::MAX_COUNT; i++)
	{
		if (myBuildings[i])
		{
			stream->writeEnterSection(i);
			myBuildings[i]->saveCrossRef(stream);
			stream->writeLeaveSection();
		}
	}
	stream->writeLeaveSection();

	stream->writeUint32(allies, "allies");
	stream->writeUint32(enemies, "enemies");
	stream->writeUint32(sharedVisionOther, "sharedVisionExchange");
	stream->writeUint32(sharedVisionFood, "sharedVisionFood");
	stream->writeUint32(sharedVisionOther, "sharedVisionOther");
	stream->writeUint32(me, "me");
	stream->writeSint32(startPosX, "startPosX");
	stream->writeSint32(startPosY, "startPosY");
	stream->writeSint32(startPosSet, "startPosSet");
	stream->writeSint32(unitConversionLost, "unitConversionLost");
	stream->writeSint32(unitConversionGained, "unitConversionGained");

	stream->writeEnterSection("teamRessources");
	for (unsigned int i=0; i<MAX_NB_RESSOURCES; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(teamRessources[i], "teamRessources");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stats.save(stream);
	race.save(stream);

	stream->writeLeaveSection();
}




Uint32 Team::checkSum(std::vector<Uint32> *checkSumsVector, std::vector<Uint32> *checkSumsVectorForBuildings, std::vector<Uint32> *checkSumsVectorForUnits)
{
	Uint32 cs=0;

	cs^=BaseTeam::checkSum();
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [1+t*20]

	for (int i=0; i<Unit::MAX_COUNT; i++)
		if (myUnits[i])
	{
		cs^=myUnits[i]->checkSum(checkSumsVectorForUnits);
		cs=rotr1(cs);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [2+t*20]

	for (int i=0; i<Building::MAX_COUNT; i++)
		if (myBuildings[i])
	{
		cs^=myBuildings[i]->checkSum(checkSumsVectorForBuildings);
		cs=rotr1(cs);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [3+t*20]

	for (int i=0; i<NB_ABILITY; i++)
	{
		cs^=upgrade[i].size();
		cs=rotr1(cs);
	}
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [4+t*20]

	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [7+t*20]

	cs^=canExchange.size();
	cs^=canFeedUnit.size();
	cs^=canHealUnit.size();
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [8+t*20]

	cs^=buildingsToBeDestroyed.size();
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [9+t*20]
	cs^=buildingsTryToBuildingSiteRoom.size();
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [10+t*20]

	cs^=swarms.size();
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [11+t*20]
	cs^=turrets.size();
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [12+t*20]

	cs^=allies;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [13+t*20]
	cs^=enemies;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [14+t*20]
	cs^=sharedVisionExchange;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [15+t*20]
	cs^=sharedVisionFood;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [16+t*20]
	cs^=sharedVisionOther;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [17+t*20]
	cs^=me;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [18+t*20]

	cs^=noMoreBuildingSitesCountdown;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [19+t*20]

	cs^=prestige;
	cs=rotr1(cs);
	if (checkSumsVector)
		checkSumsVector->push_back(cs); // [20+t*20]

	return cs;
}
