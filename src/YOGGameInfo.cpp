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

#include "YOGGameInfo.h"


YOGGameInfo::YOGGameInfo()
{
}



void YOGGameInfo::setGameName(const std::string& newGameName)
{
	gameName = newGameName;
}


	
std::string YOGGameInfo::getGameName() const
{
	return gameName;
}



Uint8* YOGGameInfo::encodeData() const
{
	Uint16 length = getDataLength();
	Uint8* data = new Uint8[length];
	Uint32 pos = 0;
	data[pos] = gameName.size();
	pos+=1;
	std::copy(gameName.begin(), gameName.end(), data+pos);
	return data;
}


	
Uint16 YOGGameInfo::getDataLength() const
{
	return 1 + gameName.size();
}



bool YOGGameInfo::decodeData(const Uint8 *data, int dataLength)
{
	Uint8 pos = 0;
	//Read in the gameName
	Uint8 gameNameLength = data[pos];
	pos+=1;
	for(int i=0; i<gameNameLength; ++i)
	{
		gameName+=static_cast<char>(data[pos]);
		pos+=1;
	}
}


	
bool YOGGameInfo::operator==(const YOGGameInfo& rhs) const
{
	if(gameName == rhs.gameName)
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
	if(gameName != rhs.gameName)
	{
		return true;
	}
	else
	{
		return false;
	}
	return false;
}

