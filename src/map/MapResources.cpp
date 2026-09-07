// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Utilities.h"
#include "GlobalContainer.h"
#include "MapInternal.h"



// Resource grid mutations + resource availability + points/area names

void Map::decResource(int x, int y)
{
	Resource &r = getCase(x, y).resource;
	
	if (r.type == NO_RES_TYPE || r.amount == 0)
		return;
	
	const ResourceType *fulltype = globalContainer->resourcesTypes.get(r.type);
	
	if (!fulltype->shrinkable)
		return;
	if (fulltype->eternal)
	{
		if (r.amount > 0)
			r.amount--;
	}
	else
	{
		if (!fulltype->granular || r.amount<=1)
			r.clear();
		else
			r.amount--;
	}
}

void Map::decResource(int x, int y, int resourceType)
{
	if (isResourceTakeable(x, y, resourceType))
		decResource(x, y);
}

bool Map::incResource(int x, int y, int resourceType, int variety)
{
	Resource &r = getCase(x, y).resource;
	const ResourceType *fulltype;
	if (r.type == NO_RES_TYPE)
	{
		if (getBuilding(x, y) != NOGBID)
			return false;
		if (getGroundUnit(x, y) != NOGUID)
			return false;

		fulltype = globalContainer->resourcesTypes.get(resourceType);
		if (getTerrainType(x, y) == fulltype->terrain)
		{
			r.type = resourceType;
			r.variety = variety;
			r.amount = RESOURCE_INITIAL_AMOUNT;
			r.animation = 0;
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		fulltype = globalContainer->resourcesTypes.get(r.type);
	}

	if (r.type != resourceType)
		return false;
	if (!fulltype->shrinkable)
		return false;
	if (r.amount < fulltype->sizesCount)
	{
		r.amount++;
		return true;
	}
	else
	{
		r.amount--;
	}
	return false;
}


void Map::setNoResource(int x, int y, int l)
{
	assert(l>=0);
	assert(l<w);
	assert(l<h);
	for (int dx=x-(l>>1); dx<x+(l>>1)+1; dx++)
		for (int dy=y-(l>>1); dy<y+(l>>1)+1; dy++)
			cases[coordToIndex(dx, dy)].resource.clear();
}

void Map::removeUnallowedResources(int x, int y, int w, int h)
{
	for (int dx=x; dx<x+w; dx++)
		for (int dy=y; dy<y+h; dy++)
		{
			Resource& r=cases[coordToIndex(dx, dy)].resource;
			if (r.type!=NO_RES_TYPE && getTerrainType(dx, dy)!=globalContainer->resourcesTypes.get(r.type)->terrain)
				r.clear();
		}
}

void Map::setResource(int x, int y, int type, int l)
{
	assert(l>=0);
	assert(l<w);
	assert(l<h);
	for (int dx=x-(l>>1); dx<x+(l>>1)+1; dx++)
		for (int dy=y-(l>>1); dy<y+(l>>1)+1; dy++)
			if (isResourceAllowed(dx, dy, type))
			{
				Resource& rp=cases[coordToIndex(dx, dy)].resource;
				rp.type=type;
				const ResourceType *rt=globalContainer->resourcesTypes.get(type);
				rp.variety=syncRand()%rt->varietiesCount;
				assert(rt->sizesCount>1);
				rp.amount=RESOURCE_INITIAL_AMOUNT+syncRand()%(rt->sizesCount-1);
				rp.animation=0;
			}
}

bool Map::isResourceAllowed(int x, int y, int type)
{
	return (getBuilding(x, y) == NOGBID) && (getGroundUnit(x, y) == NOGUID) && (getTerrainType(x, y)==globalContainer->resourcesTypes.get(type)->terrain);
}

bool Map::isPointSet(int n, int x, int y) const
{
	return getCase(x, y).scriptAreas & 1<<n;
}

void Map::setPoint(int n, int x, int y)
{
	getCase(x, y).scriptAreas |= 1<<n;
}

void Map::unsetPoint(int n, int x, int y)
{
	getCase(x, y).scriptAreas ^= getCase(x, y).scriptAreas & (1<<n);
}

std::string Map::getAreaName(int n) const
{
	return areaNames[n];
}

void Map::setAreaName(int n, std::string name)
{
	areaNames[n]=name;
}


bool Map::resourceAvailable(int teamNumber, int resourceType, bool canSwim, int x, int y) const
{
	Uint8 g = getGradient(teamNumber, resourceType, canSwim, x, y);
	return g>GRADIENT_UNREACHABLE; //Because 0==obstacle, 1==no obstacle, but you don't know if there is anything around.
}

bool Map::resourceAvailable(int teamNumber, int resourceType, bool canSwim, int x, int y, int *dist) const
{
	Uint8 g = getGradient(teamNumber, resourceType, canSwim, x, y);
	if (g>GRADIENT_UNREACHABLE)
	{
		*dist = GRADIENT_AT_GOAL-g;
		return true;
	}
	else
		return false;
}

bool Map::resourceAvailableUpdate(int teamNumber, int resourceType, bool canSwim, int x, int y, Sint32 *targetX, Sint32 *targetY, int *dist)
{
	// distance and availability
	bool result;
	if (dist)
		result = resourceAvailable(teamNumber, resourceType, canSwim, x, y, dist);
	else
		result = resourceAvailable(teamNumber, resourceType, canSwim, x, y);
		
	// target position
	Uint8 *gradient = resourcesGradient[teamNumber][resourceType][canSwim];
	getGlobalGradientDestination(gradient, x, y, targetX, targetY);

	return result;
}

bool Map::getGlobalGradientDestination(Uint8 *gradient, int x, int y, Sint32 *targetX, Sint32 *targetY) const
{
	// we start from our current position
	int vx = x & wMask;
	int vy = y & hMask;
	// max is initialized to gradient value of current position
	Uint8 max = gradient[coordToIndex(vx, vy)];
	
	bool result = false;
	// for up to 255 steps, we follow gradient
	for (int count=0; count<255; count++)
	{
		bool found = false;
		int vddx = 0;
		int vddy = 0;
		
		// search all directions
		for (int d=0; d<8; d++)
		{
			int ddx = deltaOne[d][0];
			int ddy = deltaOne[d][1];
			Uint8 g = gradient[coordToIndex(vx + ddx, vy + ddy)];
			if (g>max)
			{
				max = g;
				vddx = ddx;
				vddy = ddy;
				found = true;
			}
		}
		
		// change position
		vx = (vx+vddx) & wMask;
		vy = (vy+vddy) & hMask;
		
		// if we have reached destination break
		if (max == GRADIENT_AT_GOAL)
		{
			result = true;
			break;
		}
		// if we haven't found a suitable direction, we break, but we do not have exact destination
		else if (!found)
			break;
	}
	
	// return best destination and wether it is exact or not
	*targetX = vx;
	*targetY = vy;
	return result;
}



