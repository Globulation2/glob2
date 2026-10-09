// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <FileManager.h>
#include <FormatableString.h>
#include <Toolkit.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <StreamBackend.h>

#include "AINames.h"
#include "Engine.h"
#include "EngineTiming.h"
#include "GlobalContainer.h"
#include "Player.h"
#include "Utilities.h"

#include <iostream>
#include <memory>
#include <optional>


namespace
{
std::unique_ptr<BinaryInputStream> openOwnedGameStream(const std::string& filename)
{
    // Inflation may already own a large snapshot. Keep it owned if allocating
    // the stream wrapper fails; BinaryInputStream takes ownership on success.
    std::unique_ptr<StreamBackend> backend(glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(), filename));
    auto stream = std::make_unique<BinaryInputStream>(backend.get());
    backend.release();
    return stream;
}
}

// Header preflight used to inflate the whole file again for each reader.
// One validated snapshot belongs to this load operation, through deserialization.
std::unique_ptr<InputStream> Engine::openGameInput(const std::string& filename, MapHeader& map, GameHeader& players)
{
    try
    {
        auto stream = openOwnedGameStream(filename);
        if (!stream->isValid() || !map.load(stream.get())) return {};
        map.resolveGrowthLayout(stream.get());
        if (!players.load(stream.get(), map.loadingVersion(), map.historicalGrowthLayout ? map.getVersionMinor() : 0)) return {};
        map.setMapName(glob2FilenameToName(filename));
        stream->seekFromStart(0);
        return stream;
    }
    catch (const std::exception& error)
    { std::cerr << "Cannot load headers: " << error.what() << std::endl; return {}; }
}

// Loads a map header from disk. The two failure modes are asymmetric:
//   * Missing or unreadable file: logs to stderr and returns a default-constructed
//     MapHeader (numberOfTeams == 0). Callers must check.
//   * Malformed file contents: re-throws std::ios_base::failure after logging.
MapHeader Engine::loadMapHeader(const std::string &filename)
{
	MapHeader mapHeader;
	std::unique_ptr<InputStream> stream = openOwnedGameStream(filename);
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
	std::unique_ptr<InputStream> stream = openOwnedGameStream(filename);
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
		if (headerValid) mapHeader.resolveGrowthLayout(stream.get());
		bool validMapSelected = headerValid && gameHeader.load(stream.get(), mapHeader.loadingVersion(), mapHeader.historicalGrowthLayout ? mapHeader.getVersionMinor() : 0);
		if (!headerValid || !validMapSelected)
		{
			std::cerr << "Engine::loadGameHeader : invalid game header for map " << filename << std::endl;
			return GameHeader();
		}
	}
	return gameHeader;

}



// Pick one map from the maps/ directory at random and load its header.
// Three outcomes for the caller to handle:
//   * --map override set: returns the named map (still throws on missing
//     file, since a typo'd name is a fatal user error).
//   * No override + maps/ has at least one .map file: consumes one
//     draw from the private map-selection stream to index the listing and returns
//     the loaded MapHeader.
//   * No override + maps/ is empty or unreadable: returns std::nullopt
//     without consuming RNG state. Callers must surface this as a clear
//     fatal-config error — previously this path was undefined behavior
//     (a modulo-zero draw caused SIGFPE on x86, bypassing the createRandomGame
//     retry-on-malformed-file loop and terminating the process).
// Loaded maps that turn out to be malformed propagate via
// std::ios_base::failure (the existing retry loop in createRandomGame
// catches that and picks again).
std::optional<MapHeader> Engine::chooseRandomMap()
{
	if (!globalContainer->testGamesMap.empty())
	{
		std::string fullPath = std::string("maps") + DIR_SEPARATOR
			+ globalContainer->testGamesMap + ".map";
		return loadMapHeader(fullPath);
	}

	std::vector<std::string> maps = glob2ListMapOrSaveFiles(*Toolkit::getFileManager(), "maps", "map");

	if (maps.empty())
		return std::nullopt;

	if (!mapSelectionInitialized)
	{
		const Uint32 seed = globalContainer->testGamesSeedSet ? globalContainer->testGamesSeed : gui.game.gameHeader.getRandomSeed();
		mapSelectionRandom.initializeOwner(seed, unsigned(RandomDomain::MatchMap));
		mapSelectionInitialized = true;
	}
	int number = mapSelectionRandom.nextU32() % maps.size();

	return loadMapHeader(maps[number]);
}



GameHeader Engine::createRandomGame(int numberOfTeams)
{
	GameHeader gameHeader;
	gameHeader.setRandomSeed(globalContainer->testGamesSeedSet ? globalContainer->testGamesSeed : gui.game.gameHeader.getRandomSeed());
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
			EntityRandom seatRandom;
			seatRandom.initializeOwner(gameHeader.getRandomSeed(), unsigned(RandomDomain::MatchAI), teamColor);
			AI::ImplementationID iid;
			if (!globalContainer->testGamesMatchup.empty())
			{
				// --matchup: matchup[k] is the AI for team k. teamColor
				// here equals the team this AI plays for (the wrap-around
				// at i==numberOfTeams gives teamColor=0, which gets
				// matchup[0]). Team-count consistency was verified by the
				// caller (createRandomGame() parameterless) before we got
				// here, so direct indexing is safe.
				iid = static_cast<AI::ImplementationID>(
					globalContainer->testGamesMatchup[teamColor]);
			}
			else if (!globalContainer->testGamesAIPool.empty())
			{
				int idx = seatRandom.nextU32() % globalContainer->testGamesAIPool.size();
				iid = static_cast<AI::ImplementationID>(globalContainer->testGamesAIPool[idx]);
			}
			else
			{
				iid = static_cast<AI::ImplementationID>(seatRandom.nextU32() % AI_RANDOM_PICK_COUNT + 1);
			}
			FormattableString name("%0 %1");
			name.arg(AINames::getAIText(iid)).arg(i-1);
			gameHeader.getBasePlayer(count) = BasePlayer(i, name.c_str(), teamColor, Player::playerTypeFromImplementationID(iid));
		}
		gameHeader.setAllyTeamNumber(teamColor, teamColor);
		count+=1;
	}
	gameHeader.setNumberOfPlayers(count);
	return gameHeader;
}
