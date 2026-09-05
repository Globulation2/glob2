// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"



// Viewport coordinate conversions

void Map::mapCaseToDisplayable(int mx, int my, int *px, int *py, int viewportX, int viewportY) const
{
	int x = (mx - viewportX + w) & wMask;
	int y = (my - viewportY + h) & hMask;
	if (x > (w - HALF_TILE_PX))
		x-=w;
	if (y > (h - HALF_TILE_PX))
		y-=h;
	*px=x<<TILE_PIXEL_SHIFT;
	*py=y<<TILE_PIXEL_SHIFT;
}

void Map::mapCaseToDisplayableVector(int mx, int my, int *px, int *py, int viewportX, int viewportY, int screenW, int screenH) const
{
	int x = (mx - viewportX + w) & wMask;
	int y = (my - viewportY + h) & hMask;
	if (x > (w/2 + (screenW/(TILE_PX*2))))
		x-=w;
	if (y > (h/2 + (screenH/(TILE_PX*2))))
		y-=h;
	*px=x<<TILE_PIXEL_SHIFT;
	*py=y<<TILE_PIXEL_SHIFT;
}

void Map::displayToMapCaseAligned(int mx, int my, int *px, int *py, int viewportX, int viewportY) const
{
	*px=((mx>>TILE_PIXEL_SHIFT)+viewportX)&getMaskW();
	*py=((my>>TILE_PIXEL_SHIFT)+viewportY)&getMaskH();
}

void Map::displayToMapCaseUnaligned(int mx, int my, int *px, int *py, int viewportX, int viewportY) const
{
	*px=(((mx+HALF_TILE_PX)>>TILE_PIXEL_SHIFT)+viewportX)&getMaskW();
	*py=(((my+HALF_TILE_PX)>>TILE_PIXEL_SHIFT)+viewportY)&getMaskH();
}

void Map::cursorToBuildingPos(int mx, int my, int buildingWidth, int buildingHeight, int *px, int *py, int viewportX, int viewportY) const
{
	int tempX, tempY;
	if (buildingWidth&0x1)
		tempX=((mx)>>TILE_PIXEL_SHIFT)+viewportX;
	else
		tempX=((mx+HALF_TILE_PX)>>TILE_PIXEL_SHIFT)+viewportX;

	if (buildingHeight&0x1)
		tempY=((my)>>TILE_PIXEL_SHIFT)+viewportY;
	else
		tempY=((my+HALF_TILE_PX)>>TILE_PIXEL_SHIFT)+viewportY;

	*px=tempX&getMaskW();
	*py=tempY&getMaskH();
}

void Map::buildingPosToCursor(int px, int py, int buildingWidth, int buildingHeight, int *mx, int *my, int viewportX, int viewportY) const
{
	mapCaseToDisplayable(px, py, mx, my, viewportX, viewportY);
	*mx+=buildingWidth*HALF_TILE_PX;
	*my+=buildingHeight*HALF_TILE_PX;
}


