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


#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "LogFileManager.h"
#include "Unit.h"
#include "MapInternal.h"

#include <algorithm>
#include <valarray>
#include <Stream.h>
#include <queue>


// Viewport coordinate conversions

void Map::mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY) const
{
	int x = (mx - viewportX + w) & wMask;
	int y = (my - viewportY + h) & hMask;
	if (x > (w - 16))
		x-=w;
	if (y > (h - 16))
		y-=h;
	*px=x<<5;
	*py=y<<5;
}

void Map::mapCaseToDisplayableVector(int mx, int my, int *px, int *py, int viewportX, int viewportY, int screenW, int screenH) const
{
	int x = (mx - viewportX + w) & wMask;
	int y = (my - viewportY + h) & hMask;
	if (x > (w/2 + (screenW/64)))
		x-=w;
	if (y > (h/2 + (screenH/64)))
		y-=h;
	*px=x<<5;
	*py=y<<5;
}

void Map::displayToMapCaseAligned(int mx, int my, int *px, int *py, int viewportX, int viewportY) const
{
	*px=((mx>>5)+viewportX)&getMaskW();
	*py=((my>>5)+viewportY)&getMaskH();
}

void Map::displayToMapCaseUnaligned(int mx, int my, int *px, int *py, int viewportX, int viewportY) const
{
	*px=(((mx+16)>>5)+viewportX)&getMaskW();
	*py=(((my+16)>>5)+viewportY)&getMaskH();
}

void Map::cursorToBuildingPos(int mx, int my, int buildingWidth, int buildingHeight, int *px, int *py, int viewportX, int viewportY) const
{
	int tempX, tempY;
	if (buildingWidth&0x1)
		tempX=((mx)>>5)+viewportX;
	else
		tempX=((mx+16)>>5)+viewportX;
			
	if (buildingHeight&0x1)
		tempY=((my)>>5)+viewportY;
	else
		tempY=((my+16)>>5)+viewportY;
		
	*px=tempX&getMaskW();
	*py=tempY&getMaskH();
}

void Map::buildingPosToCursor(int px, int py, int buildingWidth, int buildingHeight, int *mx, int *my, int viewportX, int viewportY) const
{
	mapCaseToDisplayable(px, py, mx, my, viewportX, viewportY);
	*mx+=buildingWidth*16;
	*my+=buildingHeight*16;
}


