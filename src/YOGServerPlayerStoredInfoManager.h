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


#ifndef YOGServerPlayerStoredInfoManager_h
#define YOGServerPlayerStoredInfoManager_h

#include "YOGPlayerStoredInfo.h"
#include <string>
#include <map>
#include "SDL2/SDL_net.h"
#include <list>

class YOGServer;

///This class stores and records YOGPlayerStoredInfo for the server
class YOGServerPlayerStoredInfoManager
{
public:
	///Constructs a YOGServerPlayerStoredInfoManager, reads from the database
	YOGServerPlayerStoredInfoManager(YOGServer* server);

	///Updates this YOGServerPlayerStoredInfoManager, periodically saving
	void update();

	///Insure that a YOGPlayerStoredInfo exists for the given username, if it doesn't, this creates one
	void insureStoredInfoExists(const std::string& username);
	
	///Returns true if a player info with the given username exists
	bool doesStoredInfoExist(const std::string& username);
	
	///Returns the player info
	const YOGPlayerStoredInfo& getPlayerStoredInfo(const std::string& username);

	///Sets the player info
	void setPlayerStoredInfo(const std::string& username, const YOGPlayerStoredInfo& info);

	///Returns a list of the banned players
	std::list<std::string> getBannedPlayers();
	
	///This stores the player infos in a file
	void savePlayerInfos();

	///This loads the player infos from a file
	void loadPlayerInfos();
private:
	bool modified;
	int saveCountdown;
	std::map<std::string, YOGPlayerStoredInfo> playerInfos;
	YOGServer* server;
};



#endif
