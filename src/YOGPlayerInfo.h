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

#ifndef __YOGPlayerInfo_h
#define __YOGPlayerInfo_h

#include <string>
#include "Stream.h"

///This class contains information related to a player connected to YOG
class YOGPlayerInfo
{
public:
	///Construct an empty YOGPlayerInfo
	YOGPlayerInfo();

	///Construct a YOGPlayerInfo
	YOGPlayerInfo(const std::string& playerName, Uint16 id);

	///Sets the name of the player
	void setPlayerName(const std::string& playerName);
	
	///Returns the name of the player
	std::string getPlayerName() const;

	///Sets the unique player ID
	void setPlayerID(Uint16 id);
	
	///Returns the unique player ID
	Uint16 getPlayerID() const;

	///Encodes this YOGPlayerInfo into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGPlayerInfo from a bit stream
	void decodeData(GAGCore::InputStream* stream);
	
	///Test for equality between two YOGPlayerInfo
	bool operator==(const YOGPlayerInfo& rhs) const;
	bool operator!=(const YOGPlayerInfo& rhs) const;
private:
	Uint16 playerID;
	std::string playerName;
};

#endif
