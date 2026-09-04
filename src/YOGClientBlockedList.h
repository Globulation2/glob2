// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef YOGClientBlockedList_h
#define YOGClientBlockedList_h

#include <string>
#include <set>

///This holds a player-end blocked list
class YOGClientBlockedList
{
public:
	YOGClientBlockedList(const std::string& username);
	
	///Loads from the blocked list text file
	void load();
	
	///Saves to the blocked list text file
	void save();
	
	///Adds a player as blocked
	void addBlockedPlayer(const std::string& name);
	
	///Returns true if the given player is blocked
	bool isPlayerBlocked(const std::string& name);
	
	///Removes a player from the blocked list
	void removeBlockedPlayer(const std::string& name);
	
	///Returns a set containing all blocked players
	const std::set<std::string>& getBlockedPlayers() const;
private:
	std::set<std::string> blockedPlayers;
	std::string username;
};


#endif
