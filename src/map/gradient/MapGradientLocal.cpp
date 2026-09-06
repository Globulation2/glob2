// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "BuildingType.h"
#include "Unit.h"
#include "MapInternal.h"



// updateLocalGradient (32x32 building gradient) + helpers

namespace {
/** Helper for updateLocalGradient */
void fillGradientRectangle(Uint8* gradient, int posW, int posH) {
	for (int dy=0; dy<posH; dy++) {
		int yyi=clip_0_31(LOCAL_GRID_CENTER+dy);
		for (int dx=0; dx<posW; dx++)
		{
			int xxi=clip_0_31(LOCAL_GRID_CENTER+dx);
			gradient[xxi+(yyi<<LOCAL_GRID_SHIFT)]=GRADIENT_AT_GOAL;
		}
	}
}
} // namespace

void Map::updateLocalGradient(Building *building, bool canSwim)
{
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

	Uint8 gradient[LOCAL_GRID_AREA];

	// 1. INITIALIZATION of gradient[]:
	// 1a. Set all values to GRADIENT_UNREACHABLE (meaning 'far away, but not inaccessible').
	memset(gradient, GRADIENT_UNREACHABLE, LOCAL_GRID_AREA);

	bool isWarFlag=false;
	bool isClearingFlag=false;
	if(building->type->isVirtual && building->type->zonable[WARRIOR])
		isWarFlag=true;
	if(building->type->isVirtual && building->type->zonable[WORKER])
		isClearingFlag=true;

	// 1b. Set values at target building to GRADIENT_AT_GOAL.
	if (building->type->isVirtual && !building->type->zonable[WORKER])
	{
		assert(!building->type->zonableForbidden);
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			int yyi=clip_0_31(LOCAL_GRID_CENTER+yi);
			for (int xi=-r; xi<=r; xi++)
			{
				if (yi2+(xi*xi)<=r2)
				{
					int xxi=clip_0_31(LOCAL_GRID_CENTER+xi);
					gradient[xxi+(yyi<<LOCAL_GRID_SHIFT)]=GRADIENT_AT_GOAL;
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
			int yyi=clip_0_31(LOCAL_GRID_CENTER+yi);
			for (int xi=-r; xi<=r; xi++)
			{
				if (yi2+(xi*xi)<=r2)
				{
					size_t addr = coordToIndex(posX+w+xi, posY+h+yi);
					if(cases[addr].ressource.type != NO_RES_TYPE && building->clearingRessources[cases[addr].ressource.type])
					{
						int xxi=clip_0_31(LOCAL_GRID_CENTER+xi);
						gradient[xxi+(yyi<<LOCAL_GRID_SHIFT)]=GRADIENT_AT_GOAL;
					}
				}
			}
		}
	}
	else
		fillGradientRectangle(gradient, posW, posH);

	// 1c. Set values at inaccessible areas to GRADIENT_FORBIDDEN.
	// Here g=Global(map axis), l=Local(map axis)

	for (int yl=0; yl<LOCAL_GRID_W; yl++)
	{
		int wyl=(yl<<LOCAL_GRID_SHIFT);
		int yg=(yl+posY-LOCAL_GRID_CENTER)&hMask;
		int wyg=w*yg;
		for (int xl=0; xl<LOCAL_GRID_W; xl++)
		{
			int xg=(xl+posX-LOCAL_GRID_CENTER)&wMask;
			const Case& c=cases[wyg+xg];
			int wyx=wyl+xl;

			if (c.building==NOGBID)
			{
				if (c.forbidden&teamMask)
					gradient[wyx] = GRADIENT_FORBIDDEN;
				else if (c.ressource.type!=NO_RES_TYPE && !(isClearingFlag && gradient[wyx]==GRADIENT_AT_GOAL))
					gradient[wyx] = GRADIENT_FORBIDDEN;
				else if(immobileUnits[wyx] != 255)
					gradient[wyx] = GRADIENT_FORBIDDEN;
				else if (!canSwim && isWater(xg, yg))
					gradient[wyx] = GRADIENT_FORBIDDEN;
			}
			else
			{
				if (c.building==bgid)
				{
					gradient[wyx] = GRADIENT_AT_GOAL;
				}
				//Warflags don't consider enemy buildings an obstacle
				else if(!isWarFlag || (1<<Building::GIDtoTeam(c.building)) & (building->owner->allies))
					gradient[wyx] = GRADIENT_FORBIDDEN;
				else if(gradient[wyx]!=GRADIENT_AT_GOAL)
					gradient[wyx] = GRADIENT_UNREACHABLE;
			}
		}
	}

	// 2. NEED TO UPDATE? Check boundary conditions to see if they have changed.
	// I commented this out, because the tgtGradient is not initialized
	// in the first runs: leading to an unconditional jump
	// todo: write a real fix

/*
	bool change = false;

	for (int i=0; i<LOCAL_GRID_AREA; i++) {
		// The boundary conditions - do they match?
		if (gradient[i]==GRADIENT_FORBIDDEN || gradient[i]==GRADIENT_AT_GOAL || tgtGradient[i]==GRADIENT_FORBIDDEN || tgtGradient[i]==GRADIENT_AT_GOAL) {
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
		// Spiral around the building footprint corner; start one cell NW of (CENTER, CENTER),
		// stride posW+1 so we wrap around the whole footprint.
		bool reachable = spiralFindNonZero(gradient,
		                                    LOCAL_GRID_CENTER - 1, LOCAL_GRID_CENTER - 1,
		                                    posW + 1,
		                                    LOCAL_GRID_W - 1, LOCAL_GRID_W - 1,
		                                    LOCAL_GRID_SHIFT);
		building->locked[canSwim] = !reachable;
		if (!reachable)
		{
			memcpy(tgtGradient, gradient, LOCAL_GRID_AREA); // Don't leave tgt as-is (it might be dirty)
			return;
		}
	}
	else
		building->locked[canSwim]=false;

	// 4. PROPAGATION of gradient values.
	propagateLocalGradient32(gradient);

	// 5. WRITEBACK (because of the 'any change'-computation).
	memcpy(tgtGradient, gradient, LOCAL_GRID_AREA);
}


// Chamfer-dilate the 32x32 local gradient buffer in-place. Two depth passes; each pass
// runs an outward sweep from the center then an inward sweep from a corner, with each
// sweep tracing a back-and-forth spiral that visits every cell. At every cell, the
// value is raised toward max(8-neighbors) - 1 (clamped at 1, i.e. GRADIENT_UNREACHABLE);
// 0 (obstacle) and 255 (source) are preserved. OOB neighbors are masked out via the
// LOCAL_GRID_W bit overflow trick — `xpart & LOCAL_GRID_W` flags x=-1 (sign-bit pattern
// has bit 5 set) and x=32, and likewise for y via `ypart & LOCAL_GRID_AREA`.
//
// Two passes ("depth") cover obstacles that fold the propagation path back on itself.
void propagateLocalGradient32(Uint8* gradient) {
	for (int depth=0; depth<2; depth++)
	{
		for (int down=0; down<2; down++)
		{
			int x, y, dis, die, ddi;
			if (down)
			{
				x=0;
				y=0;
				dis=LOCAL_GRID_W-1;
				die=1;
				ddi=-2;
			}
			else
			{
				x=LOCAL_GRID_CENTER;
				y=LOCAL_GRID_CENTER;
				dis=1;
				die=LOCAL_GRID_W-1;
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
							assert(x>=0);
							assert(y>=0);
							assert(x<LOCAL_GRID_W);
							assert(y<LOCAL_GRID_W);

							int wy=(y<<LOCAL_GRID_SHIFT);
							Uint8 max=gradient[wy+x];
							if (max && max!=GRADIENT_AT_GOAL)
							{
								for (int dy=-LOCAL_GRID_W; dy<=LOCAL_GRID_W; dy+=LOCAL_GRID_W) {
									int ypart = wy+dy;
									if (ypart & LOCAL_GRID_AREA) continue; // OOB row
									for (int dx=-1; dx<=1; dx++) {
										int xpart = x+dx;
										if (xpart & LOCAL_GRID_W) continue; // OOB column
										UPDATE_MAX(max,gradient[ypart+xpart]);
									}
								}
								assert(max);
								if (max==GRADIENT_UNREACHABLE)
									gradient[wy+x]=GRADIENT_UNREACHABLE;
								else
									gradient[wy+x]=max-1;
							}

							if (bi==0)
							{
								switch (ai)
								{
									case 0: x++; break;
									case 1: y++; break;
									case 2: x--; break;
									case 3: y--; break;
								}
							}
							else
							{
								switch (ai)
								{
									case 0: y++; break;
									case 1: x++; break;
									case 2: y--; break;
									case 3: x--; break;
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
}

