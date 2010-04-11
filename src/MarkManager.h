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

#ifndef MarkManager_h
#define MarkManager_h

#include "GraphicContext.h"

class Game;

///This class represents a mark on the screen. Players are able to mark
///places on the map that show briefly to other players. This class
///manages and draws those marks
class Mark
{
public:
	///Construct a Mark. The px and py cordinates are on the map, not on the screen
	///r, g, and b are colors and time is how long the Mark is to stay on the screen
	Mark(int px, int py, GAGCore::Color color, const int time=50);

	///Construct an empty mark
	Mark();

protected:
	friend class MarkManager;
	///x and y here indicate the x and y screen cordinates
	void draw(int x, int y, float scale);
	///This draws the mark in a minimap where s is the size of the minimap (in pixels),
	///local is the local team number, x and y are the locations of the minimap in
	///pixels, and g is the game
	void drawInMinimap(int s, int local, int x, int y, Game& game);
	///Draws this mark on the screen, where viewport x and viewport y are the
	///positions of the viewport and game is the game
	void drawInMainView(int viewportX, int viewportY, Game& game);
	int showTicks;
	int totalTime;
private:
	int px;
	int py;
	GAGCore::Color color;
};


///The job of this class is to handle Marks.
class MarkManager
{
public:
	///Construct a MarkManager
	MarkManager();
	
	///Draw all marks
	void drawAll(int localTeam, int minimapX, int minimapY, int minimapSize, int viewportX, int viewportY, Game& game);

	///Add another mark to the manager
	void addMark(const Mark& mark);
private:
	std::vector<Mark> marks;
};


#endif
