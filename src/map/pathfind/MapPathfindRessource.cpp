// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Utilities.h"
#include "Unit.h"
#include "MapInternal.h"



// Resource pathfinding for units (pathfindResource, pathfindRandom)

#ifndef YOG_SERVER_ONLY
bool Map::pathfindResource(int teamNumber, Uint8 resourceType, int swimClass, int x, int y, int *dx, int *dy, bool *stopWork, Building *target, bool withMarkets)
{
	assert(resourceType<MAX_RESOURCES);
	const Uint16 *gradient=getResourceGradient(teamNumber, resourceType, swimClass, withMarkets);
	size_t hereIndex=coordToIndex(x, y);
	Uint16 here=gradient[hereIndex];
	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	if (here==GRADIENT_FORBIDDEN)
	{
		*stopWork=true;
		return pathfindForbidden(gradient, teamNumber, swimClass, x, y, dx, dy);
	}
	if (here==GRADIENT_UNREACHABLE)
	{
		*stopWork=true;
		return false;
	}
	*stopWork=false;
	if (here==GRADIENT_AT_GOAL)
		return false; // standing where the resource was: it is gone, wander until the gradient is rebuilt
	if (target)
	{
		// The round-trip gradient may lag behind this one by a few ticks; when
		// it is blocked or stale here, the plain gradient below still leads to
		// a resource.
		const Uint16 *roundTrip=roundTripGradient(target, resourceType, swimClass);
		if (roundTrip && roundTrip[hereIndex]>GRADIENT_UNREACHABLE
			&& directionByGradient(teamMask, swimClass, x, y, roundTrip, dx, dy, true))
			return true;
	}
	if (directionByGradient(teamMask, swimClass, x, y, gradient, dx, dy, true))
		return true;
	return directionByGradient(teamMask, swimClass, x, y, gradient, dx, dy, false);
}


void Map::pathfindRandom(Unit *unit)
{
	int x=unit->posX;
	int y=unit->posY;
	if ((tiles[x+(y<<wDec)].forbidden)&unit->owner->me)
	{
		if (pathfindForbidden(NULL, unit->owner->teamNumber, unit->swimClass(), x, y, &unit->dx, &unit->dy))
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
