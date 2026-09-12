// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "BuildingType.h"
#include "Game.h"
#include "Unit.h"
#include "MapInternal.h"



// updateGlobalGradient(Building*): the full-map gradient toward a building, a
// flag's zone or, for a clearing flag, the clearable resources in its range.
// updateRoundTripGradient: the gradient of the trip to a resource and on to
// the building.

void Map::updateGlobalGradient(Building *building, int swimClass)
{
	assert(building);
	assert(building->type);
	int posX=building->posX;
	int posY=building->posY;
	int posW=building->type->width;
	Uint32 teamMask=building->owner->me;
	Uint16 bgid=building->gid;
	bool canSwim=swimClass>0;

	Uint16 *gradient=building->globalGradient[swimClass];
	assert(gradient);
	building->dirtyGradient[swimClass]=false;
	building->lastGlobalGradientUpdateStepCounter[swimClass]=game->stepCounter;
	building->gradientGeneration[swimClass]=topologyGeneration;

	bool isClearingFlag=false;
	bool isWarFlag=false;
	if (building->type->isVirtual && building->type->zonable[WARRIOR])
		isWarFlag=true;

	std::fill(gradient, gradient+size, GRADIENT_UNREACHABLE);
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
		bool anyResourceToClear=false;
		int r=building->unitStayRange;
		int r2=r*r;
		for (int yi=-r; yi<=r; yi++)
		{
			int yi2=(yi*yi);
			for (int xi=-r; xi<=r; xi++)
				if (yi2+(xi*xi)<=r2)
				{
					size_t addr = coordToIndex(posX+w+xi, posY+h+yi);
					if(tiles[addr].resource.type!=NO_RES_TYPE && building->clearingResources[tiles[addr].resource.type])
					{
						if(gradient[addr] == GRADIENT_UNREACHABLE)
							gradient[addr] = GRADIENT_AT_GOAL;
						anyResourceToClear=true;
					}
				}
		}
		building->anyResourceToClear[canSwim] = anyResourceToClear ? 1 : 2;
	}

	for (int y=0; y<h; y++)
	{
		int wy=w*y;
		for (int x=0; x<w; x++)
		{
			int wyx=wy+x;
			const Tile& c=tiles[wyx];
			if (c.building==NOGBID)
			{
				if (c.forbidden&teamMask)
					gradient[wyx] = GRADIENT_FORBIDDEN;
				else if (c.resource.type!=NO_RES_TYPE && !(isClearingFlag && gradient[wyx]==GRADIENT_AT_GOAL))
					gradient[wyx] = GRADIENT_FORBIDDEN;
				else if(immobileUnits[wyx] != IMMOBILE_UNIT_NONE)
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
				//War flags don't consider enemy buildings an obstacle
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

	propagateGradient(gradient, swimClass);
}


void Map::updateRoundTripGradient(Building *building, int resourceType, int swimClass)
{
	Uint16 *gradient=building->roundTripGradient[resourceType][swimClass];
	assert(gradient);
	building->roundTripGradientStep[resourceType][swimClass]=game->stepCounter;
	const Uint16 *toBuilding=building->globalGradient[swimClass];
	const Uint16 *toResource=getResourceGradient(building->owner->teamNumber, resourceType, swimClass);
	// Same obstacles as the resource gradient. A resource tile is seeded with
	// the cost of carrying from the cheapest free cell next to it, where the
	// unit harvests, to the building.
	Uint16 bestSeed=GRADIENT_UNREACHABLE;
	for (size_t i=0; i<size; i++)
	{
		if (toResource[i]!=GRADIENT_AT_GOAL)
		{
			gradient[i]=toResource[i]==GRADIENT_FORBIDDEN ? GRADIENT_FORBIDDEN : GRADIENT_UNREACHABLE;
			continue;
		}
		size_t x=i&wMask;
		size_t y=i>>wDec;
		Uint16 best=GRADIENT_UNREACHABLE;
		for (int d=0; d<8; d++)
		{
			size_t n=coordToIndex(x+tabClose[d][0], y+tabClose[d][1]);
			if (toResource[n]>GRADIENT_UNREACHABLE && toBuilding[n]>best)
				best=toBuilding[n];
		}
		gradient[i]=best;
		if (best>bestSeed)
			bestSeed=best;
	}
	// Units farther than this from the cheapest fetch are scored by the plain
	// distances instead (the callers fall back when a cell is unreachable
	// here), which keeps the build small on big maps.
	constexpr int ROUND_TRIP_RANGE=128*GRADIENT_STEP;
	propagateGradient(gradient, swimClass, GRADIENT_AT_GOAL-bestSeed+ROUND_TRIP_RANGE);
}
