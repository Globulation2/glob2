/*
  Copyright (C) 2007 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef Minimap_h
#define Minimap_h

#include "GraphicContext.h"
#include "Game.h"

class Game;

///This class is used to represent a minimap
class Minimap
{
public:
	enum MinimapMode
	{
		///In this mode, FOW and discovered zone is hidden
		HideFOW,
		///In this mode, FOW and discovered zones are ignored
		///and everything is shown
		ShowFOW,
	};

	///Construct a minimap to be drawn at the given cordinates, and the given size, provided that
	///some of that size is a border
	Minimap(bool nox, int menuWidth, int gameWidth, int xOffset, int yOffset, int width, int height, MinimapMode minimapMode);

	~Minimap();

	///Sets the game assocciatted with the minimap
	void setGame(Game& game);

	///Draws the minimap
	void draw(int localteam, int viewportX, int viewportY, int viewportW, int viewportH);

	///This tells whether the given on-screen cordinates are inside the minimap itself
	bool insideMinimap(int x, int y);

	///This converts the given on-screen cordinate (provided its within the minimap itself)
	///to a cordinate on the map. The nx and ny variables are the on-screen cordinates,
	///the x and y variables are where the map cordinates will be placed.
	void convertToMap(int nx, int ny, int& x, int& y);

	///This converts the given map cordinates to the closest on-screen cordinate
	void convertToScreen(int nx, int ny, int& x, int& y);

	///This resest the minimap drawing
	void resetMinimapDrawing();

	///Enable or disable fog of war
	void setMinimapMode(MinimapMode mode);

private:
	///Computes the minimap positioning
	void computeMinimapPositioning();

	///Refreshes a range of rows on the screen, handles wrapping
	void refreshPixelRows(int start, int end, int localteam);

	/// Computes the colors for positions in the given row
	void computeColors(int row, int localteam);

	bool noX;
	int menuWidth;
	int gameWidth;
	int xOffset;
	int yOffset;
	int width;
	int height;
	int update_row;
	int offset_x;
	int offset_y;
	int mini_x;
	int mini_y;
	int mini_w;
	int mini_h;
	int mini_offset_x;
	int mini_offset_y;
	MinimapMode minimapMode;

	Game* game;

	///Converts x & y to a position in the color map
	int position(int x, int y) { return (x * game->map.getH() + y); }

	DrawableSurface *surface;
};


#endif
