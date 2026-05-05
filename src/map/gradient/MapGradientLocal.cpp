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


// updateLocalGradient (32x32 building gradient) + helpers

namespace {
/** Helper for updateLocalGradient */
void fillGradientRectangle(Uint8* gradient, int posW, int posH) {
	for (int dy=0; dy<posH; dy++) {
		int yyi=clip_0_31(15+dy);
		for (int dx=0; dx<posW; dx++)
		{
			int xxi=clip_0_31(15+dx);
			gradient[xxi+(yyi<<5)]=255;
		}
	}
}

void propagateLocalGradients(Uint8* gradient);
} // namespace

void Map::updateLocalGradient(Building *building, bool canSwim)
{
	localBuildingGradientUpdate++;
	//fprintf(logFile, "updatingLocalGradient (gbid=%d)...\n", building->gid);
	//printf("updatingLocalGradient (gbid=%d)...\n", building->gid);
	assert(building);
	assert(building->type);
	building->dirtyLocalGradient[canSwim]=false;
	int posX=building->posX;
	int posY=building->posY;
	int posW=building->type->width;
	int posH=building->type->height;
	Uint32 teamMask=building->owner->me;
	Uint16 bgid=building->gid;
	
	Uint8 *tgtGradient=building->localGradient[canSwim];

	Uint8 gradient[1024];
 
	// 1. INITIALIZATION of gradient[]:
	// 1a. Set all values to 1 (meaning 'far away, but not inaccessable').
	memset(gradient, 1, 1024);

	bool isWarFlag=false;
	bool isClearingFlag=false;
	if(building->type->isVirtual && building->type->zonable[WARRIOR])
		isWarFlag=true;
	if(building->type->isVirtual && building->type->zonable[WORKER])
		isClearingFlag=true;

	// 1b. Set values at target building to 255 (meaning 'very close'/'at destination').
	if (building->type->isVirtual && !building->type->zonable[WORKER])
	{
		assert(!building->type->zonableForbidden);
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			int yyi=clip_0_31(15+yi);
			for (int xi=-r; xi<=r; xi++)
			{
				if (yi2+(xi*xi)<=r2)
				{
					int xxi=clip_0_31(15+xi);
					gradient[xxi+(yyi<<5)]=255;
				}
			}
		}
	}
	else if (building->type->isVirtual && building->type->zonable[WORKER])
	{
		assert(!building->type->zonableForbidden);
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			int yyi=clip_0_31(15+yi);
			for (int xi=-r; xi<=r; xi++)
			{
				if (yi2+(xi*xi)<=r2)
				{
					size_t addr = coordToIndex(posX+w+xi, posY+h+yi);
					if(cases[addr].ressource.type != NO_RES_TYPE && building->clearingRessources[cases[addr].ressource.type])
					{
						int xxi=clip_0_31(15+xi);
						gradient[xxi+(yyi<<5)]=255;
					}
				}
			}
		}
	}
	else
		fillGradientRectangle(gradient, posW, posH);

	// 1c. Set values at inaccessible areas to 0 (meaning, well, 'inaccessible').
	// Here g=Global(map axis), l=Local(map axis)
	
	for (int yl=0; yl<32; yl++)
	{
		int wyl=(yl<<5);
		int yg=(yl+posY-15)&hMask;
		int wyg=w*yg;
		for (int xl=0; xl<32; xl++)
		{
			int xg=(xl+posX-15)&wMask;
			const Case& c=cases[wyg+xg];
			int wyx=wyl+xl;
			
			if (c.building==NOGBID)
			{
				if (c.forbidden&teamMask)
					gradient[wyx] = 0;
				else if (c.ressource.type!=NO_RES_TYPE && !(isClearingFlag && gradient[wyx]==255))
					gradient[wyx] = 0;
				else if(immobileUnits[wyx] != 255)
					gradient[wyx] = 0;
				else if (!canSwim && isWater(xg, yg))
					gradient[wyx] = 0;
			}
			else
			{
				if (c.building==bgid)
				{
					gradient[wyx] = 255;
				}
				//Warflags don't consider enemy buildings an obstacle
				else if(!isWarFlag || (1<<Building::GIDtoTeam(c.building)) & (building->owner->allies))
					gradient[wyx] = 0;
				else if(gradient[wyx]!=255)
					gradient[wyx] = 1;
			}
		}
	}
	
	// 2. NEED TO UPDATE? Check boundary conditions to see if they have changed.
	// I commented this out, because the tgtGradient is not initialized
	// in the first runs: leading to an unconditional jump
	// todo: write a real fix

/*
	bool change = false;

	for (int i=0; i<1024; i++) {
		// The boundary conditions - do they match?
		if (gradient[i]==0 || gradient[i]==255 || tgtGradient[i]==0 || tgtGradient[i]==255) {
			if (gradient[i] != tgtGradient[i]) {
				if (((gradient[i]+1)&0xFE)==0 ||  // Is either gradient or tgtGradient 0 or 255?
				    ((tgtGradient[i]+1)&0xFE)==0)
				{
					change = true; break;
				}
			}
		}
		if (!change) return; // No need to update; boundary conditions are unchanged.
	}
	if (!change) return; // No need to update; boundary conditions are unchanged.
*/
	// 3. Check that the building is REACHABLE.
	if (!building->type->isVirtual)
	{
		building->locked[canSwim]=true;
		int x=14;
		int y=14;
		int d=posW+1;
		for (int ai=0; ai<4; ai++) //angle-iterator
			for (int mi=0; mi<d; mi++) //move-iterator
			{
				assert(x>=0);
				assert(y>=0);
				assert(x<32);
				assert(y<32);
				
				Uint8 g=gradient[(y<<5)+x];
				//printf("ai=%d, mi=%d, (%d, %d), g=%d\n", ai, mi, x, y, g);
				if (g)
				{
					building->locked[canSwim]=false;
					goto doubleBreak;
				}
				switch (ai)
				{
					case 0:
						x++;
					break;
					case 1:
						y++;
					break;
					case 2:
						x--;
					break;
					case 3:
						y--;
					break;
				}
			}
		
		assert(building->locked[canSwim]);
		localBuildingGradientUpdateLocked++;
		//fprintf(logFile, "...not updatedLocalGradient! building bgid=%d is locked!\n", building->gid);
		//printf("...not updatedLocalGradient! building bgid=%d is locked!\n", building->gid);
		memcpy(tgtGradient, gradient, 1024); // Don't leave gradient as-is (it might be dirty)
		return;
		doubleBreak:;
	}
	else
		building->locked[canSwim]=false;

	// 4. PROPAGATION of gradient values.
	propagateLocalGradients(gradient);

	// 5. WRITEBACK (because of the 'any change'-computation).
	memcpy(tgtGradient, gradient, 1024);
}


namespace {
void propagateLocalGradients(Uint8* gradient) {
	//In this algorithm, "l" stands for one case at Left, "r" for one case at Right, "u" for Up, and "d" for Down.
	for (int depth=0; depth<2; depth++) // With a higher depth, we can have more complex obstacles.
	{
		for (int down=0; down<2; down++)
		{
			int x, y, dis, die, ddi;
			if (down)
			{
				x=0;
				y=0;
				dis=31;
				die=1;
				ddi=-2;
			}
			else
			{
				x=15;
				y=15;
				dis=1;
				die=31;
				ddi=+2;
			}
			
			for (int di=dis; di!=die; di+=ddi) //distance-iterator
			{
				for (int bi=0; bi<2; bi++) //back-iterator
				{
					for (int ai=0; ai<4; ai++) //angle-iterator
					{
						for (int mi=0; mi<di; mi++) //move-iterator
						{
							//printf("di=%d, ai=%d, mi=%d, p=(%d, %d)\n", di, ai, mi, x, y);
							//fprintf(logFile, "di=%d, ai=%d, mi=%d, p=(%d, %d)\n", di, ai, mi, x, y);
							assert(x>=0);
							assert(y>=0);
							assert(x<32);
							assert(y<32);

							int wy=(y<<5);
							Uint8 max=gradient[wy+x];
							if (max && max!=255)
							{
								for (int dy=-32; dy<=32; dy+=32) {
									int ypart = wy+dy;
									if (ypart & (32*32)) continue; // Over- or underflow
									for (int dx=-1; dx<=1; dx++) {
										int xpart = x+dx;
										if (xpart & 32) continue; // Over- or underflow
										UPDATE_MAX(max,gradient[ypart+xpart]);
									}
								}
								// TODO: checkstyle found very long code duplicaitons here
								// src/Map.cpp:3463: warning: Found duplicate of 59 lines in src/Map.cpp, starting from line 3,858
								assert(max);
								if (max==1)
									gradient[wy+x]=1;
								else
									gradient[wy+x]=max-1;
							}

							if (bi==0)
							{
								switch (ai)
								{
									case 0:
										x++;
									break;
									case 1:
										y++;
									break;
									case 2:
										x--;
									break;
									case 3:
										y--;
									break;
								}
							}
							else
							{
								switch (ai)
								{
									case 0:
										y++;
									break;
									case 1:
										x++;
									break;
									case 2:
										y--;
									break;
									case 3:
										x--;
									break;
								}
							}
						}
					}
				}
				if (down)
				{
					x++;
					y++;
				}
				else
				{
					x--;
					y--;
				}
			}
		}
	}
	//printf("...updatedLocalGradient\n");
	//fprintf(logFile, "...updatedLocalGradient\n");
}


} // namespace

