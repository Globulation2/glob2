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
