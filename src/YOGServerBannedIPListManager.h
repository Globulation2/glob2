// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

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
