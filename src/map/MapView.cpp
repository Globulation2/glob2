// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

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


