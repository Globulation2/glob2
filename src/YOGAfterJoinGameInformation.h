/*
  Copyright (C) 2008 Bradley Arsenault

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


#ifndef __YOGAfterJoinGameInformation_h
#define __YOGAfterJoinGameInformation_h

#include "GameHeader.h"
#include "MapHeader.h"
#include "NetReteamingInformation.h"
#include "SDL2/SDL_net.h"
#include <string>

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

///This class holds all of the data that is sent to a player immeddiettly after they join a game,
///including all of the information that isn't visible directly from the lobby
class YOGAfterJoinGameInformation
{
public:
	YOGAfterJoinGameInformation();
	
	///Sets map header
	void setMapHeader(const MapHeader& header);
	
	///Returns the map header
	const MapHeader& getMapHeader() const;
	
	///Sets the game header
	void setGameHeader(const GameHeader& header);
	
	///Returns the game header
	const GameHeader& getGameHeader() const;

	///Sets the reteaming information
	void setReteamingInformation(const NetReteamingInformation& reteam);
	
	///Returns the reteaming information
	const NetReteamingInformation& getReteamingInformation() const;
	
	///Sets the latency adjustment
	void setLatencyAdjustment(Uint8 latency);
	
	///Returns the latency adjustment
	Uint8 getLatencyAdjustment() const;
	
	///Sets the IP address of the game-router for this game
	void setGameRouterIP(const std::string& ip);
	
	///Returns the ip address of the game router for this game
	const std::string& getGameRouterIP() const;
	
	///Sets the fileID for the games map, so that players can download it if needed
	void setMapFileID(Uint16 fileID);
	
	///Returns the fileID for the games map, so that players can download it if needed
	Uint16 getMapFileID() const;

	///Encodes this YOGAfterJoinGameInfo into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGAfterJoinGameInfo from a bit stream
	void decodeData(GAGCore::InputStream* stream);
	
	///Test for equality between two YOGAfterJoinGameInfo
	bool operator==(const YOGAfterJoinGameInformation& rhs) const;
	bool operator!=(const YOGAfterJoinGameInformation& rhs) const;
private:
	MapHeader map;
	GameHeader game;
	NetReteamingInformation reteam;
	Uint8 latency;
	std::string routerIP;
	Uint32 fileID;
};

#endif
