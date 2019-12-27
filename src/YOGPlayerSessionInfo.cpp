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

#include "YOGPlayerSessionInfo.h"
#include "SDL_net.h"
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
