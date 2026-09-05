// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "BuildingType.h"
#include "Unit.h"
#include "MapInternal.h"



// updateGlobalGradient(Building*), updateLocalRessources

void Map::updateGlobalGradient(Building *building, bool canSwim)
{
	assert(building);
	assert(building->type);
	int posX=building->posX;
	int posY=building->posY;
	int posW=building->type->width;
	Uint32 teamMask=building->owner->me;
	Uint16 bgid=building->gid;

	Uint8 *gradient=building->globalGradient[canSwim];
	assert(gradient);

	bool isClearingFlag=false;
	bool isWarFlag=false;
	if (building->type->isVirtual && building->type->zonable[WARRIOR])
		isWarFlag=true;

	memset(gradient, GRADIENT_UNREACHABLE, size);
	if (building->type->isVirtual && !building->type->zonable[WORKER])
	{
		assert(!building->type->zonableForbidden);
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			for (int xi=-r; xi<=r; xi++)
				if (yi2+(xi*xi)<=r2)
				{
					size_t addr = coordToIndex(posX+w+xi, posY+h+yi);
					if(gradient[addr] == GRADIENT_UNREACHABLE)
						gradient[addr] = GRADIENT_AT_GOAL;
				}
		}
	}
	else if (building->type->isVirtual && building->type->zonable[WORKER])
	{
		assert(!building->type->zonableForbidden);
		isClearingFlag=true;
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			for (int xi=-r; xi<=r; xi++)
				if (yi2+(xi*xi)<=r2)
				{
					size_t addr = coordToIndex(posX+w+xi, posY+h+yi);
					if(cases[addr].ressource.type!=NO_RES_TYPE && building->clearingRessources[cases[addr].ressource.type])
					{
						if(gradient[addr] == GRADIENT_UNREACHABLE)
							gradient[addr] = GRADIENT_AT_GOAL;
					}
				}
		}
	}

	for (int y=0; y<h; y++)
	{
		int wy=w*y;
		for (int x=0; x<w; x++)
		{
			int wyx=wy+x;
			const Case& c=cases[wyx];
			if (c.building==NOGBID)
			{
				if (c.forbidden&teamMask)
					gradient[wyx] = GRADIENT_FORBIDDEN;
				else if (c.ressource.type!=NO_RES_TYPE && !(isClearingFlag && gradient[wyx]==GRADIENT_AT_GOAL))
					gradient[wyx] = GRADIENT_FORBIDDEN;
				else if(immobileUnits[wyx] != 255)
					gradient[wyx] = GRADIENT_FORBIDDEN;
				//Clearing flags don't consider water an obstacle so long as that piece of
				//water is under the flag, like algae
				else if (!canSwim && isWater(x, y) && (!isClearingFlag || gradient[wyx] != GRADIENT_AT_GOAL))
					gradient[wyx] = GRADIENT_FORBIDDEN;
			}
			else
			{
				if (c.building==bgid)
					gradient[wyx] = GRADIENT_AT_GOAL;
				//Warflags don't consider enemy buildings an obstacle
				else if(!isWarFlag || (1<<Building::GIDtoTeam(c.building)) & (building->owner->allies))
					gradient[wyx] = GRADIENT_FORBIDDEN;
				else if(gradient[wyx]!=GRADIENT_AT_GOAL)
					gradient[wyx] = GRADIENT_UNREACHABLE;
			}
		}
	}

	if (!building->type->isVirtual)
	{
		// Spiral around the building footprint corner; start one cell NW of the building origin
		// (toroidal wrap), stride posW+1 so we cover the perimeter.
		bool reachable = spiralFindNonZero(gradient,
		                                    (posX - 1) & wMask, (posY - 1) & hMask,
		                                    posW + 1,
		                                    wMask, hMask, wDec);
		building->locked[canSwim] = !reachable;
		if (!reachable)
			return;
	}
	else
		building->locked[canSwim]=false;

	updateGlobalGradient(gradient);
}


bool Map::updateLocalRessources(Building *building, bool canSwim)
{
	assert(building);
	assert(building->type);
	assert(building->type->isVirtual);


	int posX=building->posX;
	int posY=building->posY;
	Uint32 teamMask=building->owner->me;

	Uint8 *gradient=building->localRessources[canSwim];
	if (gradient==NULL)
	{
		gradient=new Uint8[LOCAL_GRID_AREA];
		building->localRessources[canSwim]=gradient;
	}
	assert(gradient);

	bool *clearingRessources=building->clearingRessources;
	bool anyRessourceToClear=false;

	memset(gradient, GRADIENT_UNREACHABLE, LOCAL_GRID_AREA);
	int range=building->unitStayRange;
	if (range>LOCAL_GRID_CENTER)
		range=LOCAL_GRID_CENTER;
	int range2=range*range;
	for (int yl=0; yl<LOCAL_GRID_W; yl++)
	{
		int wyl=(yl<<LOCAL_GRID_SHIFT);
		int yg=(yl+posY-LOCAL_GRID_CENTER)&hMask;
		int wyg=w*yg;
		int dyl2=(yl-LOCAL_GRID_CENTER)*(yl-LOCAL_GRID_CENTER);
		for (int xl=0; xl<LOCAL_GRID_W; xl++)
		{
			int xg=(xl+posX-LOCAL_GRID_CENTER)&wMask;
			const Case& c=cases[wyg+xg];
			int addrl=wyl+xl;
			int dist2=(xl-LOCAL_GRID_CENTER)*(xl-LOCAL_GRID_CENTER)+dyl2;
			if (dist2<=range2)
			{
				if (c.forbidden&teamMask)
					gradient[addrl]=GRADIENT_FORBIDDEN;
				else if (c.ressource.type!=NO_RES_TYPE)
				{
					Sint8 t=c.ressource.type;
					if (t<BASIC_COUNT && clearingRessources[t])
					{
						gradient[addrl]=GRADIENT_AT_GOAL;
						anyRessourceToClear=true;
					}
					else
						gradient[addrl]=GRADIENT_FORBIDDEN;
				}
				else if (c.building!=NOGBID)
					gradient[addrl]=GRADIENT_FORBIDDEN;
				else if(immobileUnits[wyg+xg] != 255)
					gradient[addrl]=GRADIENT_FORBIDDEN;
				else if (!canSwim && isWater(xg, yg))
					gradient[addrl]=GRADIENT_FORBIDDEN;
			}
			else
				gradient[addrl]=GRADIENT_FORBIDDEN;
		}
	}
	// PORT: this is the SOLE reset for localRessourcesCleanTime[canSwim]; runs unconditionally
	// PORT: before both the false return below and the true return at function end. Building::clearingFlagStep
	// PORT: relies on this side effect rather than resetting the timer itself.
	building->localRessourcesCleanTime[canSwim]=0;
	if (anyRessourceToClear)
		building->anyRessourceToClear[canSwim]=1;
	else
	{
		building->anyRessourceToClear[canSwim]=2;
		return false;
	}
	propagateLocalGradient32(gradient);
	return true;
}


