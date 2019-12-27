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

#ifndef GameGUIGhostBuildingManager_h
#define GameGUIGhostBuildingManager_h

#include <vector>
#include <boost/tuple/tuple.hpp>
#include <string>

class Game;

///GameGUIGhostBuildingManager causes 'ghosts' of buildings to be drawn on the map in
///the time inbetween when the user clicks the button to construct a building, and when
///the building is actually constructed. This time is 0 for local games, but for online
///games it can be as high as 2 seconds with bad connections.
class GameGUIGhostBuildingManager
{
public:
	///Constructs the manager
	GameGUIGhostBuildingManager(Game& game);

	///Adds the building to be drawn, and the x and y positions on the map
	void addBuilding(const std::string& type, int x, int y);

	///Returns true if there is a ghost building covering the given square
	bool isGhostBuilding(int x, int y, int w, int h);

	///Removes the building from the list
	void removeBuilding(int x, int y);

	///Draws to the map
	void drawAll(int viewportX, int viewportY, int localTeamNo);
private:
	Game& game;
	std::vector<boost::tuple<std::string, int, int> > buildings;
};

#endif
