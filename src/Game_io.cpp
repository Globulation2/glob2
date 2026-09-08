// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <iostream>
#include <sstream>
#include <locale>
#include <stdexcept>

#include "AICastor.h"
#include "AINicowar.h"

#include <assert.h>
#include <string.h>

#include <string>
#include <algorithm>

#include <BinaryStream.h>

#include "BuildingType.h"
#include "DatasetWriter.h"
#include "FileFormatVersions.h"
#include "Game.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "Integrity.h"
#include "Utilities.h"
#include "SDLCompat.h"


#include "Brush.h"
#include "Bullet.h"
#include "FertilityCalculator.h"

#include "ReplayWriter.h"

#define BULLET_IMGID 0

// Save/load, integrity, checksum. Split out of Game.cpp.

// Pending sites have already reserved their footprint but are waiting for units
// to move. Preserve their order and staffing requests across a saved-game load.
void Game::saveBuildProjects(GAGCore::OutputStream* stream) const
{
    stream->writeEnterSection("buildProjects");
    stream->writeUint32(buildProjects.size(), "count");
    unsigned index = 0;
    for (const auto& project : buildProjects)
    {
        stream->writeEnterSection(index++);
        stream->writeSint32(project.posX, "posX");
        stream->writeSint32(project.posY, "posY");
        stream->writeSint32(project.teamNumber, "teamNumber");
        stream->writeSint32(project.typeNum, "typeNum");
        stream->writeSint32(project.unitWorking, "unitWorking");
        stream->writeSint32(project.unitWorkingFuture, "unitWorkingFuture");
        stream->writeLeaveSection();
    }
    stream->writeLeaveSection();
}

void Game::loadBuildProjects(GAGCore::InputStream* stream)
{
    stream->readEnterSection("buildProjects");
    const Uint32 count = stream->readUint32("count");
    // Bound allocation and reject corrupt references before the scheduler uses them.
    if (count > 65536) throw std::runtime_error("Invalid pending construction count");
    std::list<BuildProject> restored;
    for (Uint32 index = 0; index < count; ++index)
    {
        stream->readEnterSection(index);
        BuildProject project;
        project.posX = stream->readSint32("posX");
        project.posY = stream->readSint32("posY");
        project.teamNumber = stream->readSint32("teamNumber");
        project.typeNum = stream->readSint32("typeNum");
        project.unitWorking = stream->readSint32("unitWorking");
        project.unitWorkingFuture = stream->readSint32("unitWorkingFuture");
        if (project.posX < 0 || project.posX >= map.getW()
            || project.posY < 0 || project.posY >= map.getH()
            || project.teamNumber < 0 || project.teamNumber >= mapHeader.getNumberOfTeams()
            || project.typeNum < 0 || static_cast<size_t>(project.typeNum) >= globalContainer->buildingsTypes.size()
            || project.unitWorking < 0 || project.unitWorking > Unit::MAX_COUNT
            || project.unitWorkingFuture < 0 || project.unitWorkingFuture > Unit::MAX_COUNT)
            throw std::runtime_error("Invalid pending construction project");
        restored.push_back(project);
        stream->readLeaveSection();
    }
    stream->readLeaveSection();
    buildProjects.swap(restored);
}




namespace
{
	// RAII guard: enters a stream section on construction, leaves it on
	// destruction unless commit() was called. Lets failure paths just
	// `return false;` without remembering to call readLeaveSection().
	// Note: readLeaveSection is a no-op for BinaryStream (the format used
	// for save files); it only affects TextStream nesting.
	class ReadSectionGuard
	{
		GAGCore::InputStream *stream;
		bool committed = false;
	public:
		ReadSectionGuard(GAGCore::InputStream *s, const char *name) : stream(s)
		{
			stream->readEnterSection(name);
		}
		ReadSectionGuard(const ReadSectionGuard &) = delete;
		ReadSectionGuard &operator=(const ReadSectionGuard &) = delete;
		void commit()
		{
			stream->readLeaveSection();
			committed = true;
		}
		~ReadSectionGuard()
		{
			if (!committed)
				stream->readLeaveSection();
		}
	};

	// Read a 4-byte signature and check it equals `expected`. Signatures are
	// basic corruption tests scattered through the save format.
	bool readMatchingSignature(GAGCore::InputStream *stream,
	                           const char *expected,
	                           const char *fieldName)
	{
		char signature[FILE_SIG_LEN];
		stream->read(signature, FILE_SIG_LEN, fieldName);
		return memcmp(signature, expected, FILE_SIG_LEN) == 0;
	}

	// Note: the rotr1 helper used below now lives in Utilities.h so all
	// checksum mixers in the codebase share one definition.
}

bool Game::load(GAGCore::InputStream *stream)
{
    return loadTask(stream).run();
}

GAGCore::CooperativeTask Game::loadTask(GAGCore::InputStream *stream)
{
	assert(stream);
    co_await GAGCore::CooperativeTask::checkpoint("[Loading headers]");

	ReadSectionGuard gameSection(stream, "Game");

	///Clears any previous game
	clearGame();
	mapHeader.reset();
	gameHeader.reset();

	// We load the map header
	MapHeader tempMapHeader;
	if (verbose)
		printf("Loading map header\n");
	if (!tempMapHeader.load(stream))
		co_return false;
	mapHeader=tempMapHeader;
	Sint32 versionMinor=mapHeader.getVersionMinor();


	// We load the game header
	GameHeader tempGameHeader;
	if (verbose)
		printf("Loading game header\n");
	if (!tempGameHeader.load(stream, versionMinor))
		co_return false;
	gameHeader=tempGameHeader;

	if (!readMatchingSignature(stream, FILE_SIG_GAME_BEGIN, "signatureStart"))
		co_return false;

	///Load the step counter
	stepCounter = stream->readUint32("stepCounter");

	if(versionMinor < FILE_FORMAT_VERSION_UNIFIED_SEED)
	{
		///Load random seeds, these are no longer used
		stream->readUint32("SyncRandSeedA");
		stream->readUint32("SyncRandSeedB");
		stream->readUint32("SyncRandSeedC");

		if (!readMatchingSignature(stream, FILE_SIG_GAME_SYNC, "signatureAfterSyncRand"))
			co_return false;
	}
	else
	{
		if (!readMatchingSignature(stream, FILE_SIG_GAME_BUILT, "signatureBeforeTeams"))
			co_return false;
	}

	///Load teams
	stream->readEnterSection("teams");
	for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		stream->readEnterSection(i);
        co_await GAGCore::CooperativeTask::checkpoint("[Loading teams]");
		teams[i]=new Team(this);
        if (!(co_await teams[i]->loadTask(stream, &globalContainer->buildingsTypes, versionMinor)))
            co_return false;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	if (!readMatchingSignature(stream, FILE_SIG_GAME_TEAM, "signatureAfterTeams"))
		co_return false;

	// Load the map. Team has to be saved and loaded first.
	if(!(co_await map.loadTask(stream, mapHeader, this)))
		co_return false;

	if (!readMatchingSignature(stream, FILE_SIG_GAME_MAP, "signatureAfterMap"))
		co_return false;

	// Load the players. Both Map and Team must be loaded first.
	stream->readEnterSection("players");
	for (int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
	{
		stream->readEnterSection(i);
        co_await GAGCore::CooperativeTask::checkpoint("[Loading players]");
		players[i]=new Player();
		if (!players[i]->load(stream, teams, versionMinor)) co_return false;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	if (!readMatchingSignature(stream, FILE_SIG_GAME_PLAYER, "signatureAfterPlayers"))
		co_return false;

	// We have to finish Team's loading
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		teams[i]->update();
	}

	// Check integrity of loaded game
	if (!integrity())
		co_return false;

    co_await GAGCore::CooperativeTask::checkpoint("[Loading scripts]");
	// Now load the old map script
	if (!sgslScript.load(stream, this))
		co_return false;

	if(versionMinor >= FILE_FORMAT_VERSION_USL_MAPSCRIPT)
	{
		// This is the new map script system
		if (!mapscript.decodeData(stream, mapHeader.getVersionMinor()))
			co_return false;
	}

	///Load the campaign text for the game.
	if(versionMinor < FILE_FORMAT_VERSION_CAMPAIGN_TEXT_OBJECTIVES)
		stream->readText("campaignText");

	// default prestige calculation
	prestigeToReach = std::max(MIN_MAX_PRESTIGE, mapHeader.getNumberOfTeams()*TEAM_MAX_PRESTIGE);

	if(mapHeader.getVersionMinor() >= FILE_FORMAT_VERSION_CAMPAIGN_TEXT_OBJECTIVES)
	{
		objectives.decodeData(stream, mapHeader.getVersionMinor());
	}

	if(mapHeader.getVersionMinor() >= FILE_FORMAT_VERSION_BRIEFING_HINTS_OBJ_FAILED)
	{
		missionBriefing = stream->readText("briefing");
		gameHints.decodeData(stream, mapHeader.getVersionMinor());
	}

	if (versionMinor >= FILE_FORMAT_VERSION_PENDING_CONSTRUCTION) loadBuildProjects(stream);
	boost::mt19937 savedRandom;
	if (versionMinor >= FILE_FORMAT_VERSION_CONTINUATION_STATE && mapHeader.getIsSavedGame())
	{
		GAGCore::BinaryInputStream::CheckedReads checked(stream);
		// Boost's canonical stream representation is exactly 624 uint32 words.
		// Store fixed-width words, not locale-dependent text or a raw object.
		std::ostringstream state;
		state.imbue(std::locale::classic());
		stream->readEnterSection("randomState");
		for (unsigned i=0; i<boost::mt19937::state_size; ++i)
		{
			stream->readEnterSection(i);
			state << stream->readUint32("word") << ' ';
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		std::istringstream input(state.str());
		input.imbue(std::locale::classic());
		if (!(input >> savedRandom)) co_return false;
		map.loadRuntimeState(stream);
	}
	gameSection.commit();

	///versions less than 63 did not have fertility computed with the map, but computed it live.
	///compute it now
	if(mapHeader.getVersionMinor() < FILE_FORMAT_VERSION_PRE_FERTILITY)
	{
        FertilityCalculator::Job fertility(map);
        while (!fertility.advance(65536))
            co_await GAGCore::CooperativeTask::checkpoint("[Computing Fertility]");
        fertility.commit();
	}

	if (versionMinor >= FILE_FORMAT_VERSION_CONTINUATION_STATE && mapHeader.getIsSavedGame())
	{
		randomGenerator = savedRandom;
		hasSavedRandomState = true;
	}

	co_return true;
}

bool Game::checkBuildingsDoNotOverlapAndHealMissing() {
	std::vector<Uint16> buildings(map.getW()*map.getH(), NOGBID);
	for (int ti=0; ti<mapHeader.getNumberOfTeams(); ti++)
	{
		Team *team = teams[ti];
		for (int bi=0; bi<Building::MAX_COUNT; bi++)
		{
			const auto building = team->myBuildings[bi];
			if (!building)
				continue;
			if (building->buildingState==Building::DEAD)  // kill() cleared its footprint
				continue;
			const auto x = building->posX;
			const auto y = building->posY;
			const auto type = building->type;
			const auto w = type->width;
			const auto h = type->height;
			const auto gid = building->gid;
			for (int yi=y; yi<y+h; yi++)
				for (int xi=x; xi<x+w; xi++)
				{
					// virtual buildings (flags) do not participate in this check
					if (type->isVirtual)
						continue;
					// check for overlap
					const auto index = map.coordToIndex(xi, yi);
					checkInvariant(buildings[index]==NOGBID);
					buildings[index] = gid;
					// heal missing cells
					if (map.getTile(xi, yi).building != gid)
					{
						std::cerr << "Missing map cell GBID at " << xi << "," << yi
							<< " for team " << ti
							<< " building " << bi
							<< " (" << building->type->type << "), healing!"
							<< std::endl;
						map.getTile(xi, yi).building = gid;
					}
				}
		}
	}
	return true;
}

bool Game::integrity(void)
{
	///Check teams integrity
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
		checkInvariant(teams[i]->integrity());

	///Check that buildings do not overlap, as a pre-condition for healing
	checkInvariant(checkBuildingsDoNotOverlapAndHealMissing());

	///Check that all ID do point to existing objects
	for (int y=0; y<map.getH(); y++)
		for (int x=0; x<map.getW(); x++)
		{
			Tile& c = map.getTile(x, y);
			if (c.building != NOGBID)
			{
				int tid = Building::GIDtoTeam(c.building);
				checkInvariant(teams[tid]);
				int bid = Building::GIDtoID(c.building);
				const auto building = teams[tid]->myBuildings[bid];
				checkInvariant(building);

				// If a cell points at a building whose footprint doesn't
				// actually cover this cell, log it and clear the bad GBID.
				auto healOutsideCoord = [&](bool predicate, const char *coordName,
				                            int coordValue, int posValue, int endValue)
				{
					if (!predicate)
					{
						std::cerr << "Invalid coordinate " << coordName << "=" << coordValue
							<< " for team " << tid
							<< " building " << bid
							<< " (" << building->type->type << ")"
							<< " with " << coordName
							<< " span [" << posValue << ":" << endValue << "[, healing!"
							<< std::endl;
						map.getTile(x, y).building = NOGBID;
					}
				};

				// Footprints may straddle the map edge (posX can be -1), so
				// measure the cell's offset from the building with wrap-around.
				const auto buildingEndX = building->posX + building->type->width;
				healOutsideCoord(((x - building->posX) & map.wMask) < building->type->width,
				                 "X", x, building->posX, buildingEndX);
				const auto buildingEndY = building->posY + building->type->height;
				healOutsideCoord(((y - building->posY) & map.hMask) < building->type->height,
				                 "Y", y, building->posY, buildingEndY);
			}
			if (c.groundUnit != NOGUID)
			{
				int tid = Unit::GIDtoTeam(c.groundUnit);
				checkInvariant(teams[tid]);
				const auto unit = teams[tid]->myUnits[Unit::GIDtoID(c.groundUnit)];
				checkInvariant(unit);
				// checkInvariantText(unit->posX == x, ", unit " << unit->typeNum << " at " << x << "," << y << " has instead posX=" << unit->posX);
				// checkInvariantText(unit->posY == y, ", unit " << unit->typeNum << " at " << x << "," << y << " has instead posY=" << unit->posY);
			}
			if (c.airUnit != NOGUID)
			{
				int tid = Unit::GIDtoTeam(c.airUnit);
				checkInvariant(teams[tid]);
				const auto unit = teams[tid]->myUnits[Unit::GIDtoID(c.airUnit)];
				checkInvariant(unit);
				// A unit on its final step into a building (DIS_ENTERING_BUILDING)
				// already has the building's position but stays registered on the
				// cell it came from until it is inside (see UnitDisplacement.cpp).
				const bool entering = unit->displacement == Unit::DIS_ENTERING_BUILDING;
				const int expectedX = entering ? ((unit->posX - unit->dx) & map.wMask) : unit->posX;
				const int expectedY = entering ? ((unit->posY - unit->dy) & map.hMask) : unit->posY;
				checkInvariant(expectedX == x);
				checkInvariant(expectedY == y);
			}
		}
	return true;
}

void Game::save(GAGCore::OutputStream *stream, bool fileIsAMap, const std::string& name)
{
	assert(stream);
	stream->writeEnterSection("Game");
	if(dynamic_cast<GAGCore::BinaryOutputStream*>(stream))
	{
		dynamic_cast<GAGCore::BinaryOutputStream*>(stream)->enableSHA1();
	}

	///Save the two headers, record the position in the file because mapHeader will
	///will need to be overwritten with the mapOffset known.
	///
	/// We mutate mapHeader briefly to shape the on-disk record (mapName,
	/// isSavedGame), then restore it on scope exit via the RAII guard
	/// below. Without the restore, every in-game save (the ReplayWriter's
	/// initial state dump with name="replayHeader" and the GameGUI auto-save
	/// every 256 ticks with name="Auto save") would permanently overwrite
	/// the live mapHeader.mapName — observable later in things like the
	/// GLOB2_GAME_END "map=" field, which would read "Auto save" instead
	/// of the actual map. Map-editor "Save As" still wants the new name
	/// to persist; MapEdit::save() explicitly re-sets it after the call.
	struct MapHeaderRestoreGuard
	{
		MapHeader &header;
		std::string savedMapName;
		bool savedIsSavedGame;
		MapHeaderRestoreGuard(MapHeader &h)
			: header(h), savedMapName(h.getMapName()), savedIsSavedGame(h.getIsSavedGame()) {}
		MapHeaderRestoreGuard(const MapHeaderRestoreGuard &) = delete;
		MapHeaderRestoreGuard &operator=(const MapHeaderRestoreGuard &) = delete;
		~MapHeaderRestoreGuard()
		{
			header.setMapName(savedMapName);
			header.setIsSavedGame(savedIsSavedGame);
		}
	} mapHeaderRestore(mapHeader);

	Uint32 mapHeaderOffset = stream->getPosition();
	mapHeader.setMapName(name);
	mapHeader.setIsSavedGame(!fileIsAMap);
	mapHeader.resetGameSHA1();

	for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		mapHeader.getBaseTeam(i)=*teams[i];
		mapHeader.getBaseTeam(i).disableRecursiveDestruction=true;
	}

	for (int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
	{
		gameHeader.getBasePlayer(i)=*players[i];
		gameHeader.getBasePlayer(i).disableRecursiveDestruction=true;
	}

	mapHeader.save(stream);
	gameHeader.save(stream);

	///Save basic informations
	stream->write(FILE_SIG_GAME_BEGIN, FILE_SIG_LEN, "signatureStart");
	stream->writeUint32(stepCounter, "stepCounter");
	stream->write(FILE_SIG_GAME_BUILT, FILE_SIG_LEN, "signatureBeforeTeams");

	///Save teams
	stream->writeEnterSection("teams");
	for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		stream->writeEnterSection(i);
		teams[i]->save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->write(FILE_SIG_GAME_TEAM, FILE_SIG_LEN, "signatureAfterTeams");


	///Save the map offset to the header, before we save the map
	///Then, save the map
	mapHeader.setMapOffset(stream->getPosition());
	map.save(stream);
	stream->write(FILE_SIG_GAME_MAP, FILE_SIG_LEN, "signatureAfterMap");

	///Save the players
	stream->writeEnterSection("players");
	for (int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
	{
		stream->writeEnterSection(i);
		players[i]->save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->write(FILE_SIG_GAME_PLAYER, FILE_SIG_LEN, "signatureAfterPlayers");

	// Save the old map script state
	sgslScript.save(stream, this);

	// This is the new map script system
	mapscript.encodeData(stream);

	///Save game objectives
	objectives.encodeData(stream);
	stream->writeText(missionBriefing, "missionBriefing");
	gameHints.encodeData(stream);

	saveBuildProjects(stream);
	if (!fileIsAMap)
	{
		std::ostringstream randomState;
		randomState.imbue(std::locale::classic());
		randomState << randomGenerator;
		std::istringstream state(randomState.str());
		state.imbue(std::locale::classic());
		stream->writeEnterSection("randomState");
		for (unsigned i=0; i<boost::mt19937::state_size; ++i)
		{
			stream->writeEnterSection(i);
			Uint32 word;
			if (!(state >> word)) throw std::runtime_error("Invalid RNG state while saving");
			stream->writeUint32(word, "word");
			stream->writeLeaveSection();
		}

		stream->writeLeaveSection();
		map.saveRuntimeState(stream);
	}

	Uint8 sha1[SHA1_BYTE_LEN];
	for(int i=0; i<SHA1_BYTE_LEN; ++i)
		sha1[i]=0;
	if(dynamic_cast<GAGCore::BinaryOutputStream*>(stream))
	{
		dynamic_cast<GAGCore::BinaryOutputStream*>(stream)->finishSHA1(sha1);
	}
	mapHeader.setGameSHA1(sha1);

	///Overwrite the MapHeader. This is done after the map
	///offset has been set.
	if (stream->canSeek())
	{
		Uint32 position = stream->getPosition();
		stream->seekFromStart(mapHeaderOffset);
		mapHeader.save(stream);
		stream->seekFromStart(position);
	}

	stream->writeLeaveSection();

	// mapHeaderRestore's destructor restores the pre-save mapName and
	// isSavedGame on scope exit.
}

Uint32 Game::checkSum(std::vector<Uint32> *checkSumsVector, std::vector<Uint32> *checkSumsVectorForBuildings, std::vector<Uint32> *checkSumsVectorForUnits, bool heavy)
{
	Uint32 cs=0;

	Uint32 headerCs=mapHeader.checkSum();
	cs^=headerCs;
	if (checkSumsVector)
		checkSumsVector->push_back(headerCs);// [0]

	cs=rotr1(cs);

	Uint32 teamsCs=0;
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		teamsCs^=teams[i]->checkSum(checkSumsVector, checkSumsVectorForBuildings, checkSumsVectorForUnits);
		teamsCs=rotr1(teamsCs);
		cs=rotr1(cs);
	}
	cs^=teamsCs;
	if (checkSumsVector)
		checkSumsVector->push_back(teamsCs);// [1+t*20]

	cs=rotr1(cs);

	Uint32 playersCs=0;
	for (int i=0; i<gameHeader.getNumberOfPlayers(); i++)
	{
		playersCs^=players[i]->checkSum(checkSumsVector);
		playersCs=rotr1(playersCs);
		cs=rotr1(cs);
	}
	cs^=playersCs;
	if (checkSumsVector)
		checkSumsVector->push_back(playersCs);// [2+t*20+p*2]

	cs=rotr1(cs);

	for (int i=0; i<gameHeader.getNumberOfPlayers(); i++)
	{
		if (players[i]->type==BasePlayer::P_IP)
		{
			heavy=true;
			break;
		}
	}
	Uint32 mapCs=map.checkSum(heavy);
	cs^=mapCs;
	if (checkSumsVector)
		checkSumsVector->push_back(mapCs);// [3+t*20+p*2]

	cs=rotr1(cs);

	Uint32 scriptCs=sgslScript.checkSum();
	cs^=scriptCs;
	if (checkSumsVector)
		checkSumsVector->push_back(scriptCs);// [4+t*20+p*2]

	return cs;
}
