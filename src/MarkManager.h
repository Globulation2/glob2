// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

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
