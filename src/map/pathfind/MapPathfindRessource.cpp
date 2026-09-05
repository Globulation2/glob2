// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Utilities.h"
#include "BuildingType.h"
#include "Unit.h"
#include "MapInternal.h"



// Ressource pathfinding for units (pathfindRessource, pathfindLocalRessource, pathfindRandom)

bool Map::pathfindRessource(int teamNumber, Uint8 ressourceType, bool canSwim, int x, int y, int *dx, int *dy, bool *stopWork)
{
	assert(ressourceType<MAX_RESSOURCES);
	const Uint8 *gradient=ressourcesGradient[teamNumber][ressourceType][canSwim];
	assert(gradient);
	Uint8 max=gradient[x+y*w];
	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	if (max==GRADIENT_FORBIDDEN)
	{
		*stopWork=true;
		return pathfindForbidden(gradient, teamNumber, canSwim, x, y, dx, dy);
	}
	if (max<2)
	{
		*stopWork=true;
		return false;
	}

	if (directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient, true))
		return true;

	*stopWork=false;
	return false;
}


#ifndef YOG_SERVER_ONLY
void Map::pathfindRandom(Unit *unit)
{
	int x=unit->posX;
	int y=unit->posY;
	if ((cases[x+(y<<wDec)].forbidden)&unit->owner->me)
	{
		if (pathfindForbidden(NULL, unit->owner->teamNumber, (unit->performance[SWIM]>0), x, y, &unit->dx, &unit->dy))
		{
			unit->directionFromDxDy();
		}
		else
		{
			unit->dx=0;
			unit->dy=0;
			unit->direction=8;
		}
	}
	else
	{
		bool da[8];
		int count=0;
		for (int di=0; di<8; di++)
		{
			int tx=(x+tabClose[di][0])&wMask;
			int ty=(y+tabClose[di][1])&hMask;
			if (isFreeForGroundUnit(tx, ty, (unit->performance[SWIM]>0), unit->owner->me))
			{
				da[di]=true;
				count++;
			}
			else
				da[di]=false;
		}
		if (count==0)
		{
			unit->dx=0;
			unit->dy=0;
			unit->direction=8;
			return;
		}
		int dir=syncRand()%count;
		for (int di=0; di<8; di++)
			if (da[di] && dir--==0)
			{
				unit->dx=tabClose[di][0];
				unit->dy=tabClose[di][1];
				unit->direction=di;
				return;
			}
		assert(false);
	}
}
#endif  // !YOG_SERVER_ONLY

bool Map::pathfindLocalRessource(Building *building, bool canSwim, int x, int y, int *dx, int *dy)
{
	assert(building);
	assert(building->type);
	assert(building->type->isVirtual);

	int bx=building->posX;
	int by=building->posY;
	Uint32 teamMask=building->owner->me;

	Uint8 *gradient=building->localRessources[canSwim];
	if (gradient==NULL)
	{
		if (!updateLocalRessources(building, canSwim))
			return false;
		gradient=building->localRessources[canSwim];
	}
	assert(gradient);
	//HACK: I have no idea what is going on or why isInLocalGradient(x, y, bx, by) was asserted and why isInLocalGradient(x, y, bx, by) checks for the rectangle it is checking for, but this fixes a rare crash.
	if(!isInLocalGradient(x, y, bx, by))
		return false;

	int lx=(x-bx+15+32)&31;
	int ly=(y-by+15+32)&31;
	int max=0;
	Uint8 currentg=gradient[lx+(ly<<5)];
	bool found=false;
	bool gradientUsable=false;

	// PORT: escalation path — bumps localRessourcesCleanTime by 16 to trigger clearingFlagStep's
	// PORT: recompute (which checks >125) sooner. The 125/128 thresholds are slightly mismatched;
	// PORT: align them in the Rust port (probably both should be 125).
	if (currentg==GRADIENT_UNREACHABLE && (building->localRessourcesCleanTime[canSwim]+=16)<128)
	{
		// This means there are still ressources, but they are unreachable.
		// We wait 5[s] before recomputing anything.
		return false;
	}

	if (currentg>GRADIENT_UNREACHABLE && currentg!=GRADIENT_AT_GOAL)
	{
		for (int sd=0; sd<=1; sd++)
			for (int d=sd; d<8; d+=2)
			{
				int ddx, ddy;
				Unit::dxDyFromDirection(d, &ddx, &ddy);
				int lxddx=clip_0_31(lx+ddx);
				int lyddy=clip_0_31(ly+ddy);
				Uint8 g=gradient[lxddx+(lyddy<<5)];
				if (!gradientUsable && g>currentg && isHardSpaceForGroundUnit(x+ddx, y+ddy, canSwim, teamMask))
					gradientUsable=true;
				if (g>=max && isFreeForGroundUnit(x+ddx, y+ddy, canSwim, teamMask))
				{
					max=g;
					*dx=ddx;
					*dy=ddy;
					found=true;
				}
			}

		if (gradientUsable)
		{
			if (!found)
			{
				*dx=0;
				*dy=0;
			}
			return true;
		}
	}

	updateLocalRessources(building, canSwim);

	max=0;
	currentg=gradient[lx+(ly<<5)];
	found=false;
	gradientUsable=false;

	if (currentg==GRADIENT_UNREACHABLE)
		return false;

	if (currentg==GRADIENT_FORBIDDEN || currentg==GRADIENT_AT_GOAL)
		return false;

	for (int sd=0; sd<=1; sd++)
		for (int d=sd; d<8; d+=2)
		{
			int ddx, ddy;
			Unit::dxDyFromDirection(d, &ddx, &ddy);
			int lxddx=clip_0_31(lx+ddx);
			int lyddy=clip_0_31(ly+ddy);
			Uint8 g=gradient[lxddx+(lyddy<<5)];
			if (!gradientUsable && g>currentg && isHardSpaceForGroundUnit(x+ddx, y+ddy, canSwim, teamMask))
				gradientUsable=true;
			if (g>=max && isFreeForGroundUnit(x+ddx, y+ddy, canSwim, teamMask))
			{
				max=g;
				*dx=ddx;
				*dy=ddy;
				found=true;
			}
		}

	if (!gradientUsable)
		return false;

	if (!found)
	{
		*dx=0;
		*dy=0;
	}
	return true;
}
