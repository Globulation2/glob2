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

#ifndef YOGClientRatedMapList_h
#define YOGClientRatedMapList_h

#include <string>
#include <set>

///This class holds the list of rated maps
class YOGClientRatedMapList
{
public:
	///Loads the list of rated maps
	YOGClientRatedMapList(const std::string& username);

	///Sets a map that the user has rated by the user
	void addRatedMap(const std::string& mapname);

	///Returns true if the given map has been rated by the user, false otherwise
	bool isMapRated(const std::string& mapname);

private:
	///Saves the list
	void save();
	///Loads the list
	void load();

	std::set<std::string> maps;
	std::string username;
};

#endif
