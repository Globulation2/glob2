// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <iostream>

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
#include "FertilityCalculatorDialog.h"

#include "ReplayWriter.h"

#define BULLET_IMGID 0

// Save/load, integrity, checksum. Split out of Game.cpp.

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
	assert(stream);

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
		return false;
	mapHeader=tempMapHeader;
	Sint32 versionMinor=mapHeader.getVersionMinor();


	// We load the game header
	GameHeader tempGameHeader;
	if (verbose)
		printf("Loading game header\n");
	if (!tempGameHeader.load(stream, versionMinor))
		return false;
	gameHeader=tempGameHeader;

	if (!readMatchingSignature(stream, FILE_SIG_GAME_BEGIN, "signatureStart"))
		return false;

	///Load the step counter
	stepCounter = stream->readUint32("stepCounter");

	if(versionMinor < FILE_FORMAT_VERSION_UNIFIED_SEED)
	{
		///Load random seeds, these are no longer used
		stream->readUint32("SyncRandSeedA");
		stream->readUint32("SyncRandSeedB");
		stream->readUint32("SyncRandSeedC");

		if (!readMatchingSignature(stream, FILE_SIG_GAME_SYNC, "signatureAfterSyncRand"))
			return false;
	}
	else
	{
		if (!readMatchingSignature(stream, FILE_SIG_GAME_BUILT, "signatureBeforeTeams"))
			return false;
	}

	///Load teams
	stream->readEnterSection("teams");
	for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		stream->readEnterSection(i);
		teams[i]=new Team(stream, this, versionMinor);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	if (!readMatchingSignature(stream, FILE_SIG_GAME_TEAM, "signatureAfterTeams"))
		return false;

	// Load the map. Team has to be saved and loaded first.
	if(!map.load(stream, mapHeader, this))
		return false;

	if (!readMatchingSignature(stream, FILE_SIG_GAME_MAP, "signatureAfterMap"))
		return false;

	// Load the players. Both Map and Team must be loaded first.
	stream->readEnterSection("players");
	for (int i=0; i<gameHeader.getNumberOfPlayers(); ++i)
	{
		stream->readEnterSection(i);
		players[i]=new Player(stream, teams, versionMinor);
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	if (!readMatchingSignature(stream, FILE_SIG_GAME_PLAYER, "signatureAfterPlayers"))
		return false;

	// We have to finish Team's loading
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		teams[i]->update();
	}

	// Check integrity of loaded game
	if (!integrity())
		return false;

	// Now load the old map script
	if (!sgslScript.load(stream, this))
		return false;

	if(versionMinor >= FILE_FORMAT_VERSION_USL_MAPSCRIPT)
	{
		// This is the new map script system
		if (!mapscript.decodeData(stream, mapHeader.getVersionMinor()))
			return false;
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

	gameSection.commit();

	///versions less than 63 did not have fertility computed with the map, but computed it live.
	///compute it now
	if(mapHeader.getVersionMinor() < FILE_FORMAT_VERSION_PRE_FERTILITY)
	{
	    if(globalContainer->runNoX)
	    {
	        FertilityCalculator::compute(map, {});
	    }
	    else
	    {
	        FertilityCalculatorDialog dialog(globalContainer->gfx, map);
	        dialog.runModal();
	    }
	}

	return true;
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
					if (map.getCase(xi, yi).building != gid)
					{
						std::cerr << "Missing map cell GBID at " << xi << "," << yi
							<< " for team " << ti
							<< " building " << bi
							<< " (" << building->type->type << "), healing!"
							<< std::endl;
						map.getCase(xi, yi).building = gid;
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
			Case& c = map.getCase(x, y);
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
						map.getCase(x, y).building = NOGBID;
					}
				};

				const auto buildingEndX = building->posX + building->type->width;
				healOutsideCoord(x >= building->posX || x < (buildingEndX & map.wMask),
				                 "X", x, building->posX, buildingEndX);
				healOutsideCoord(x < buildingEndX, "X", x, building->posX, buildingEndX);
				const auto buildingEndY = building->posY + building->type->height;
				healOutsideCoord(y >= building->posY || y < (buildingEndY & map.hMask),
				                 "Y", y, building->posY, buildingEndY);
				healOutsideCoord(y < buildingEndY, "Y", y, building->posY, buildingEndY);
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
				checkInvariant(unit->posX == x);
				checkInvariant(unit->posY == y);
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
