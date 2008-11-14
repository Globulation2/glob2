/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "GameHeader.h"

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
	
	for(int i=0; i<32; ++i)
	{
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
	stream->readEnterSection("players");
	for(int i=0; i<32; ++i)
	{
		stream->readEnterSection(i);
		players[i].load(stream, versionMinor);
		stream->readLeaveSection(i);
	}
	stream->readLeaveSection();
	if(versionMinor >= 71)
	{
		stream->readEnterSection("allyTeamNumbers");
		for(int i=0; i<32; ++i)
		{
			allyTeamNumbers[i] = stream->readUint8("allyTeamNumber");
		}
		stream->readLeaveSection();
		allyTeamsFixed = stream->readUint8("allyTeamsFixed");
		
		stream->readEnterSection("winningConditions");
		winningConditions.clear();
		Uint32 size = stream->readUint32("size");
		for(int i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			winningConditions.push_back(WinningCondition::getWinningCondition(stream, versionMinor));
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}
	if(versionMinor >= 64)
		seed = stream->readUint32("seed");
	if(versionMinor >=  72)
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
	for(int i=0; i<32; ++i)
	{
		stream->writeEnterSection(i);
		players[i].save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("allyTeamNumbers");
	for(int i=0; i<32; ++i)
	{
		stream->writeUint8(allyTeamNumbers[i], "allyTeamNumber");
	}
	stream->writeLeaveSection();
	stream->writeUint8(allyTeamsFixed, "allyTeamsFixed");
	stream->writeEnterSection("winningConditions");
	stream->writeUint32(winningConditions.size(), "size");
	int n=0;
	for(std::list<boost::shared_ptr<WinningCondition> >::const_iterator i=winningConditions.begin(); i!=winningConditions.end(); ++i)
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
	if(versionMinor >= 71)
	{
		stream->readEnterSection("allyTeamNumbers");
		for(int i=0; i<32; ++i)
		{
			allyTeamNumbers[i] = stream->readUint8("allyTeamNumber");
		}
		stream->readLeaveSection();
		allyTeamsFixed = stream->readUint8("allyTeamsFixed");
		
		stream->readEnterSection("winningConditions");
		winningConditions.clear();
		Uint32 size = stream->readUint32("size");
		for(int i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			winningConditions.push_back(WinningCondition::getWinningCondition(stream, versionMinor));
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
	}
	if(versionMinor >= 64)
		seed = stream->readUint32("seed");
	if(versionMinor >=  72)
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
	for(int i=0; i<32; ++i)
	{
		stream->writeUint8(allyTeamNumbers[i], "allyTeamNumber");
	}
	stream->writeLeaveSection();
	stream->writeUint8(allyTeamsFixed, "allyTeamsFixed");
	stream->writeEnterSection("winningConditions");
	stream->writeUint32(winningConditions.size(), "size");
	int n=0;
	for(std::list<boost::shared_ptr<WinningCondition> >::const_iterator i=winningConditions.begin(); i!=winningConditions.end(); ++i)
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
	for(int i=0; i<32; ++i)
	{
		stream->readEnterSection(i);
		players[i].load(stream, versionMinor);
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
	for(int i=0; i<32; ++i)
	{
		stream->writeEnterSection(i);
		players[i].save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}



Sint32 GameHeader::getNumberOfPlayers() const
{
	return numberOfPlayers;
}



void GameHeader::setNumberOfPlayers(Sint32 players)
{
	numberOfPlayers=players;
}



Sint32 GameHeader::getGameLatency() const
{
	return gameLatency;
}



void GameHeader::setGameLatency(Sint32 latency)
{
	gameLatency = latency;
}



Uint8 GameHeader::getOrderRate() const
{
	return orderRate;
}



void GameHeader::setOrderRate(Uint8 aorderRate)
{
	orderRate = aorderRate;
}



BasePlayer& GameHeader::getBasePlayer(const int n)
{
	assert(n<32 && n>=0);
	return players[n];
}



const BasePlayer& GameHeader::getBasePlayer(const int n) const
{
	assert(n<32 && n>=0);
	return players[n];
}



Uint8 GameHeader::getAllyTeamNumber(int teamNumber)
{
	return allyTeamNumbers[teamNumber];
}



void GameHeader::setAllyTeamNumber(int teamNumber, Uint8 allyTeam)
{
	allyTeamNumbers[teamNumber]=allyTeam;
}



bool GameHeader::areAllyTeamsFixed()
{
	return allyTeamsFixed;
}



void GameHeader::setAllyTeamsFixed(bool fixed)
{
	allyTeamsFixed = fixed;
}



std::list<boost::shared_ptr<WinningCondition> >& GameHeader::getWinningConditions()
{
	return winningConditions;
}



Uint32 GameHeader::getRandomSeed() const
{
	return seed;
}


void GameHeader::setRandomSeed(Uint32 nseed)
{
	seed = nseed;
}



bool GameHeader::isMapDiscovered() const
{
	return mapDiscovered;
}



void GameHeader::setMapDiscovered(bool discovered)
{
	mapDiscovered=discovered;
}
