// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <cstddef>
#include <optional>
#include <vector>
//#include "GraphicContext.h" // just to get Color, really this should only be in GameGUI

namespace GAGCore
{
	struct Color;
}

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

	enum ClickType {
		CT_DELETE=0,
		CT_AREA=1,
		CT_NO_RESOURCE_GROWTH=2
	};

	/*! Pixel layout of the brush tool panel, in tool-local coordinates whose
		origin is the (x,y) passed to draw(). A row of mode buttons sits above a
		grid of figure buttons:

		    y=0               +--------+--------+
		                      |  ADD   |  DEL   |   MODE_ROW_HEIGHT
		    y=MODE_ROW_HEIGHT +--+--+--+--+-----+
		                      | 0| 1| 2| 3|         FIGURE_CELL_SIZE
		                      +--+--+--+--+
		                      | 4| 5| 6| 7|         FIGURE_CELL_SIZE
		    y=HEIGHT          +--+--+--+--+
		                      x=0        x=WIDTH

		draw() and hitTest() are the only readers. They must agree, or the drawn
		grid and the clickable grid drift apart. The mode row occupies its 36px
		even when addRemoveEnabled is false and draw() leaves it blank — the
		figure grid's position does not depend on it. */
	static constexpr int MODE_ROW_HEIGHT = 36;
	//! One button per selectable Mode, i.e. MODE_ADD and MODE_DEL (not MODE_NONE).
	static constexpr int MODE_BUTTON_COUNT = 2;
	static constexpr int FIGURE_COLUMNS = 4;
	static constexpr int FIGURE_ROWS = 2;
	static constexpr int FIGURE_CELL_SIZE = 32;
	//! Full extent of the panel; also the size the containing widget must have.
	static constexpr int WIDTH = FIGURE_COLUMNS * FIGURE_CELL_SIZE;
	static constexpr int HEIGHT = MODE_ROW_HEIGHT + FIGURE_ROWS * FIGURE_CELL_SIZE;
	static constexpr int MODE_BUTTON_WIDTH = WIDTH / MODE_BUTTON_COUNT;
	//! The mode sprites are narrower than their button; this centres them.
	static constexpr int MODE_BUTTON_SPRITE_INSET = 16;
	//! Number of brush figures, one per cell of the figure grid.
	static constexpr unsigned BRUSH_COUNT = FIGURE_COLUMNS * FIGURE_ROWS;

	//! The button under a pixel, as returned by hitTest.
	struct Hit
	{
		enum Kind { ModeButton, FigureButton };
		Kind kind;
		//! ModeButton: the Mode to select. FigureButton: a figure in [0, BRUSH_COUNT).
		unsigned value;
	};

protected:
	unsigned figure;
	Mode mode;
	bool addRemoveEnabled;
public:
	BrushTool();
	//! Draw the brush tool and its actual state at a given coordinate 
	void draw(int x, int y);
	/*! Map a tool-local pixel to the button under it, or nullopt when the pixel
		is outside the panel. Pure layout — applies no policy; handleClick is
		what honours addRemoveEnabled. Out-of-range coordinates are a normal
		nullopt result rather than an error: GameGUI's flag panel forwards every
		click below its zone-type strip, so clicks landing in that strip arrive
		here with negative y. */
	static std::optional<Hit> hitTest(int x, int y);
	/*! Handle a click at a tool-local coordinate: select the mode and/or figure
		under it. A click with no mode selected yet selects MODE_ADD even when it
		falls outside the panel — GameGUI's flag panel depends on this, because
		clicking a zone-type button must leave the brush usable, and those clicks
		reach us with negative y (see hitTest). */
	void handleClick(int x, int y);
	//! Deselect any brush
	void unselect(void) { mode = MODE_NONE; }
	//! Set default selection
	void defaultSelection(void) { mode = MODE_ADD; }

	//! Draw the actual brush (not the brush tool)
	void drawBrush(int x, int y, int viewportX, int viewportY, int originalX=-1, int originalY=-1, bool onlines=false);
	void drawBrush(int x, int y, GAGCore::Color c, int viewportX, int viewportY, int originalX=-1, int originalY=-1, bool onlines=false);
	//! Return the mode of the brush
	unsigned getType(void) { return static_cast<unsigned>(mode); }
	//! Set the mode of the brush
	void setType(Mode m) { mode = m; }
	//! Return the id of the actual figure, always in [0, BRUSH_COUNT).
	unsigned getFigure(void) { return figure; }
	/*! Set the id of the actual figure; f must be in [0, BRUSH_COUNT).
		Sole mutation point for figure — handleClick routes through here too,
		so the range invariant is enforced in one place. */
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
	static bool getBrushValue(unsigned figure, int x, int y, int centerX, int centerY, int originalX=0, int originalY=0);
};

inline std::optional<BrushTool::Hit> BrushTool::hitTest(int x, int y)
{
	if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
		return std::nullopt;
	if (y < MODE_ROW_HEIGHT)
	{
		// Buttons run left to right in Mode order, starting at MODE_NONE + 1.
		return Hit{ Hit::ModeButton, static_cast<unsigned>(x / MODE_BUTTON_WIDTH) + 1 };
	}
	const int column = x / FIGURE_CELL_SIZE;
	const int row = (y - MODE_ROW_HEIGHT) / FIGURE_CELL_SIZE;
	return Hit{ Hit::FigureButton, static_cast<unsigned>(row * FIGURE_COLUMNS + column) };
}

namespace Utilities
{
	class BitArray;
}

class Map;

//! Accumulator that can store brush and return the resulting bitmap
class BrushAccumulator
{
public:
	BrushAccumulator();

	//! Dimension of the resulting bitmap
	struct AreaDimensions
	{
		int centerX, centerY;
		int minX, minY, maxX, maxY;
		
		AreaDimensions() { minX = minY = maxX = maxY = centerX = centerY = 0; }
	};
	
	int firstX;
	int firstY;
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

