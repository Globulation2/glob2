/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

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

#include "Brush.h"
#include "BitArray.h"
#include "GlobalContainer.h"
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

void BrushTool::drawBrush(int x, int y)
{
	x &= ~0x1f;
	y &= ~0x1f;
	if (figure < 4)
	{
		if (mode == MODE_ADD)
			globalContainer->gfx->drawCircle(x+16, y+16, figure*32+16, 255, 255, 255);
		else
			globalContainer->gfx->drawCircle(x+16, y+16, figure*32+16, 200, 200, 200);
	}
	else
	{
		int l = (figure-4)*32;
		if (mode == MODE_ADD)
			globalContainer->gfx->drawRect(x-l, y-l, 2*l+32, 2*l+32, 255, 255, 255);
		else
			globalContainer->gfx->drawRect(x-l, y-l, 2*l+32, 2*l+32, 200, 200, 200);
	}
}

#define BRUSH_COUNT 8

int BrushTool::getBrushWidth(unsigned figure)
{
	int dim[BRUSH_COUNT] = { 1, 3, 5, 7, 1, 3, 5, 7};
	assert(figure < BRUSH_COUNT);
	return dim[figure];
}

int BrushTool::getBrushHeight(unsigned figure)
{
	int dim[BRUSH_COUNT] = { 1, 3, 5, 7, 1, 3, 5, 7};
	assert(figure < BRUSH_COUNT);
	return dim[figure];
}

int BrushTool::getBrushDimX(unsigned figure)
{
	return (getBrushWidth(figure) - 1) >> 1;
}

int BrushTool::getBrushDimY(unsigned figure)
{
	return (getBrushHeight(figure) - 1) >> 1;
}


bool BrushTool::getBrushValue(unsigned figure, int x, int y)
{
	int brush0[] = { 	1 };
	int brush1[] = { 	0, 1, 0,
						1, 1, 1,
						0, 1, 0 };
	int brush2[] = {	0, 1, 1, 1, 0,
						1, 1, 1, 1, 1,
						1, 1, 1, 1, 1,
						1, 1, 1, 1, 1,
						0, 1, 1, 1, 0 };
	int brush3[] = {	0, 0, 1, 1, 1, 0, 0,
						0, 1, 1, 1, 1, 1, 0,
						1, 1, 1, 1, 1, 1, 1,
						1, 1, 1, 1, 1, 1, 1,
						1, 1, 1, 1, 1, 1, 1,
						0, 1, 1, 1, 1, 1, 0,
						0, 0, 1, 1, 1, 0, 0 };
	int brush4[] = { 	1 };
	int brush5[] = { 	1, 1, 1,
						1, 1, 1,
						1, 1, 1 };
	int brush6[] = {	1, 1, 1, 1, 1,
						1, 1, 1, 1, 1,
						1, 1, 1, 1, 1,
						1, 1, 1, 1, 1,
						1, 1, 1, 1, 1 };
	int brush7[] = {	1, 1, 1, 1, 1, 1, 1,
						1, 1, 1, 1, 1, 1, 1,
						1, 1, 1, 1, 1, 1, 1,
						1, 1, 1, 1, 1, 1, 1,
						1, 1, 1, 1, 1, 1, 1,
						1, 1, 1, 1, 1, 1, 1,
						1, 1, 1, 1, 1, 1, 1, };
	int *brushes[BRUSH_COUNT] = { brush0, brush1, brush2, brush3, brush4, brush5, brush6, brush7 };
	
	assert(figure<BRUSH_COUNT);
	assert(x<getBrushHeight(figure));
	assert(y<getBrushWidth(figure));
	
	return (brushes[figure][y*getBrushWidth(figure)+x] != 0);
}

bool BrushAccumulator::getBitmap(Utilities::BitArray *array, AreaDimensions *dim)
{
	assert(array);
	assert(dim);

	if (applications.size() > 0)
	{
		// Get dimensions
		dim->minX = applications[0].x - BrushTool::getBrushDimX(applications[0].figure);
		dim->maxX = applications[0].x + BrushTool::getBrushDimX(applications[0].figure) + 1;
		dim->minY = applications[0].y - BrushTool::getBrushDimY(applications[0].figure);
		dim->maxY = applications[0].y + BrushTool::getBrushDimY(applications[0].figure) + 1;
		for (size_t i=1; i<applications.size(); ++i)
		{
			dim->minX = std::min<int>(dim->minX, applications[i].x - BrushTool::getBrushDimX(applications[i].figure));
			dim->maxX = std::max<int>(dim->maxX, applications[i].x + BrushTool::getBrushDimX(applications[i].figure) + 1);
			dim->minY = std::min<int>(dim->minY, applications[i].y - BrushTool::getBrushDimY(applications[i].figure));
			dim->maxY = std::max<int>(dim->maxY, applications[i].y + BrushTool::getBrushDimY(applications[i].figure) + 1);
		}
		
		// Fill array
		int arrayH = dim->maxY-dim->minY;
		int arrayW = dim->maxX-dim->minX;
		size_t size = static_cast<size_t>(arrayW * arrayH);
		array->resize(size, false);
		
		for (size_t i=0; i<applications.size(); ++i)
		{
			int realXMin = applications[i].x - BrushTool::getBrushDimX(applications[i].figure);
			int realYMin = applications[i].y - BrushTool::getBrushDimY(applications[i].figure);
			for (int y=0; y<BrushTool::getBrushHeight(applications[i].figure); y++)
			{
				int realY = y + realYMin;
				int arrayY = realY - dim->minY;
				for (int x=0; x<BrushTool::getBrushWidth(applications[i].figure); x++)
				{
					int realX = x + realXMin;
					int arrayX = realX - dim->minX;
					size_t arrayPos = static_cast<size_t>(arrayY * arrayW + arrayX);
					array->set(arrayPos, BrushTool::getBrushValue(applications[i].figure, x, y));
				}
			}
		}
		return true;
	}
	return false;
}
