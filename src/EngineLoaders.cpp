// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <FileManager.h>
#include <FormatableString.h>
#include <Toolkit.h>
#include <Stream.h>
#include <BinaryStream.h>

#include "AINames.h"
#include "Engine.h"
#include "EngineTiming.h"
#include "GlobalContainer.h"
#include "Player.h"
#include "Utilities.h"

#include <iostream>
#include <memory>


// Loads a map header from disk. The two failure modes are asymmetric:
//   * Missing or unreadable file: logs to stderr and returns a default-constructed
//     MapHeader (numberOfTeams == 0). Callers must check.
//   * Malformed file contents: re-throws std::ios_base::failure after logging.
MapHeader Engine::loadMapHeader(const std::string &filename)
{
	MapHeader mapHeader;
	std::unique_ptr<InputStream> stream = std::make_unique<BinaryInputStream>(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "Engine::loadMapHeader : error, can't open file " << filename  << std::endl;
	}
	else
	{
		if (verbose)
			std::cout << "Engine::loadMapHeader : loading map " << filename << std::endl;

		bool validMapSelected;

		try
		{
			validMapSelected = mapHeader.load(stream.get());
		}
		catch (std::ios_base::failure &e)
		{
			// Notify what filename couldn't load, because if we're doing -test-games(-nox) and loading the map fails,
			// the map name won't be saved inside mapHeader.
			std::cerr << "Engine::loadMapHeader : can't load map \"" << filename << "\": bad format" << std::endl;

			// We didn't solve the problem though, so we re-throw
			throw;
		}

		if (!validMapSelected)
			std::cerr << "Engine::loadMapHeader : invalid map header for map " << filename << std::endl;
	}

	mapHeader.setMapName(glob2FilenameToName(filename));

	return mapHeader;
}



GameHeader Engine::loadGameHeader(const std::string &filename)
{
	MapHeader mapHeader;
	GameHeader gameHeader;
	std::unique_ptr<InputStream> stream = std::make_unique<BinaryInputStream>(Toolkit::getFileManager()->openInputStreamBackend(filename));
	if (stream->isEndOfStream())
	{
		std::cerr << "Engine::loadGameHeader : error, can't open file " << filename  << std::endl;
		return GameHeader(); // an empty game header
	}
	else
	{
		if (verbose)
			std::cout << "Engine::loadGameHeader : loading map " << filename << std::endl;
		bool headerValid = mapHeader.load(stream.get());
		bool validMapSelected = gameHeader.load(stream.get(), mapHeader.getVersionMinor());
		if (!headerValid || !validMapSelected)
		{
			std::cerr << "Engine::loadGameHeader : invalid game header for map " << filename << std::endl;
			return GameHeader();
		}
	}
	return gameHeader;

}



MapHeader Engine::chooseRandomMap()
{
	// --map override: pin to a specific map by bare name. Resolved as
	// maps/<name>.map. Throws std::ios_base::failure on missing file —
	// the caller (createRandomGame) handles that with a clear error
	// rather than letting the legacy retry-loop spin forever.
	if (!globalContainer->testGamesMap.empty())
	{
		std::string fullPath = std::string("maps") + DIR_SEPARATOR
			+ globalContainer->testGamesMap + ".map";
		return loadMapHeader(fullPath);
	}

	std::vector<std::string> maps;

	std::string fullDir = "maps";

	// we add the other files
	if (Toolkit::getFileManager()->initDirectoryListing(fullDir.c_str(), "map", false))
	{
		std::string fileName;
		while (!(fileName = (Toolkit::getFileManager()->getNextDirectoryEntry())).empty())
		{
			std::string fullFileName = fullDir + DIR_SEPARATOR + fileName;
			maps.push_back(fullFileName);
		}
	}

	int number = syncRand() % maps.size();

	return loadMapHeader(maps[number]);
}



GameHeader Engine::createRandomGame(int numberOfTeams)
{
	GameHeader gameHeader;
	int count = 0;
	for (int i=0; i<numberOfTeams+1; i++)
	{
		int teamColor=(i % numberOfTeams);
		if (i==0)
		{
			gameHeader.getBasePlayer(count) = BasePlayer(0, globalContainer->settings.getUsername(), teamColor, BasePlayer::P_LOCAL);
		}
		else
		{
			AI::ImplementitionID iid;
			if (!globalContainer->testGamesMatchup.empty())
			{
				// --matchup: matchup[k] is the AI for team k. teamColor
				// here equals the team this AI plays for (the wrap-around
				// at i==numberOfTeams gives teamColor=0, which gets
				// matchup[0]). Team-count consistency was verified by the
				// caller (createRandomGame() parameterless) before we got
				// here, so direct indexing is safe.
				iid = static_cast<AI::ImplementitionID>(
					globalContainer->testGamesMatchup[teamColor]);
			}
			else if (!globalContainer->testGamesAIPool.empty())
			{
				int idx = syncRand() % globalContainer->testGamesAIPool.size();
				iid = static_cast<AI::ImplementitionID>(globalContainer->testGamesAIPool[idx]);
			}
			else
			{
				iid = static_cast<AI::ImplementitionID>(syncRand() % AI_RANDOM_PICK_COUNT + 1);
			}
			FormatableString name("%0 %1");
			name.arg(AINames::getAIText(iid)).arg(i-1);
			gameHeader.getBasePlayer(count) = BasePlayer(i, name.c_str(), teamColor, Player::playerTypeFromImplementitionID(iid));
		}
		gameHeader.setAllyTeamNumber(teamColor, teamColor);
		count+=1;
	}
	gameHeader.setNumberOfPlayers(count);
	return gameHeader;
}
