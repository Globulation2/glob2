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

#ifndef __YOGGameInfo_h
#define __YOGGameInfo_h

#include <string>

///This class summarizes hosted game information on the YOG server.
///It does not include information about the game itself, just how
///its hosted, a name, and some other preamble that the GUI can use
///to filter and sort games before a user decides to join one.
class YOGGameInfo
{
public:
	///Construct an empty YOGGameInfo
	YOGGameInfo();

	///Sets the name of the game
	void setGameName(const std::string& gameName);
	
	///Returns the name of the game
	std::string getGameName() const;

	///Sets the unique game ID of the game
	void setGameID(Uint16 id);
	
	///Returns the unique game ID of the game
	Uint16 getGameID() const;

	///Encodes this YOGGameInfo into a bit stream
	Uint8* encodeData() const;
	
	///Gets the length of the data returned by encodeData
	Uint16 getDataLength() const;

	///Decodes this YOGGameInfo from a bit stream
	bool decodeData(const Uint8 *data, int dataLength);
	
	///Test for equality between two YOGGameInfo
	bool operator==(const YOGGameInfo& rhs) const;
	bool operator!=(const YOGGameInfo& rhs) const;
private:
	Uint16 gameID;
	std::string gameName;
};

#endif
