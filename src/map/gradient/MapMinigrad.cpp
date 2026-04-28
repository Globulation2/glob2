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


// 5x5 minigrad direction queries (directionFromMinigrad, directionByMinigrad)

bool Map::directionFromMinigrad(Uint8 miniGrad[25], int *dx, int *dy, const bool strict, bool verbose) const
{
	Uint8 max;
	Uint8 mxd; // max in direction
	Uint32 maxs[8];
	
	max=mxd=miniGrad[1+1*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[0+2*5]);
		UPDATE_MAX(max,miniGrad[0+1*5]);
		UPDATE_MAX(max,miniGrad[0+0*5]);
		UPDATE_MAX(max,miniGrad[1+0*5]);
		UPDATE_MAX(max,miniGrad[2+0*5]);
	}
	maxs[0]=(max<<8)|mxd;
	max=mxd=miniGrad[3+1*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[2+0*5]);
		UPDATE_MAX(max,miniGrad[3+0*5]);
		UPDATE_MAX(max,miniGrad[4+0*5]);
		UPDATE_MAX(max,miniGrad[4+1*5]);
		UPDATE_MAX(max,miniGrad[4+2*5]);
	}
	maxs[1]=(max<<8)|mxd;
	max=mxd=miniGrad[3+3*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[4+2*5]);
		UPDATE_MAX(max,miniGrad[4+3*5]);
		UPDATE_MAX(max,miniGrad[4+4*5]);
		UPDATE_MAX(max,miniGrad[3+4*5]);
		UPDATE_MAX(max,miniGrad[2+4*5]);
	}
	maxs[2]=(max<<8)|mxd;
	max=mxd=miniGrad[1+3*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[2+4*5]);
		UPDATE_MAX(max,miniGrad[1+4*5]);
		UPDATE_MAX(max,miniGrad[0+4*5]);
		UPDATE_MAX(max,miniGrad[0+3*5]);
		UPDATE_MAX(max,miniGrad[0+2*5]);
	}
	maxs[3]=(max<<8)|mxd;
	
	
	max=mxd=miniGrad[2+1*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[1+0*5]);
		UPDATE_MAX(max,miniGrad[2+0*5]);
		UPDATE_MAX(max,miniGrad[3+0*5]);
	}
	maxs[4]=(max<<8)|mxd;
	max=mxd=miniGrad[3+2*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[4+1*5]);
		UPDATE_MAX(max,miniGrad[4+2*5]);
		UPDATE_MAX(max,miniGrad[4+3*5]);
	}
	maxs[5]=(max<<8)|mxd;
	max=mxd=miniGrad[2+3*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[1+4*5]);
		UPDATE_MAX(max,miniGrad[2+4*5]);
		UPDATE_MAX(max,miniGrad[3+4*5]);
	}
	maxs[6]=(max<<8)|mxd;
	max=mxd=miniGrad[1+2*5];
	if (max && max!=255)
	{
		max=1;
		UPDATE_MAX(max,miniGrad[0+1*5]);
		UPDATE_MAX(max,miniGrad[0+2*5]);
		UPDATE_MAX(max,miniGrad[0+3*5]);
	}
	maxs[7]=(max<<8)|mxd;
	
	int centerg=miniGrad[2+2*5];
	centerg=(centerg<<8)|centerg;
	int maxg=0;
	int maxd=8;
	bool good=false;
	if (strict)
	{
		for (int d=0; d<8; d++)
		{
			int g=maxs[d];
			if (g>centerg)
				good=true;
			if (maxg<=g)
			{
				maxg=g;
				maxd=d;
			}
		}
	}
	else
	{
		for (int d=0; d<8; d++)
		{
			int g=maxs[d];
			if (g && g!=centerg)
				good=true;
			if (maxg<=g)
			{
				maxg=g;
				maxd=d;
			}
		}
	}
	
	if (verbose)
	{
		if (verbose)
			printf("miniGrad (%d):\n", strict);
		for (int ry=0; ry<5; ry++)
		{
			for (int rx=0; rx<5; rx++)
			if (verbose)
				printf("%4d", miniGrad[rx+ry*5]);
			if (verbose)
				printf("\n");
		}
		if (verbose)
		{
			printf("maxs:\n");
			for (int d=0; d<8; d++)
				printf("%4d.%4d (%d)\n", maxs[d]>>8, maxs[d]&0xFF, maxs[d]);
			printf("max=%4d.%4d (%d), d=%d, good=%d\n", maxs[maxd]>>8, maxs[maxd]&0xFF, maxs[maxd], maxd, good);
		};
	}
	
	if (!good)
		return false;
	
	int stdd;
	if (maxd<4)
		stdd=(maxd<<1);
	else if (maxd!=8)
		stdd=1+((maxd-4)<<1);
	else
		stdd=8;
	
	//printf("stdd=%4d\n", maxd);
	
	Unit::dxDyFromDirection(stdd, dx, dy);
	return true;
}

bool Map::directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int *dx, int *dy, const Uint8 *gradient, bool strict, bool verbose) const
{
	Uint8 miniGrad[25];
	miniGrad[2+2*5]=gradient[x+y*w];
	for (int di=0; di<16; di++)
	{
		int rx=tabFar[di][0];
		int ry=tabFar[di][1];
		int xg = x + rx;
		int yg = y + ry;
		int g=gradient[coordToIndex(xg, yg)];
		if (g==0 || g==255 || isFreeForGroundUnit(xg, yg, canSwim, teamMask))
			miniGrad[rx+ry*5+12]=g;
		else
			miniGrad[rx+ry*5+12]=0;
	}
	for (int di=0; di<8; di++)
	{
		int rx=tabClose[di][0];
		int ry=tabClose[di][1];
		int xg = x + rx;
		int yg = y + ry;
		int g=gradient[coordToIndex(xg, yg)];
		if (g==0 || isFreeForGroundUnit(xg, yg, canSwim, teamMask))
			miniGrad[rx+ry*5+12]=g;
		else
			miniGrad[rx+ry*5+12]=0;
	}
	if (verbose)
		printf("directionByMinigrad global %d\n", canSwim);
	return directionFromMinigrad(miniGrad, dx, dy, strict, verbose);
}

bool Map::directionByMinigrad(Uint32 teamMask, bool canSwim, int x, int y, int bx, int by, int *dx, int *dy, Uint8 localGradient[1024], bool strict, bool verbose) const
{
	Uint8 miniGrad[25];
	for (int ry=0; ry<5; ry++)
		for (int rx=0; rx<5; rx++)
		{
			int gx=(x+rx-2)&wMask;
			int gy=(y+ry-2)&hMask;
			int lx=(x-bx+15+rx-2)&wMask;
			int ly=(y-by+15+ry-2)&hMask;
			//printf("+r=(%d, %d), b=(%d, %d), g=(%d, %d), l=(%d, %d)\n", rx, ry, bx, by, gx, gy, lx, ly);
			if (lx==wMask)
			{
				gx=(gx+1)&wMask;
				lx=0;
			}
			else if (lx==32)
			{
				gx=(gx-1)&wMask;
				lx=31;
			}
			if (ly==hMask)
			{
				gy=(gy+1)&hMask;
				ly=0;
			}
			else if (ly==32)
			{
				gy=(gy-1)&hMask;
				ly=31;
			}
			assert(lx>=0);
			assert(ly>=0);
			assert(lx<32);
			assert(ly<32);
			int g=localGradient[lx+ly*32];
			//printf("|r=(%d, %d), b=(%d, %d), g=(%d, %d), l=(%d, %d), g=%d\n", rx, ry, bx, by, gx, gy, lx, ly, g);
			if (g==0 || g==255 || (rx==2 && ry==2) || isFreeForGroundUnit(gx, gy, canSwim, teamMask))
				miniGrad[rx+ry*5]=g;
			else
				miniGrad[rx+ry*5]=0;
		}
	for (int ry=1; ry<=3; ry++)
		for (int rx=1; rx<=3; rx++)
			if (miniGrad[rx+ry*5]==255)
			{
				int gx=(x+rx-2)&wMask;
				int gy=(y+ry-2)&hMask;
				if (!isFreeForGroundUnit(gx, gy, canSwim, teamMask))
					miniGrad[rx+ry*5]=0;
			}
	if (verbose)
		printf("directionByMinigrad local %d\n", canSwim);
	return directionFromMinigrad(miniGrad, dx, dy, strict, verbose);
}


