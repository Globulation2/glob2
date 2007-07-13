/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
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
	///Construct a minimap to be drawn at the given cordinates, and the given size
	Minimap(int px, int py, int size);

	~Minimap();

	///Sets the game assocciatted with the minimap
	void setGame(Game& game);

	///Draws the minimap
	void draw(int localteam);
	
	///Renders the full minimap
	void renderAllRows(int localteam);
private:
	///Refreshes a range of rows on the screen, handles wrapping
	void refreshPixelRows(int start, int end);

	///Renders a single row on the colorMap, based on the given y cordinate
	void renderRow(int y, int localteam);
	
	/// Returns the value at the given point, by interpolating between colors
	GAGCore::Color getColor(double xpos, double ypos);

	/// Returns a value by interpolating between given values
	int interpolate(double mu, int y1, int y2);

	
	int px;
	int py;
	int size;
	int row;
	int offset_x;
	int offset_y;
	Game* game;
	
	enum ColorMode
	{
		TerrainWater=0,
		TerrainSand,
		TerrainGrass,
		Self,
		Ally,
		Enemy,
		SelfFOW,
		AllyFOW,
		EnemyFOW,
		RessourceColorStart,
	};
	
	///This represents the colors for every position on the map (not pixels)
	std::vector<ColorMode> colorMap;
	///This represents the color for a particular mode
	std::vector<GAGCore::Color> colors;
	///Converts x & y to a position in the color map
	int position(int x, int y) { return (x * game->map.getH() + y); }
	
	DrawableSurface *surface;
};


#endif
