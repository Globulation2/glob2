// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BuildingType.h"
#include "FileFormatVersions.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Utilities.h"
#include <BinaryStream.h>
#include <stdexcept>

bool Team::load(GAGCore::InputStream *stream, BuildingsTypes *buildingstypes, Sint32 versionMinor)
{
    return loadTask(stream, buildingstypes, versionMinor).run();
}

GAGCore::CooperativeTask Team::loadTask(GAGCore::InputStream *stream, BuildingsTypes *buildingstypes, Sint32 versionMinor)
{
	assert(stream);
	assert(buildingsToBeDestroyed.size()==0);
	buildingsTryToBuildingSiteRoom.clear();

	// loading base team
	if(!BaseTeam::load(stream, versionMinor))
		co_return false;

	stream->readEnterSection("Team");

	// normal load
	stream->readEnterSection("myUnits");
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
        if ((i & 31) == 0) co_await GAGCore::CooperativeTask::checkpoint("[Loading units]");
		if (myUnits[i])
			delete myUnits[i];
        myUnits[i] = NULL;

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
        if ((i & 31) == 0) co_await GAGCore::CooperativeTask::checkpoint("[Loading buildings]");
		if (myBuildings[i])
			delete myBuildings[i];
        myBuildings[i] = NULL;

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
        if ((i & 31) == 0) co_await GAGCore::CooperativeTask::checkpoint("[Resolving team links]");
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
        if ((i & 31) == 0) co_await GAGCore::CooperativeTask::checkpoint("[Resolving team links]");
		if (myBuildings[i])
		{
			stream->readEnterSection(i);
			myBuildings[i]->loadCrossRef(stream, buildingstypes, this, versionMinor);
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
	for (unsigned int i=0; i<MAX_NB_RESOURCES; ++i)
	{
		stream->readEnterSection(i);
		teamResources[i] = stream->readUint32("teamRessources");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();


	for(int i=0; i<GESize; ++i)
		eventCooldownTimers[i]=0;

	if (!stats.load(stream, versionMinor))
	{
		stream->readLeaveSection();
		co_return false;
	}
	if (versionMinor < FILE_FORMAT_VERSION_LIVE_TEAM_STATS)
		stats.step(this, true);

	if(versionMinor >= FILE_FORMAT_VERSION_RACE_FIELD)
	{
		if(!race.load(stream, versionMinor))
		{
			stream->readLeaveSection();
			co_return false;
		}
	}
	else
	{
		race.load();
	}

	isAlive = true;

	if (versionMinor >= FILE_FORMAT_VERSION_SIMULATION_CONTINUATION)
	{
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		auto readBuildings = [&](auto& list, const char* name) {
			stream->readEnterSection(name);
			const Uint32 count = stream->readUint32("count");
			if (count > Building::MAX_COUNT) throw std::runtime_error("Invalid team building list size");
			list.clear();
			// Preserve multiplicity too: existing live flag lists can contain
			// repeated references, and deduplicating would change scheduling.
			for (Uint32 i=0; i<count; ++i)
			{
				stream->readEnterSection(i);
				const Uint16 gid = stream->readUint16("gid");
				if (gid >= Building::MAX_COUNT * Team::MAX_COUNT || Building::GIDtoTeam(gid) != teamNumber || !myBuildings[Building::GIDtoID(gid)])
					throw std::runtime_error(std::string("Invalid team building list reference: ")+name+" team="+std::to_string(teamNumber)+" gid="+std::to_string(gid));
				list.push_back(myBuildings[Building::GIDtoID(gid)]);
				stream->readLeaveSection();
			}
			stream->readLeaveSection();
		};
		readBuildings(canFeedUnit, "canFeedUnit");
		readBuildings(canHealUnit, "canHealUnit");
		readBuildings(canExchange, "canExchange");
		readBuildings(swarms, "swarms");
		readBuildings(turrets, "turrets");
		readBuildings(clearingFlags, "clearingFlags");
		readBuildings(virtualBuildings, "virtualBuildings");
		readBuildings(buildingsWaitingForDestruction, "buildingsWaitingForDestruction");
		readBuildings(buildingsToBeDestroyed, "buildingsToBeDestroyed");
		readBuildings(buildingsTryToBuildingSiteRoom, "buildingsTryToBuildingSiteRoom");
		for (int i=0; i<NB_ABILITY; ++i)
		{
			stream->readEnterSection(i);
			readBuildings(canUpgrade[i], "canUpgrade");
			stream->readLeaveSection();
		}
		buildingsNeedingUnits.clear();
		const Uint32 buckets = stream->readUint32("hiringBuckets");
		if (buckets > Building::MAX_COUNT) throw std::runtime_error("Invalid hiring bucket count");
		for (Uint32 i=0; i<buckets; ++i)
		{
			stream->readEnterSection(i);
			const Sint32 priority = stream->readSint32("priority");
			if (buildingsNeedingUnits.count(priority)) throw std::runtime_error("Duplicate hiring priority");
			readBuildings(buildingsNeedingUnits[priority], "buildings");
			stream->readLeaveSection();
		}
	}

	if (versionMinor >= FILE_FORMAT_VERSION_CONSTRUCTION_COOLDOWN)
	{
		noMoreBuildingSitesCountdown = stream->readSint32("noMoreBuildingSitesCountdown");
		if (noMoreBuildingSitesCountdown < 0 || noMoreBuildingSitesCountdown > noMoreBuildingSitesCountdownMax)
			throw std::runtime_error("Invalid construction cooldown");
	}

	stream->readLeaveSection();
	co_return true;
}




void Team::save(GAGCore::OutputStream *stream)
{
	// saving base team
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
	for (unsigned int i=0; i<MAX_NB_RESOURCES; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(teamResources[i], "teamRessources");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stats.save(stream);
	race.save(stream);

	auto writeBuildings = [&](const auto& list, const char* name) {
		stream->writeEnterSection(name);
		stream->writeUint32(list.size(), "count");
		unsigned i=0;
		for (const auto* building : list)
		{
			stream->writeEnterSection(i++);
			stream->writeUint16(building->gid, "gid");
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
	};
	writeBuildings(canFeedUnit, "canFeedUnit");
	writeBuildings(canHealUnit, "canHealUnit");
	writeBuildings(canExchange, "canExchange");
	writeBuildings(swarms, "swarms");
	writeBuildings(turrets, "turrets");
	writeBuildings(clearingFlags, "clearingFlags");
	writeBuildings(virtualBuildings, "virtualBuildings");
	writeBuildings(buildingsWaitingForDestruction, "buildingsWaitingForDestruction");
	writeBuildings(buildingsToBeDestroyed, "buildingsToBeDestroyed");
	writeBuildings(buildingsTryToBuildingSiteRoom, "buildingsTryToBuildingSiteRoom");
	for (int i=0; i<NB_ABILITY; ++i)
	{
		stream->writeEnterSection(i);
		writeBuildings(canUpgrade[i], "canUpgrade");
		stream->writeLeaveSection();
	}
	stream->writeUint32(buildingsNeedingUnits.size(), "hiringBuckets");
	unsigned bucket=0;
	for (const auto& entry : buildingsNeedingUnits)
	{
		stream->writeEnterSection(bucket++);
		stream->writeSint32(entry.first, "priority");
		writeBuildings(entry.second, "buildings");
		stream->writeLeaveSection();
	}

	stream->writeSint32(noMoreBuildingSitesCountdown, "noMoreBuildingSitesCountdown");

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
		cs^=canUpgrade[i].size();
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
