// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

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
