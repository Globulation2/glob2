// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Utilities.h"
#include "Unit.h"
#include "MapInternal.h"



// Resource pathfinding for units (pathfindResource, pathfindRandom)

bool Map::pathfindResource(int teamNumber, Uint8 resourceType, int swimClass, int x, int y, int *dx, int *dy, bool *stopWork)
{
	assert(resourceType<MAX_RESOURCES);
	const Uint16 *gradient=getResourceGradient(teamNumber, resourceType, swimClass);
	Uint16 here=gradient[coordToIndex(x, y)];
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
	if (directionByGradient(teamMask, swimClass, x, y, gradient, dx, dy, true))
		return true;
	return directionByGradient(teamMask, swimClass, x, y, gradient, dx, dy, false);
}


#ifndef YOG_SERVER_ONLY
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
