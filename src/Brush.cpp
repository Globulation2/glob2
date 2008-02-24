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

#include "Brush.h"
#include "BitArray.h"
#include "GlobalContainer.h"
#include "Map.h"
#include <GraphicContext.h>

BrushTool::BrushTool()
{
	figure = 0;
	mode = MODE_NONE;
}

void BrushTool::draw(int x, int y)
{
	globalContainer->gfx->drawSprite(x+16, y, globalContainer->brush, 0);
	globalContainer->gfx->drawSprite(x+64+16, y, globalContainer->brush, 1);
	if (mode)
		globalContainer->gfx->drawSprite(x+(static_cast<int>(mode)-1)*64+16, y, globalContainer->gamegui, 22);
	for (unsigned i=0; i<8; i++)
	{
		int decX = (i%4)*32;
		int decY = 32*(i/4)+36;
		globalContainer->gfx->drawSprite(x+decX, y+decY, globalContainer->brush, 2+i);
		if ((mode != MODE_NONE) && (figure == i))
			globalContainer->gfx->drawSprite(x+decX, y+decY, globalContainer->gamegui, 22);
	}
}

void BrushTool::handleClick(int x, int y)
{
	if (mode == MODE_NONE)
		mode = MODE_ADD;
	if (y>0)
		if (y<36)
		{
			mode = static_cast<Mode>((x/64)+1);
		}
		else if (y<36+64)
		{
			y -= 36;
			figure = (y/32)*4 + ((x/32)%4);
		}
}

void BrushTool::drawBrush(int x, int y, int viewportX, int viewportY, bool onlines)
{
	/* We use 2/3 intensity to indicate removing areas.  This was
		formerly 78% intensity, which was bright enough that it was hard
		to notice any difference, so the brightness has been lowered. */
	int i = ((mode == MODE_ADD) ? 255 : 170);
	drawBrush(x, y, Color(i,i,i), viewportX, viewportY, onlines);
}

void BrushTool::drawBrush(int x, int y, GAGCore::Color c, int viewportX, int viewportY, bool onlines)
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
	for (int cx = 0; cx < w; cx++)
	{
		for (int cy = 0; cy < h; cy++)
		{
			// TODO: the brush is wrong, but without lookuping viewport in game gui, there is no way to know this
			if (getBrushValue(figure, cx, cy, viewportX + (x / cell_size), viewportY + (y / cell_size)))
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

int BrushTool::getBrushDimXMinus(unsigned figure)
{
	if (getBrushWidth(figure) % 2)
		return getBrushWidth(figure) / 2;
	else
		return getBrushWidth(figure) / 2;
}

int BrushTool::getBrushDimXPlus(unsigned figure)
{
	if (getBrushWidth(figure) % 2)
		return (getBrushWidth(figure) / 2) + 1;
	else
		return getBrushWidth(figure) / 2;
}

int BrushTool::getBrushDimYMinus(unsigned figure)
{
	if (getBrushHeight(figure) % 2)
		return getBrushHeight(figure) / 2;
	else
		return getBrushHeight(figure) / 2;
}

int BrushTool::getBrushDimYPlus(unsigned figure)
{
	if (getBrushHeight(figure) % 2)
		return (getBrushHeight(figure) / 2) + 1;
	else
		return getBrushHeight(figure) / 2;
}
/*
int BrushTool::getBrushDimX(unsigned figure)
{
	if (getBrushWidth(figure) % 2)
		return (getBrushWidth(figure) - 1) >> 1;
	else
		return getBrushWidth(figure) >> 1;
}

int BrushTool::getBrushDimY(unsigned figure)
{
	if (getBrushHeight(figure) % 2)
		return (getBrushHeight(figure) - 1) >> 1;
	else
		return getBrushHeight(figure) >> 1;
}*/


bool BrushTool::getBrushValue(unsigned figure, int x, int y, int centerX, int centerY)
{
	int brush0[] = { 	1 };
	int brush1[] = { 	0, 1, 0,
						1, 1, 1,
						0, 1, 0 };
	int brush2[] = {	1, 0, 0,
						0, 1, 0,
						0, 0, 1, };
	int brush3[] = {	0, 0, 1,
						0, 1, 0,
						1, 0, 0, };
	/*int brush4[] = { 	1, 0, 1, 0, 1,
						0, 1, 0, 1, 0,
						1, 0, 1, 0, 1,
						0, 1, 0, 1, 0,
						1, 0, 1, 0, 1 };
	int brush5[] = { 	1, 0, 1, 0, 1,
						0, 0, 0, 0, 0,
						1, 0, 1, 0, 1,
						0, 0, 0, 0, 0,
						1, 0, 1, 0, 1 };
						*/
	int brush4[] = { 	1, 0, 1, 0,
						0, 1, 0, 1,
						1, 0, 1, 0,
						0, 1, 0, 1 };
	int brush5[] = { 	1, 0, 1, 0,
						0, 0, 0, 0,
						1, 0, 1, 0,
						0, 0, 0, 0 };
	int brush6[] = { 	1, 1, 1,
						1, 1, 1,
						1, 1, 1 };
	int brush7[] = {	1, 1, 1, 1, 1,
						1, 1, 1, 1, 1,
						1, 1, 1, 1, 1,
						1, 1, 1, 1, 1,
						1, 1, 1, 1, 1 };
	int *brushes[BRUSH_COUNT] = { brush0, brush1, brush2, brush3, brush4, brush5, brush6, brush7 };
	
	assert(figure < BRUSH_COUNT);
	int w = getBrushWidth(figure);
	int h = getBrushHeight(figure);
	assert(x < w);
	assert(y < h);
	
	if ((figure == 4) || (figure == 5))
	{
		// do alignment on specific brush (4 and 5)
		if (centerX % 2 != 0)
			x++;
		if (centerY % 2 != 0)
			y++;
	}
	
	return (brushes[figure][(y%h) * getBrushWidth(figure) + (x%w)] != 0);
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
					if (BrushTool::getBrushValue(applications[i].figure, x, y, applications[i].x, applications[i].y))
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
