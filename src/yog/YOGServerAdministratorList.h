// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include <set>
#include <string>

///This class reads the administrator list
class YOGServerAdministratorList
{
public:
	///This will read the administrator list
	YOGServerAdministratorList();
	
	///Returns true if the given username is an administrator, false otherwise
	bool isAdministrator(const std::string& playerName);
	
	///Adds the specificed user as an administrator
	void addAdministrator(const std::string& playerName);
	
	///Removes the specified user from the administrator list
	void removeAdministrator(const std::string& playerName);
private:
	///Saves the list of administrators
	void save();
	
	///Loads the list of administrators
	void load();

	std::set<std::string> admins;
};


