// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "MapHeader.h"

//! Repeating a map in x and y at game setup. The map is copied rx by ry
//! times over the torus, every copy keeps its colonies, and the colonies
//! are dealt round robin to the chosen number of teams; colonies beyond an
//! equal share are dropped.
namespace MapTiling
{
	//! Largest side the engine supports.
	static constexpr int MAX_MAP_SIDE = 512;

	//! True when the factors or the team count change the map as loaded.
	bool isActive(int rx, int ry, int teams, int mapTeams);
	//! Colonies the tiled map contains.
	int colonyCount(int mapTeams, int rx, int ry);
	//! Colonies actually placed: perTeam for every team, at most an equal share of the total.
	int placedColonyCount(int total, int teams, int perTeam);
	//! Team that receives colony n, or -1 when it is dropped. The placed colonies are spread evenly over all of them.
	int teamForColony(int n, int total, int teams, int perTeam);
	//! What the chooser needs to know about a map file without loading it: its header and size in tiles.
	struct MapInfo
	{
		MapHeader header;
		int w = 0, h = 0;
		bool valid = false;
	};
	//! Reads the header and the map size of a map file; valid is false when the file cannot be read.
	MapInfo readMapInfo(const std::string& fileName);
	//! True when the map can be repeated rx by ry with `teams` colonies of `swarms` swarms each.
	bool fits(const MapInfo& map, int rx, int ry, int teams, int swarms);
	//! Adjusts rx, ry, teams and swarms to the map, keeping the colony count whenever any
	//! repeat within the size limit reaches it, then the swarms per colony.
	void adjust(const MapInfo& map, int& rx, int& ry, int& teams, int& swarms);
	//! Writes the repeated map as a map file next to the user's own maps and returns its header,
	//! or a header with no teams when the map could not be read or written.
	MapHeader writeTiledMap(const MapHeader& source, int rx, int ry, int teams, int perTeam);
	//! Repeat factors usable for a side of `side` tiles: powers of two within MAX_MAP_SIDE.
	std::vector<int> repeatOptions(int side);
	//! The header of the tiled map: `teams` teams whose type follows the
	//! colonies round robin, colours spread around the wheel.
	MapHeader tiledHeader(const MapHeader& source, int rx, int ry, int teams);
}
