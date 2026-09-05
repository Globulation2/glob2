// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "Unit.h"
#include "MapInternal.h"



// Building pathfinding (buildingAvailable, pathfindBuilding, dirtyLocalGradient)

namespace {

// Probe a 32x32 local gradient at (lx, ly) and its 8 neighbors. If any cell has a
// reachable gradient (g > GRADIENT_UNREACHABLE), set *dist = GRADIENT_AT_GOAL - g
// (distance to the building) and return true.
bool probeLocalGradient(const Uint8 *gradient, int lx, int ly, int *dist)
{
	Uint8 currentg = gradient[lx + (ly << 5)];
	if (currentg > GRADIENT_UNREACHABLE)
	{
		*dist = GRADIENT_AT_GOAL - currentg;
		return true;
	}
	for (int d = 0; d < 8; d++)
	{
		int ddx, ddy;
		Unit::dxDyFromDirection(d, &ddx, &ddy);
		int lxddx = clip_0_31(lx + ddx);
		int lyddy = clip_0_31(ly + ddy);
		Uint8 g = gradient[lxddx + (lyddy << 5)];
		if (g > GRADIENT_UNREACHABLE)
		{
			*dist = GRADIENT_AT_GOAL - g;
			return true;
		}
	}
	return false;
}

} // namespace

// Probe a full-map global gradient at (x, y) and its 8 neighbors.
bool Map::probeGlobalGradient(const Uint8 *gradient, int x, int y, int *dist) const
{
	Uint8 currentg = gradient[coordToIndex(x, y)];
	if (currentg > GRADIENT_UNREACHABLE)
	{
		*dist = GRADIENT_AT_GOAL - currentg;
		return true;
	}
	for (int d = 0; d < 8; d++)
	{
		int ddx, ddy;
		Unit::dxDyFromDirection(d, &ddx, &ddy);
		Uint8 g = gradient[coordToIndex(x + ddx, y + ddy)];
		if (g > GRADIENT_UNREACHABLE)
		{
			*dist = GRADIENT_AT_GOAL - g;
			return true;
		}
	}
	return false;
}

bool Map::buildingAvailable(Building *building, bool canSwim, int x, int y, int *dist)
{
	assert(building);
	int bx=building->posX;
	int by=building->posY;
	x&=wMask;
	y&=hMask;
	assert(x>=0);
	assert(y>=0);

	if (isInLocalGradient(x, y, bx, by))
	{
		Uint8 *gradient=building->localGradient[canSwim];
		int lx=(x-bx+15+32)&31;
		int ly=(y-by+15+32)&31;

		if (!building->dirtyLocalGradient[canSwim] && probeLocalGradient(gradient, lx, ly, dist))
			return true;

		updateLocalGradient(building, canSwim);
		if (building->locked[canSwim])
			return false;

		return probeLocalGradient(gradient, lx, ly, dist);
	}

	Uint8 *gradient=building->globalGradient[canSwim];
	if (gradient!=NULL)
	{
		// Existing global gradient: probe without recomputing. Recomputing the full-map
		// gradient on every miss is too expensive — callers fall back to other strategies.
		if (building->locked[canSwim])
			return false;
		return probeGlobalGradient(gradient, x, y, dist);
	}

	gradient=new Uint8[size];
	building->globalGradient[canSwim]=gradient;

	updateGlobalGradient(building, canSwim);
	if (building->locked[canSwim])
		return false;

	return probeGlobalGradient(gradient, x, y, dist);
}


bool Map::pathfindBuilding(Building *building, bool canSwim, int x, int y, int *dx, int *dy)
{
	assert(building);
	int bx=building->posX;
	int by=building->posY;
	assert(x>=0);
	assert(y>=0);
	Uint32 teamMask=building->owner->me;
	if (((cases[x+y*w].forbidden) & teamMask)!=0)
	{
		int teamNumber=building->owner->teamNumber;
		return pathfindForbidden(building->globalGradient[canSwim], teamNumber, canSwim, x, y, dx, dy);
	}
	Uint8 *gradient=building->localGradient[canSwim];
	if (isInLocalGradient(x, y, bx, by))
	{
		int lx=(x-bx+15+32)&31;
		int ly=(y-by+15+32)&31;
		Uint8 currentg=gradient[lx+(ly<<5)];

		if (!building->dirtyLocalGradient[canSwim] && currentg==GRADIENT_AT_GOAL)
		{
			*dx=0;
			*dy=0;
			return true;
		}

		if (!building->dirtyLocalGradient[canSwim] && currentg>GRADIENT_UNREACHABLE)
		{
			if (directionByMinigrad(teamMask, canSwim, x, y, bx, by, dx, dy, gradient, true))
				return true;
		}

		updateLocalGradient(building, canSwim);
		if (building->locked[canSwim])
			return false;

		currentg=gradient[lx+ly*32];
		if (currentg>GRADIENT_UNREACHABLE)
		{
			if (directionByMinigrad(teamMask, canSwim, x, y, bx, by, dx, dy, gradient, true))
				return true;
		}
	}
	// Local 32x32 gradient pathfinding has failed, fall back to the full-size gradient.

	gradient=building->globalGradient[canSwim];
	if (gradient==NULL)
	{
		gradient=new Uint8[size];
		building->globalGradient[canSwim]=gradient;
	}
	else
	{
		if (building->locked[canSwim])
			return false;
		Uint8 currentg=gradient[coordToIndex(x, y)];
		if (currentg==GRADIENT_UNREACHABLE)
			return false;

		if (directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient, true))
			return true;

		// Recomputing the global gradient is expensive; throttle to once every 128 ticks (~5.12s).
		if (building->lastGlobalGradientUpdateStepCounter[canSwim]+128>game->stepCounter)
			return directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient, false);
	}

	updateGlobalGradient(building, canSwim);
	building->lastGlobalGradientUpdateStepCounter[canSwim]=game->stepCounter;

	if (building->locked[canSwim])
		return false;

	Uint8 currentg=gradient[coordToIndex(x, y)];
	if (currentg>GRADIENT_UNREACHABLE)
	{
		if (directionByMinigrad(teamMask, canSwim, x, y, dx, dy, gradient, true))
			return true;
	}

	return false;
}


void Map::dirtyLocalGradient(int x, int y, int wl, int hl, int teamNumber)
{
	y &= hMask;
	x &= wMask;
	for (int hi=0; hi<hl; hi++)
	{
		for (int wi=0; wi<wl; wi++)
		{
			int bgid=cases[coordToIndex(x + wi, y + hi)].building;
			if (bgid!=NOGBID)
				if (Building::GIDtoTeam(bgid)==teamNumber)
				{
					Building *b=game->teams[teamNumber]->myBuildings[Building::GIDtoID(bgid)];
					b->resetLocalRessources();
				}
		}
	}
}
