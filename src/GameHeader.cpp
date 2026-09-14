// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "GameHeader.h"

#include "FileFormatVersions.h"

#include <algorithm>
#include <ctime>

GameHeader::GameHeader()
{
	reset();
}

void GameHeader::reset()
{
	//These are the default game options
	numberOfPlayers = 0;
	gameLatency = 0;
	orderRate = 1;
	//Seed is random by default
	seed = std::time(NULL);
	//If needed, seed can be fixed, default value, 5489
	//seed = 5489;
	
	for (Uint8 i=0; i<Team::MAX_COUNT; ++i)
	{
		players[i] = BasePlayer();
		aiConfig[i].clear();
		allyTeamNumbers[i] = i+1;
	}
	allyTeamsFixed=true;
	winningConditions = WinningCondition::getDefaultWinningConditions();
	mapDiscovered=false;
	resourceGrowthDisabled=false;
	resourceScarcityLevel=0;
	instantConstruction=false;
	stockpileStartLevel=0;
	hungerDisabled=false;
	unitUpgradesDisabled=false;
	glassCannonLevel=0;
	unitsFearless=false;
	permadeathDisabled=false;
	peacefulMode=false;
	buildingHpLevel=0;
}



namespace
{
	// 1-based ally-team IDs. reset() seeds allyTeamNumbers[i] = i+1, so
	// team 1 and team 2 are the lowest two groups available.
	constexpr Uint8 HUMAN_ALLY_TEAM = 1;
	constexpr Uint8 ENEMY_ALLY_TEAM = 2;
}

void GameHeader::setDefaultAlliances(std::optional<int> humanColor, const std::vector<int>& aiColors)
{
	for (int i = 0; i < Team::MAX_COUNT; ++i)
		setAllyTeamNumber(i, i + 1);
	if (!humanColor)
		return;
	setAllyTeamNumber(*humanColor, HUMAN_ALLY_TEAM);
	for (int aiColor : aiColors)
		if (aiColor != *humanColor)
			setAllyTeamNumber(aiColor, ENEMY_ALLY_TEAM);
}



bool GameHeader::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	stream->readEnterSection("GameHeader");
	gameLatency = stream->readSint32("gameLatency");
	orderRate = stream->readUint8("orderRate");
	numberOfPlayers = stream->readSint32("numberOfPlayers");
	if (numberOfPlayers < 0 || numberOfPlayers > Team::MAX_COUNT)
	{
		return false;
	}
	stream->readEnterSection("players");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		stream->readEnterSection(i);
		if (i < Team::MAX_COUNT)
		{
			if (!players[i].load(stream, versionMinor))
			{
				stream->readLeaveSection();
				stream->readLeaveSection();
				stream->readLeaveSection();
				return false;
			}
		}
		else
		{
			BasePlayer scratch;
			// Trailing on-disk slots beyond Team::MAX_COUNT are padding; their
			// teamNumber field is never consumed, so a bad value here is not a
			// crash hazard. Discard validation failures.
			scratch.load(stream, versionMinor);
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	if(versionMinor >= FILE_FORMAT_VERSION_ALLIES_AND_WIN_CONDITIONS)
	{
		stream->readEnterSection("allyTeamNumbers");
		for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
		{
			Uint8 v = stream->readUint8("allyTeamNumber");
			if (i < Team::MAX_COUNT)
				allyTeamNumbers[i] = v;
		}
		stream->readLeaveSection();
		allyTeamsFixed = stream->readUint8("allyTeamsFixed");

		if (!WinningCondition::loadWinningConditions(stream, versionMinor, winningConditions))
			return false;
	}
	if(versionMinor >= FILE_FORMAT_VERSION_UNIFIED_SEED)
		seed = stream->readUint32("seed");
	if(versionMinor >=  FILE_FORMAT_VERSION_MAP_DISCOVERED_FLAG)
		mapDiscovered = stream->readUint8("mapDiscovered");
	if (!loadAIConfig(stream, versionMinor)) return false;
	if(versionMinor >= FILE_FORMAT_VERSION_ECONOMY_RULES)
	{
		resourceGrowthDisabled = stream->readUint8("resourceGrowthDisabled");
		// Clamped to the tier lookup tables' range (Game.cpp's stockpileAmount[],
		// Map::growResources's scarcityDivisor[]): a corrupted save or a
		// malicious network peer could otherwise supply any Uint8 (0-255) and
		// trigger an out-of-bounds array read wherever these are used to index.
		resourceScarcityLevel = std::min<Uint8>(stream->readUint8("resourceScarcityLevel"), 3);
		instantConstruction = stream->readUint8("instantConstruction");
		stockpileStartLevel = std::min<Uint8>(stream->readUint8("stockpileStartLevel"), 3);
		hungerDisabled = stream->readUint8("hungerDisabled");
	}
	if(versionMinor >= FILE_FORMAT_VERSION_COMBAT_RULES)
	{
		unitUpgradesDisabled = stream->readUint8("unitUpgradesDisabled");
		// Clamped to the tier lookup tables' range (this class's own
		// getGlassCannonScale()/getBuildingHpMultiplier()): a corrupted save
		// or a malicious network peer could otherwise supply any Uint8
		// (0-255) and trigger an out-of-bounds array read wherever these are
		// used to index.
		glassCannonLevel = std::min<Uint8>(stream->readUint8("glassCannonLevel"), 2);
		unitsFearless = stream->readUint8("unitsFearless");
		permadeathDisabled = stream->readUint8("permadeathDisabled");
		peacefulMode = stream->readUint8("peacefulMode");
		buildingHpLevel = std::min<Uint8>(stream->readUint8("buildingHpLevel"), 2);
	}
	stream->readLeaveSection();
	return true;
}



void GameHeader::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("GameHeader");
	stream->writeSint32(gameLatency, "gameLatency");
	stream->writeUint8(orderRate, "orderRate");
	stream->writeSint32(numberOfPlayers, "numberOfPlayers");
	stream->writeEnterSection("players");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		stream->writeEnterSection(i);
		if (i < Team::MAX_COUNT)
			players[i].save(stream);
		else
			BasePlayer().save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("allyTeamNumbers");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		const Uint8 v = (i < Team::MAX_COUNT) ? allyTeamNumbers[i] : static_cast<Uint8>(i + 1);
		stream->writeUint8(v, "allyTeamNumber");
	}
	stream->writeLeaveSection();
	stream->writeUint8(allyTeamsFixed, "allyTeamsFixed");
	stream->writeEnterSection("winningConditions");
	stream->writeUint32(winningConditions.size(), "size");
	int n=0;
	for(std::list<std::shared_ptr<WinningCondition> >::const_iterator i=winningConditions.begin(); i!=winningConditions.end(); ++i)
	{
		stream->writeEnterSection(n);
		(*i)->encodeData(stream);
		stream->writeLeaveSection();
		n+=1;
	}
	stream->writeLeaveSection();
	stream->writeUint32(seed, "seed");
	stream->writeUint8(mapDiscovered, "mapDiscovered");
	saveAIConfig(stream);
	stream->writeUint8(resourceGrowthDisabled, "resourceGrowthDisabled");
	stream->writeUint8(resourceScarcityLevel, "resourceScarcityLevel");
	stream->writeUint8(instantConstruction, "instantConstruction");
	stream->writeUint8(stockpileStartLevel, "stockpileStartLevel");
	stream->writeUint8(hungerDisabled, "hungerDisabled");
	stream->writeUint8(unitUpgradesDisabled, "unitUpgradesDisabled");
	stream->writeUint8(glassCannonLevel, "glassCannonLevel");
	stream->writeUint8(unitsFearless, "unitsFearless");
	stream->writeUint8(permadeathDisabled, "permadeathDisabled");
	stream->writeUint8(peacefulMode, "peacefulMode");
	stream->writeUint8(buildingHpLevel, "buildingHpLevel");
	stream->writeLeaveSection();
}



bool GameHeader::loadWithoutPlayerInfo(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	stream->readEnterSection("GameHeader");
	gameLatency = stream->readSint32("gameLatency");
	orderRate = stream->readUint8("orderRate");
	if(versionMinor >= FILE_FORMAT_VERSION_ALLIES_AND_WIN_CONDITIONS)
	{
		stream->readEnterSection("allyTeamNumbers");
		for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
		{
			Uint8 v = stream->readUint8("allyTeamNumber");
			if (i < Team::MAX_COUNT)
				allyTeamNumbers[i] = v;
		}
		stream->readLeaveSection();
		allyTeamsFixed = stream->readUint8("allyTeamsFixed");

		if (!WinningCondition::loadWinningConditions(stream, versionMinor, winningConditions))
			return false;
	}
	if(versionMinor >= FILE_FORMAT_VERSION_UNIFIED_SEED)
		seed = stream->readUint32("seed");
	if(versionMinor >=  FILE_FORMAT_VERSION_MAP_DISCOVERED_FLAG)
		mapDiscovered = stream->readUint8("mapDiscovered");
	if (!loadAIConfig(stream, versionMinor)) return false;
	if(versionMinor >= FILE_FORMAT_VERSION_ECONOMY_RULES)
	{
		resourceGrowthDisabled = stream->readUint8("resourceGrowthDisabled");
		resourceScarcityLevel = std::min<Uint8>(stream->readUint8("resourceScarcityLevel"), 3);
		instantConstruction = stream->readUint8("instantConstruction");
		stockpileStartLevel = std::min<Uint8>(stream->readUint8("stockpileStartLevel"), 3);
		hungerDisabled = stream->readUint8("hungerDisabled");
	}
	if(versionMinor >= FILE_FORMAT_VERSION_COMBAT_RULES)
	{
		unitUpgradesDisabled = stream->readUint8("unitUpgradesDisabled");
		// Clamped to the tier lookup tables' range (this class's own
		// getGlassCannonScale()/getBuildingHpMultiplier()): a corrupted save
		// or a malicious network peer could otherwise supply any Uint8
		// (0-255) and trigger an out-of-bounds array read wherever these are
		// used to index.
		glassCannonLevel = std::min<Uint8>(stream->readUint8("glassCannonLevel"), 2);
		unitsFearless = stream->readUint8("unitsFearless");
		permadeathDisabled = stream->readUint8("permadeathDisabled");
		peacefulMode = stream->readUint8("peacefulMode");
		buildingHpLevel = std::min<Uint8>(stream->readUint8("buildingHpLevel"), 2);
	}
	stream->readLeaveSection();
	return true;
}



void GameHeader::saveWithoutPlayerInfo(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("GameHeader");
	stream->writeSint32(gameLatency, "gameLatency");
	stream->writeUint8(orderRate, "orderRate");
	stream->writeEnterSection("allyTeamNumbers");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		const Uint8 v = (i < Team::MAX_COUNT) ? allyTeamNumbers[i] : static_cast<Uint8>(i + 1);
		stream->writeUint8(v, "allyTeamNumber");
	}
	stream->writeLeaveSection();
	stream->writeUint8(allyTeamsFixed, "allyTeamsFixed");
	stream->writeEnterSection("winningConditions");
	stream->writeUint32(winningConditions.size(), "size");
	int n=0;
	for(std::list<std::shared_ptr<WinningCondition> >::const_iterator i=winningConditions.begin(); i!=winningConditions.end(); ++i)
	{
		stream->writeEnterSection(n);
		(*i)->encodeData(stream);
		stream->writeLeaveSection();
		n+=1;
	}
	stream->writeLeaveSection();
	stream->writeUint32(seed, "seed");
	stream->writeUint8(mapDiscovered, "mapDiscovered");
	saveAIConfig(stream);
	stream->writeUint8(resourceGrowthDisabled, "resourceGrowthDisabled");
	stream->writeUint8(resourceScarcityLevel, "resourceScarcityLevel");
	stream->writeUint8(instantConstruction, "instantConstruction");
	stream->writeUint8(stockpileStartLevel, "stockpileStartLevel");
	stream->writeUint8(hungerDisabled, "hungerDisabled");
	stream->writeUint8(unitUpgradesDisabled, "unitUpgradesDisabled");
	stream->writeUint8(glassCannonLevel, "glassCannonLevel");
	stream->writeUint8(unitsFearless, "unitsFearless");
	stream->writeUint8(permadeathDisabled, "permadeathDisabled");
	stream->writeUint8(peacefulMode, "peacefulMode");
	stream->writeUint8(buildingHpLevel, "buildingHpLevel");
	stream->writeLeaveSection();
}



bool GameHeader::loadPlayerInfo(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	stream->readEnterSection("GameHeader");
	numberOfPlayers = stream->readSint32("numberOfPlayers");
	if (numberOfPlayers < 0 || numberOfPlayers > Team::MAX_COUNT) return false;
	stream->readEnterSection("players");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		stream->readEnterSection(i);
		if (i < Team::MAX_COUNT)
		{
			if (!players[i].load(stream, versionMinor))
			{
				stream->readLeaveSection();
				stream->readLeaveSection();
				stream->readLeaveSection();
				return false;
			}
		}
		else
		{
			BasePlayer scratch;
			// Trailing on-disk slots beyond Team::MAX_COUNT are padding; their
			// fields are never consumed, so a bad value here is not a crash
			// hazard. Discard validation failures.
			scratch.load(stream, versionMinor);
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	if (!loadAIConfig(stream, versionMinor)) return false;
	stream->readLeaveSection();
	return true;
}



void GameHeader::savePlayerInfo(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("GameHeader");
	stream->writeSint32(numberOfPlayers, "numberOfPlayers");
	stream->writeEnterSection("players");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		stream->writeEnterSection(i);
		if (i < Team::MAX_COUNT)
			players[i].save(stream);
		else
			BasePlayer().save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	saveAIConfig(stream);
	stream->writeLeaveSection();
}


bool GameHeader::loadAIConfig(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	for (auto &values : aiConfig) values.clear();
	if (versionMinor < 101) return true;
	stream->readEnterSection("aiConfig");
	const Uint32 count = stream->readUint32("count");
	if (count > Team::MAX_COUNT) return false;
	for (Uint32 i=0; i<count; ++i)
	{
		stream->readEnterSection(i);
		aiConfig[i] = stream->readText("values");
		if (aiConfig[i].size() > 262144) return false;
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	return true;
}

void GameHeader::saveAIConfig(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("aiConfig");
	stream->writeUint32(Team::MAX_COUNT, "count");
	for (int i=0; i<Team::MAX_COUNT; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeText(aiConfig[i], "values");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}
