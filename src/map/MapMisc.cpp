// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "MapInternal.h"

#include <FileManager.h>



// Miscellaneous helpers: checkSum, warpDist*, isInLocalGradient, dumpGradient

Uint32 Map::checkSum(bool heavy)
{
	Uint32 cs=size;
	if (heavy)
	{
		for (const auto& c: cases)
		{
			cs+=
				c.terrain +
				c.building +
				c.ressource.getUint32() +
				c.groundUnit +
				c.airUnit +
				c.forbidden +
				c.scriptAreas;
			cs=rotl1(cs);
		}
	};
	return cs;
}

Sint32 Map::warpDist1d(int p, int q, int l)
{
	Sint32 d=abs(p-q);
	d%=l;
	if (d>l/2)
		d=l-d;
	return d;
}

Sint32 Map::warpDistSquare(int px, int py, int qx, int qy)
{
	Sint32 dx=warpDist1d(px,qx,w);
	Sint32 dy=warpDist1d(py,qy,h);
	return ((dx*dx)+(dy*dy));
}

Sint32 Map::warpDistMax(int px, int py, int qx, int qy)
{
	Sint32 dx=warpDist1d(px,qx,w);
	Sint32 dy=warpDist1d(py,qy,h);
	if (dx>dy)
		return dx;
	else
		return dy;
}

Sint32 Map::warpDistSum(int px, int py, int qx, int qy)
{
	Sint32 dx=warpDist1d(px,qx,w);
	Sint32 dy=warpDist1d(py,qy,h);
	return dx + dy;
}


bool Map::isInLocalGradient(int ux, int uy, int bx, int by)
{
	Sint32 dx=warpDist1d(ux,bx,w);
	Sint32 dy=warpDist1d(uy,by,h);
	if (dx>dy)
	{
		if (dx<LOCAL_GRID_CENTER)
			return true;
		if (dx>LOCAL_GRID_CENTER)
			return false;

		return ((bx+LOCAL_GRID_CENTER) & wMask)==(ux & wMask);
	}
	else if (dx<dy)
	{
		if (dy<LOCAL_GRID_CENTER)
			return true;
		if (dy>LOCAL_GRID_CENTER)
			return false;

		return ((by+LOCAL_GRID_CENTER) & wMask)==(uy & wMask);
	}
	else
	{
		if (dx<LOCAL_GRID_CENTER)
			return true;
		if (dx>LOCAL_GRID_CENTER)
			return false;

		return (((bx+LOCAL_GRID_CENTER) & wMask)==(ux & wMask)) && (((by+LOCAL_GRID_CENTER) & wMask)==(uy & wMask));
	}
}

void Map::dumpGradient(Uint8 *gradient, const std::string filename)
{
	FILE *fp = globalContainer->fileManager->openFP(filename, "wb");
	if (fp)
	{
		fprintf(fp, "P5 %d %d 255\n", w, h);
		fwrite(gradient, w, h, fp);
		fclose(fp);
	}
}


