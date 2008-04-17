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

#ifndef YOGServerAdministratorList_h
#define YOGServerAdministratorList_h

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


#endif
