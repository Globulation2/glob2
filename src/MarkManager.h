// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include "EngineTiming.h"
#include "GraphicContext.h"

class Game;

//! Length (in pixels at scale 1.0) of each cross-arm in the four-line
//! "+" decoration drawn around a Mark's pulsing circle. See MarkManager.cpp.
static constexpr int MARK_LINE_LENGTH_PX = 8;

//! Inset (in pixels at scale 1.0) from the circle edge to where each
//! cross-arm starts. Half of MARK_LINE_LENGTH_PX so the line is centred
//! on the circle's tangent point. See MarkManager.cpp.
static constexpr int MARK_LINE_OFFSET_PX = 4;

///This class represents a mark on the screen. Players are able to mark
///places on the map that show briefly to other players. This class
///manages and draws those marks
class Mark
{
public:
	///Construct a Mark. The px and py cordinates are on the map, not on the screen
	///r, g, and b are colors and time is how long the Mark is to stay on the screen
	Mark(int px, int py, GAGCore::Color color, const int time=MARK_DEFAULT_LIFETIME_TICKS);

	///Construct an empty mark
	Mark();

private:
	friend class MarkManager;
	///Advances the mark's lifetime by one engine tick. Called once per
	///MarkManager::drawAll iteration — the draw* methods are pure rendering
	///and must not mutate state. After tick(), the mark is expired iff
	///showTicks <= 0.
	void tick() { showTicks -= 1; }
	///True once the mark's lifetime has elapsed and it should be erased.
	///Use <= rather than == to be robust against any future caller passing
	///an odd lifetime or any future code path that skips a tick.
	bool expired() const { return showTicks <= 0; }
	///x and y here indicate the x and y screen cordinates
	void draw(int x, int y, float scale) const;
	///This draws the mark in a minimap where s is the size of the minimap (in pixels),
	///local is the local team number, x and y are the locations of the minimap in
	///pixels, and g is the game
	void drawInMinimap(int s, int local, int x, int y, Game& game) const;
	///Draws this mark on the screen, where viewport x and viewport y are the
	///positions of the viewport and game is the game
	void drawInMainView(int viewportX, int viewportY, Game& game) const;
	int showTicks;
	int totalTime;
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
