// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Brush.h"
#include "BitArray.h"
#include "GlobalContainer.h"
#include "Map.h"

BrushTool::BrushTool()
{
	addRemoveEnabled=true;
	figure = 0;
	mode = MODE_NONE;
}

void BrushTool::draw(int x, int y)
{
	if(addRemoveEnabled)
	{
		globalContainer->gfx->drawSprite(x+16, y, globalContainer->brush, 0);
		globalContainer->gfx->drawSprite(x+64+16, y, globalContainer->brush, 1);
		if (mode)
			globalContainer->gfx->drawSprite(x+(static_cast<int>(mode)-1)*64+16, y, globalContainer->gamegui, 22);
	}
	for (unsigned i=0; i<8; i++)
	{
		int decX = (i%4)*32;
		int decY = 32*(i/4)+36;
		globalContainer->gfx->drawSprite(x+decX, y+decY, globalContainer->brush, 2+i);
		if ((mode != MODE_NONE) && (figure == i))
			globalContainer->gfx->drawSprite(x+decX, y+decY, globalContainer->gamegui, 22);
	}
	globalContainer->gfx->finishDrawingSprite(globalContainer->brush, 255);
	globalContainer->gfx->finishDrawingSprite(globalContainer->gamegui, 255);
}

void BrushTool::handleClick(int x, int y)
{
	if (mode == MODE_NONE)
		mode = MODE_ADD;
	if (y>0 && x>0 && x<128)
	{
		if (y<36)
		{
			if(addRemoveEnabled)
				mode = static_cast<Mode>((x/64)+1);
		}
		else if (y<36+64)
		{
			y -= 36;
			figure = (y/32)*4 + ((x/32)%4);
		}
	}
}



void BrushTool::drawBrush(int x, int y, int viewportX, int viewportY, int originalX, int originalY, bool onlines)
{
	/* We use 2/3 intensity to indicate removing areas.  This was
		formerly 78% intensity, which was bright enough that it was hard
		to notice any difference, so the brightness has been lowered. */
	int i = ((mode == MODE_ADD) ? 255 : 170);
	drawBrush(x, y, Color(i,i,i), viewportX, viewportY, originalX, originalY, onlines);
}

void BrushTool::drawBrush(int x, int y, GAGCore::Color c, int viewportX, int viewportY, int originalX, int originalY, bool onlines)
{
	/* It violates good abstraction practices that Brush.cpp knows
		this much about the visual layout of the GUI. */
	x = ((x+(onlines ? 16 : 0)) & ~0x1f) + (!onlines ? 16 : 0);
	y = ((y+(onlines ? 16 : 0)) & ~0x1f) + (!onlines ? 16 : 0);
	int w = getBrushWidth(figure);
	int h = getBrushHeight(figure);
	/* Move x and y from center of focus point to upper left of
	brush shape. */ 
	const int cell_size = 32; // This file should not know this value!!!
	x -= ((cell_size * getBrushDimXMinus(figure)) + (cell_size / 2));
	y -= ((cell_size * getBrushDimYMinus(figure)) + (cell_size / 2));
	const int inset = 2;

	if(originalX == -1)
		originalX = viewportX + (x / cell_size);
	else if(onlines)
		originalX+=1;
	if(originalY == -1)
		originalY = viewportY + (y / cell_size);
	else if(onlines)
		originalY+=1;
	
	for (int cx = 0; cx < w; cx++)
	{
		for (int cy = 0; cy < h; cy++)
		{
			// TODO: the brush is wrong, but without lookuping viewport in game gui, there is no way to know this
			if (getBrushValue(figure, cx, cy, viewportX + (x / cell_size), viewportY + (y / cell_size), originalX, originalY))
			{
				globalContainer->gfx->drawRect(x + (cell_size * cx) + inset, y + (cell_size * cy) + inset, cell_size - inset, cell_size - inset, c);
			}
		}
	}
	
	/* The following code is the old way of doing things.  It is
	kept in case anyone wants to restore it, which might be
	useful for some of the brush shapes. */
	/*
	if (figure < 4)
	{
		int r = (getBrushWidth(figure) + getBrushHeight(figure)) * 8;
		if (mode == MODE_ADD)
			globalContainer->gfx->drawCircle(x, y, r, 255, 255, 255);
		else
			globalContainer->gfx->drawCircle(x, y, r, 200, 200, 200);
	}
	else
	{
		int w = getBrushWidth(figure) * 16;
		int h = getBrushHeight(figure) * 16;
		if (mode == MODE_ADD)
			globalContainer->gfx->drawRect(x-w, y-h, 2*w, 2*h, 255, 255, 255);
		else
			globalContainer->gfx->drawRect(x-w, y-h, 2*w, 2*h, 200, 200, 200);
	}
	*/
}

#define BRUSH_COUNT 8

void BrushTool::setFigure(unsigned f)
{
	assert (figure < BRUSH_COUNT);
	figure = f;
}

int BrushTool::getBrushWidth(unsigned figure)
{
	int dim[BRUSH_COUNT] = { 1, 3, 3, 3, 4, 4, 3, 5};
	assert(figure < BRUSH_COUNT);
	return dim[figure];
}

int BrushTool::getBrushHeight(unsigned figure)
{
	int dim[BRUSH_COUNT] = { 1, 3, 3, 3, 4, 4, 3, 5};
	assert(figure < BRUSH_COUNT);
	return dim[figure];
}

// For odd widths the center cell counts on the Plus side, so Plus = ceil(w/2)
// and Minus = floor(w/2). For even widths the brush is symmetric.
int BrushTool::getBrushDimXMinus(unsigned figure) { return getBrushWidth(figure) / 2; }
int BrushTool::getBrushDimXPlus(unsigned figure)  { return (getBrushWidth(figure) + 1) / 2; }
int BrushTool::getBrushDimYMinus(unsigned figure) { return getBrushHeight(figure) / 2; }
int BrushTool::getBrushDimYPlus(unsigned figure)  { return (getBrushHeight(figure) + 1) / 2; }


bool BrushTool::getBrushValue(unsigned figure, int x, int y, int centerX, int centerY, int originalX, int originalY)
{
	static constexpr int brush0[] = { 1 };
	static constexpr int brush1[] = {
		0, 1, 0,
		1, 1, 1,
		0, 1, 0,
	};
	static constexpr int brush2[] = {
		1, 0, 0,
		0, 1, 0,
		0, 0, 1,
	};
	static constexpr int brush3[] = {
		0, 0, 1,
		0, 1, 0,
		1, 0, 0,
	};
	static constexpr int brush4[] = {
		1, 0, 1, 0,
		0, 1, 0, 1,
		1, 0, 1, 0,
		0, 1, 0, 1,
	};
	static constexpr int brush5[] = {
		1, 0, 1, 0,
		0, 0, 0, 0,
		1, 0, 1, 0,
		0, 0, 0, 0,
	};
	static constexpr int brush6[] = {
		1, 1, 1,
		1, 1, 1,
		1, 1, 1,
	};
	static constexpr int brush7[] = {
		1, 1, 1, 1, 1,
		1, 1, 1, 1, 1,
		1, 1, 1, 1, 1,
		1, 1, 1, 1, 1,
		1, 1, 1, 1, 1,
	};
	static constexpr const int* brushes[BRUSH_COUNT] = {
		brush0, brush1, brush2, brush3, brush4, brush5, brush6, brush7,
	};
	// Brushes 4 and 5 are checkerboard patterns: their cells must be aligned to
	// the parity of the stroke origin so neighbouring strokes tile seamlessly.
	static constexpr bool needsParityAlignment[BRUSH_COUNT] = {
		false, false, false, false, true, true, false, false,
	};

	assert(figure < BRUSH_COUNT);
	int w = getBrushWidth(figure);
	int h = getBrushHeight(figure);
	assert(x < w);
	assert(y < h);

	if (needsParityAlignment[figure])
	{
		if (centerX % 2 == originalX % 2)
			x++;
		if (centerY % 2 == originalY % 2)
			y++;
	}

	return (brushes[figure][(y % h) * getBrushWidth(figure) + (x % w)] != 0);
}

void BrushTool::setAddRemoveEnabledState(bool value)
{
	addRemoveEnabled=value;
}


BrushAccumulator::BrushAccumulator()
{
	firstX=0;
	firstY=0;
}

void BrushAccumulator::applyBrush(const BrushApplication &brush, const Map* map)
{
	if (applications.size() == 0)
	{
		// init dimensions
		dim.centerX = brush.x;
		dim.centerY = brush.y;
		dim.minX = 0 - BrushTool::getBrushDimXMinus(brush.figure);
		dim.maxX = 0 + BrushTool::getBrushDimXPlus(brush.figure);
		dim.minY = 0 - BrushTool::getBrushDimYMinus(brush.figure);
		dim.maxY = 0 + BrushTool::getBrushDimYPlus(brush.figure);
	}
	else
	{
		// consider brush relative to center
		int px = brush.x - dim.centerX;
		int py = brush.y - dim.centerY;
		int mapW = map->getW();
		int mapH = map->getH();
		if (px < -(mapW/2))
			px += mapW;
		else if (px > (mapW/2))
			px -= mapW;
		if (py < -(mapH/2))
			py += mapH;
		else if (py > (mapH/2))
			py -= mapH;
		
		// extend dimensions
		dim.minX = std::min(dim.minX, px - BrushTool::getBrushDimXMinus(brush.figure));
		dim.maxX = std::max(dim.maxX, px + BrushTool::getBrushDimXPlus(brush.figure));
		dim.minY = std::min(dim.minY, py - BrushTool::getBrushDimYMinus(brush.figure));
		dim.maxY = std::max(dim.maxY, py + BrushTool::getBrushDimYPlus(brush.figure));
	}
	
	// and add to vector
	applications.push_back(brush);
}

bool BrushAccumulator::getBitmap(Utilities::BitArray *array, AreaDimensions *dim, const Map *map)
{
	assert(array);
	assert(dim);
	
	*dim = this->dim;

	if (applications.size() > 0)
	{
		// set array size
		int arrayH = dim->maxY - dim->minY;
		int arrayW = dim->maxX - dim->minX;
		size_t size = static_cast<size_t>(arrayW * arrayH);
		array->resize(size, false);
		
		// fill array
		for (size_t i=0; i<applications.size(); ++i)
		{
			for (int y=0; y<BrushTool::getBrushHeight(applications[i].figure); y++)
			{
				for (int x=0; x<BrushTool::getBrushWidth(applications[i].figure); x++)
				{
					int px = applications[i].x - dim->centerX;
					int py = applications[i].y - dim->centerY;
					int mapW = map->getW();
					int mapH = map->getH();
					if (px < -(mapW/2))
						px += mapW;
					else if (px > (mapW/2))
						px -= mapW;
					if (py < -(mapH/2))
						py += mapH;
					else if (py > (mapH/2))
						py -= mapH;
					
					int arrayX = px - dim->minX - BrushTool::getBrushDimXMinus(applications[i].figure) + x;
					int arrayY = py - dim->minY - BrushTool::getBrushDimYMinus(applications[i].figure) + y;
					
					size_t arrayPos = static_cast<size_t>(arrayY * arrayW + arrayX);
					if (BrushTool::getBrushValue(applications[i].figure, x, y, applications[i].x, applications[i].y, firstX, firstY))
						array->set(arrayPos, true);
				}
			}
		}
		return true;
	}
	return false;
}

unsigned BrushAccumulator::getAreaSurface(void)
{
	return (dim.maxX - dim.minX) * (dim.maxY - dim.minY);
}
