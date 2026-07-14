// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "YOGPlayerSessionInfo.h"
#include "Stream.h"
#include "Version.h"


YOGPlayerSessionInfo::YOGPlayerSessionInfo()
{
	playerID=0;
}



YOGPlayerSessionInfo::YOGPlayerSessionInfo(const std::string& playerName, Uint16 playerID)
	: playerID(playerID), playerName(playerName)
{

}



void YOGPlayerSessionInfo::setPlayerName(const std::string& newPlayerName)
{
	playerName = newPlayerName;
}


	
std::string YOGPlayerSessionInfo::getPlayerName() const
{
	return playerName;
}



void YOGPlayerSessionInfo::setPlayerID(Uint16 id)
{
	playerID=id;
}



Uint16 YOGPlayerSessionInfo::getPlayerID() const
{
	return playerID;
}



const YOGPlayerStoredInfo& YOGPlayerSessionInfo::getPlayerStoredInfo() const
{
	return stored;
}



void YOGPlayerSessionInfo::setPlayerStoredInfo(const YOGPlayerStoredInfo& info)
{
	stored = info;
}



void YOGPlayerSessionInfo::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("YOGPlayerSessionInfo");
	stream->writeUint16(playerID, "playerID");
	stream->writeText(playerName, "playerName");
	stored.encodeData(stream);
	stream->writeLeaveSection();
}



void YOGPlayerSessionInfo::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("YOGPlayerSessionInfo");
	playerID=stream->readUint16("playerID");
	playerName=stream->readText("playerName");
	stored.decodeData(stream, VERSION_MINOR);
	stream->readLeaveSection();
}


	
bool YOGPlayerSessionInfo::operator==(const YOGPlayerSessionInfo& rhs) const
{
	if(playerName == rhs.playerName && playerID == rhs.playerID && stored == rhs.stored)
	{
		return true;
	}
	else
	{
		return false;
	}
	return false;
}

	
	
bool YOGPlayerSessionInfo::operator!=(const YOGPlayerSessionInfo& rhs) const
{
	if(playerName != rhs.playerName || playerID != rhs.playerID || stored != rhs.stored)
	{
		return true;
	}
	else
	{
		return false;
	}
	return false;
}
