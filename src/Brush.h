/*
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

#ifndef __BRUSH_H
#define __BRUSH_H

#include <vector>
#include "GraphicContext.h" // just to get Color, really this should only be in GameGUI

//! A click of the brush tool to the map
struct BrushApplication
{
	BrushApplication(int x, int y, int figure) { this->x=x; this->y=y; this->figure=figure; }
	int x;
	int y;
	int figure;
};

//! A brush tool is the GUI and the settings container for a brush's operations
class BrushTool
{
public:
	enum Mode
	{
		MODE_NONE = 0,
		MODE_ADD,
		MODE_DEL
	};
	
protected:
	unsigned figure;
	Mode mode;
	bool addRemoveEnabled;
	
public:
	BrushTool();
	//! Draw the brush tool and its actual state at a given coordinate 
	void draw(int x, int y);
	//! Handle the click for given coordinate. Select correct mode and figure, accept negative coordinates for y
	void handleClick(int x, int y);
	//! Deselect any brush
	void unselect(void) { mode = MODE_NONE; }
	//! Set default selection
	void defaultSelection(void) { mode = MODE_ADD; }

	//! Draw the actual brush (not the brush tool)
	void drawBrush(int x, int y, int viewportX, int viewportY, bool onlines=false);
	void drawBrush(int x, int y, GAGCore::Color c, int viewportX, int viewportY, bool onlines=false);
	//! Return the mode of the brush
	unsigned getType(void) { return static_cast<unsigned>(mode); }
	//! Set the mode of the brush
	void setType(Mode m) { mode = m; }
	//! Return the id of the actual figure
	unsigned getFigure(void) { return figure; }
	//! Set the id of the actual figure
	void setFigure(unsigned f);
	
	//! Return the full width of a brush
	static int getBrushWidth(unsigned figure);
	//! Return the full height of a brush
	static int getBrushHeight(unsigned figure);
	
	//! This enables or disables the ability to select add / remove. Used by the map editor because logically you can't "remove" Terrain and such.
	void setAddRemoveEnabledState(bool value);
	//! Return the left extend of the brush (not counting its center cell)
	static int getBrushDimXMinus(unsigned figure);
	//! Return the right extend of the brush (not counting its center cell)
	static int getBrushDimXPlus(unsigned figure);
	//! Return the bottom extend of the brush (not counting its center cell)
	static int getBrushDimYMinus(unsigned figure);
	//! Return the top extend of the brush (not counting its center cell)
	static int getBrushDimYPlus(unsigned figure);
	//! Return the value of a pixel of a given brush, also pass the x and y coordinates for alignment
	static bool getBrushValue(unsigned figure, int x, int y, int centerX, int centerY);
};

namespace Utilities
{
	class BitArray;
}

class Map;

//! Accumulator that can store brush and return the resulting bitmap
class BrushAccumulator
{
public:
	//! Dimension of the resulting bitmap
	struct AreaDimensions
	{
		int centerX, centerY;
		int minX, minY, maxX, maxY;
		
		AreaDimensions() { minX = minY = maxX = maxY = centerX = centerY = 0; }
	};
	
protected:
	//! The list of brush applications
	std::vector<BrushApplication> applications;
	//! The actual dimensions of the resulting applications
	AreaDimensions dim;
	
public:
	//! Apply this brush to the brush application vector and extend dim as required
	void applyBrush(const BrushApplication &brush, const Map* map);
	//! Clear the vector of brush applications
	void clear(void) { applications.clear(); }
	//! Return a bitmap which is the result of the fusion of all accumulated brush applications
	bool getBitmap(Utilities::BitArray *array, AreaDimensions *dim, const Map *map);
	//! Return the area surface
	unsigned getAreaSurface(void);
	//! Return the number of brush applied
	size_t getApplicationCount(void) { return applications.size(); }
};

#endif
