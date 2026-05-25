// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "GameHeader.h"

#include "FileFormatVersions.h"

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
		allyTeamNumbers[i] = i+1;
	}
	allyTeamsFixed=true;
	winningConditions = WinningCondition::getDefaultWinningConditions();
	mapDiscovered=false;
}



bool GameHeader::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	stream->readEnterSection("GameHeader");
	gameLatency = stream->readSint32("gameLatency");
	orderRate = stream->readUint8("orderRate");
	numberOfPlayers = stream->readSint32("numberOfPlayers");
	if (numberOfPlayers > Team::MAX_COUNT)
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
				stream->readLeaveSection(i);
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
		stream->readLeaveSection(i);
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

		stream->readEnterSection("winningConditions");
		winningConditions.clear();
		Uint32 size = stream->readUint32("size");
		for(unsigned int i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			winningConditions.push_back(WinningCondition::getWinningCondition(stream, versionMinor));
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}
	if(versionMinor >= FILE_FORMAT_VERSION_UNIFIED_SEED)
		seed = stream->readUint32("seed");
	if(versionMinor >=  FILE_FORMAT_VERSION_MAP_DISCOVERED_FLAG)
		mapDiscovered = stream->readUint8("mapDiscovered");
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

		stream->readEnterSection("winningConditions");
		winningConditions.clear();
		Uint32 size = stream->readUint32("size");
		for(unsigned int i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			winningConditions.push_back(WinningCondition::getWinningCondition(stream, versionMinor));
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}
	if(versionMinor >= FILE_FORMAT_VERSION_UNIFIED_SEED)
		seed = stream->readUint32("seed");
	if(versionMinor >=  FILE_FORMAT_VERSION_MAP_DISCOVERED_FLAG)
		mapDiscovered = stream->readUint8("mapDiscovered");
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
	stream->writeLeaveSection();
}



bool GameHeader::loadPlayerInfo(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	stream->readEnterSection("GameHeader");
	numberOfPlayers = stream->readSint32("numberOfPlayers");
	stream->readEnterSection("players");
	for(int i=0; i<Team::MAX_COUNT_ON_DISK; ++i)
	{
		stream->readEnterSection(i);
		if (i < Team::MAX_COUNT)
		{
			players[i].load(stream, versionMinor);
		}
		else
		{
			BasePlayer scratch;
			scratch.load(stream, versionMinor);
		}
		stream->readLeaveSection(i);
	}
	stream->readLeaveSection();
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
	stream->writeLeaveSection();
}
