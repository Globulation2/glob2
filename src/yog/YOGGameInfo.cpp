// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "YOGGameInfo.h"
#include "Stream.h"

YOGGameInfo::YOGGameInfo()
{
	gameID=0;
	gameState = GameOpen;
	playersJoined = 0;
	aiJoined = 0;
	numberOfTeams = 0;
}



YOGGameInfo::YOGGameInfo(const std::string& gameName, Uint16 gameID)
	: gameID(gameID), gameName(gameName), gameState(GameOpen), aiJoined(0)
{
	playersJoined = 0;
	numberOfTeams = 0;
}



void YOGGameInfo::setGameName(const std::string& newGameName)
{
	gameName = newGameName;
}


	
std::string YOGGameInfo::getGameName() const
{
	return gameName;
}



void YOGGameInfo::setGameID(Uint16 id)
{
	gameID=id;
}



Uint16 YOGGameInfo::getGameID() const
{
	return gameID;
}



YOGGameInfo::GameState YOGGameInfo::getGameState() const
{
	return gameState;
}


	
void YOGGameInfo::setGameState(const YOGGameInfo::GameState& state)
{
	gameState = state;
}



void YOGGameInfo::setPlayersJoined(Uint8 nplayersJoined)
{
	playersJoined = nplayersJoined;
}



Uint8 YOGGameInfo::getPlayersJoined() const
{
	return playersJoined;
}



void YOGGameInfo::setAIJoined(Uint8 naiJoined)
{
	aiJoined = naiJoined;
}
	


Uint8 YOGGameInfo::getAIJoined() const
{
	return aiJoined;
}



void YOGGameInfo::setMapName(const std::string& nmapName)
{
	mapName = nmapName;
}



std::string YOGGameInfo::getMapName() const
{
	return mapName;
}



void YOGGameInfo::setNumberOfTeams(Uint8 nnumberOfTeams)
{
	numberOfTeams = nnumberOfTeams;
}



Uint8 YOGGameInfo::getNumberOfTeams() const
{
	return numberOfTeams;
}



void YOGGameInfo::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("YOGGameInfo");
	stream->writeUint16(gameID, "gameID");
	stream->writeText(gameName, "gameName");
	stream->writeUint8(static_cast<Uint8>(gameState), "gameState");
	stream->writeUint8(playersJoined, "playersJoined");
	stream->writeUint8(aiJoined, "aiJoined");
	stream->writeText(mapName, "mapName");
	stream->writeUint8(numberOfTeams, "numberOfTeams");
	stream->writeLeaveSection();
}



void YOGGameInfo::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("YOGGameInfo");
	gameID=stream->readUint16("gameID");
	gameName=stream->readText("gameName");
	gameState=static_cast<GameState>(stream->readUint8("gameState"));
	playersJoined=stream->readUint8("playersJoined");
	aiJoined=stream->readUint8("aiJoined");
	mapName=stream->readText("mapName");
	numberOfTeams=stream->readUint8("numberOfTeams");
	stream->readLeaveSection();
}


	
bool YOGGameInfo::operator==(const YOGGameInfo& rhs) const
{
	if(gameName == rhs.gameName && gameID == rhs.gameID && gameState == rhs.gameState && playersJoined == rhs.playersJoined && aiJoined == rhs.aiJoined && mapName == rhs.mapName)
	{
		return true;
	}
	else
	{
		return false;
	}
	return false;
}

	
	
bool YOGGameInfo::operator!=(const YOGGameInfo& rhs) const
{
	if(gameName != rhs.gameName || gameID != rhs.gameID || gameState!=rhs.gameState || playersJoined != rhs.playersJoined || aiJoined != rhs.aiJoined || mapName != rhs.mapName)
	{
		return true;
	}
	else
	{
		return false;
	}
	return false;
}

