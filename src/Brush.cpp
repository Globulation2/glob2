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
			globalContainer->gfx->drawCircle(x+16, y+16, figure*32+16, 0, 0, 255);
		else
			globalContainer->gfx->drawCircle(x+16, y+16, figure*32+16, 255, 0, 0);
	}
	else
	{
		int l = (figure-4)*32;
		if (mode == MODE_ADD)
			globalContainer->gfx->drawRect(x-l, y-l, 2*l+32, 2*l+32, 0, 0, 255);
		else
			globalContainer->gfx->drawRect(x-l, y-l, 2*l+32, 2*l+32, 255, 0, 0);
	}
}
