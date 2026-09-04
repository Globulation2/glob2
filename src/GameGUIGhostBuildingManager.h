// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef GameGUIGhostBuildingManager_h
#define GameGUIGhostBuildingManager_h

#include <vector>
#include <tuple>
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
	std::vector<std::tuple<std::string, int, int> > buildings;
};

#endif
