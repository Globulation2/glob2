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


#ifndef YOGServerBannedIPListManager_h
#define YOGServerBannedIPListManager_h

#include <string>
#include <map>
#include "SDL_net.h"
#include "boost/date_time/posix_time/posix_time.hpp"

///This class stores and records YOGPlayerStoredInfo for the server
class YOGServerBannedIPListManager
{
public:
	///Constructs a YOGServerBannedIPListManager, reads from the database
	YOGServerBannedIPListManager();

	///Updates this YOGServerBannedIPListManager, periodically saving
	void update();

	///Adds the given IP address to the list of IP's banned for however long
	void addBannedIP(const std::string& bannedIP, boost::posix_time::ptime unban_time);
	
	///Returns true if the given IP address is in the list of ones banned
	bool isIPBanned(const std::string& bannedIP);

	///This stores the player infos in a file
	void saveBannedIPList();

	///This loads the player infos from a file
	void loadBannedIPList();
private:
	bool modified;
	int saveCountdown;
	std::map<std::string, boost::posix_time::ptime> bannedIPs;
};



#endif
