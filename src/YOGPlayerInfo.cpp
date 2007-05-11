/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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



Uint8* YOGPlayerInfo::encodeData() const
{
	Uint16 length = getDataLength();
	Uint8* data = new Uint8[length];
	Uint32 pos = 0;
	SDLNet_Write16(playerID, data+pos);
	pos+=2;
	data[pos] = playerName.size();
	pos+=1;
	std::copy(playerName.begin(), playerName.end(), data+pos);
	return data;
}


	
Uint16 YOGPlayerInfo::getDataLength() const
{
	return 2 + 1 + playerName.size();
}



bool YOGPlayerInfo::decodeData(const Uint8 *data, int dataLength)
{
	Uint8 pos = 0;
	playerID = SDLNet_Read16(data+pos);
	pos+=2;
	//Read in the playerName
	Uint8 playerNameLength = data[pos];
	pos+=1;
	for(int i=0; i<playerNameLength; ++i)
	{
		playerName+=static_cast<char>(data[pos]);
		pos+=1;
	}
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
