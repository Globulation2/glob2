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

#include "YOGPlayerInfo.h"
#include "SDL_net.h"


YOGPlayerInfo::YOGPlayerInfo()
{
	playerID=0;
}



YOGPlayerInfo::YOGPlayerInfo(const std::string& playerName, Uint16 playerID)
	: playerID(playerID), playerName(playerName)
{

}



void YOGPlayerInfo::setPlayerName(const std::string& newPlayerName)
{
	playerName = newPlayerName;
}


	
std::string YOGPlayerInfo::getPlayerName() const
{
	return playerName;
}



void YOGPlayerInfo::setPlayerID(Uint16 id)
{
	playerID=id;
}



Uint16 YOGPlayerInfo::getPlayerID() const
{
	return playerID;
}



void YOGPlayerInfo::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("YOGPlayerInfo");
	stream->writeUint16(playerID, "playerID");
	stream->writeText(playerName, "playerName");
	stream->writeLeaveSection();
}



void YOGPlayerInfo::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("YOGPlayerInfo");
	playerID=stream->readUint16("playerID");
	playerName=stream->readText("playerName");
	stream->readLeaveSection();
}


	
bool YOGPlayerInfo::operator==(const YOGPlayerInfo& rhs) const
{
	if(playerName == rhs.playerName)
	{
		return true;
	}
	else
	{
		return false;
	}
	return false;
}

	
	
bool YOGPlayerInfo::operator!=(const YOGPlayerInfo& rhs) const
{
	if(playerName != rhs.playerName)
	{
		return true;
	}
	else
	{
		return false;
	}
	return false;
}
