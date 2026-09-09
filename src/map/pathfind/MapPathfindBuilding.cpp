// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "BuildingType.h"
#include "EngineTiming.h"
#include "Game.h"
#include "Utilities.h"
#include "Unit.h"
#include "MapInternal.h"



// Building pathfinding (buildingGradient, buildingAvailable, pathfindBuilding, dirtyBuildingGradients)

namespace {

// A gradient marked dirty by a map change is rebuilt at most this often.
constexpr Uint32 DIRTY_REBUILD_TICKS = 25;
// A unit that cannot make progress forces a rebuild at most this often (~5 s).
constexpr Uint32 STUCK_REBUILD_TICKS = 128;

bool isClearingFlag(const Building *building)
{
	return building->type->isVirtual && building->type->zonable[WORKER];
}

} // namespace

const Uint16 *Map::buildingGradient(Building *building, int swimClass)
{
	assert(building);
	Uint16 *&gradient=building->globalGradient[swimClass];
	Uint32 lastUpdate=building->lastGlobalGradientUpdateStepCounter[swimClass];
	Uint32 now=game->stepCounter;
	bool rebuild=false;
	if (gradient==NULL)
	{
		gradient=new Uint16[size];
		rebuild=true;
	}
	else if (building->dirtyGradient[swimClass] && lastUpdate+DIRTY_REBUILD_TICKS<=now)
		rebuild=true;
	// A clearing flag's goals are resources, which grow and get cleared.
	else if (isClearingFlag(building) && lastUpdate+CLEARING_FLAG_REFRESH_TICKS<=now)
		rebuild=true;
	if (rebuild)
		updateGlobalGradient(building, swimClass);
	if (building->locked[swimClass>0])
		return NULL;
	return gradient;
}

namespace {

// Probe the cell and its 8 neighbours: the unit may stand on a cell the
// gradient treats as an obstacle.
Uint16 probeAround(const Uint16 *gradient, const Map *map, int x, int y)
{
	Uint16 g=gradient[map->coordToIndex(x, y)];
	for (int d=0; d<8 && g<=GRADIENT_UNREACHABLE; d++)
		g=gradient[map->coordToIndex(x+tabClose[d][0], y+tabClose[d][1])];
	return g;
}

} // namespace

bool Map::buildingAvailable(Building *building, int swimClass, int x, int y, int *dist)
{
	const Uint16 *gradient=buildingGradient(building, swimClass);
	if (gradient==NULL)
		return false;
	Uint16 g=probeAround(gradient, this, x, y);
	if (g<=GRADIENT_UNREACHABLE)
	{
		// A building nobody can reach is offered no worker, and with no worker on
		// its way there is no stuck unit to make pathfindBuilding rebuild the
		// gradient. Rebuild here as well, at the same rate, so a field computed
		// while the site was walled off cannot outlive the wall.
		if (building->lastGlobalGradientUpdateStepCounter[swimClass]+STUCK_REBUILD_TICKS>game->stepCounter)
			return false;
		updateGlobalGradient(building, swimClass);
		if (building->locked[swimClass>0])
			return false;
		g=probeAround(gradient, this, x, y);
		if (g<=GRADIENT_UNREACHABLE)
			return false;
	}
	*dist=gradientTiles(g);
	return true;
}


bool Map::pathfindBuilding(Building *building, int swimClass, int x, int y, int *dx, int *dy)
{
	assert(building);
	assert(x>=0);
	assert(y>=0);
	Uint32 teamMask=building->owner->me;
	if (((tiles[coordToIndex(x, y)].forbidden) & teamMask)!=0)
		return pathfindForbidden(building->globalGradient[swimClass], building->owner->teamNumber, swimClass, x, y, dx, dy);

	const Uint16 *gradient=buildingGradient(building, swimClass);
	if (gradient==NULL)
		return false;
	if (isClearingFlag(building) && gradient[coordToIndex(x, y)]==GRADIENT_AT_GOAL)
	{
		// Standing where one of the flag's resources was: it is gone, the gradient is stale.
		building->dirtyGradient[swimClass]=true;
		return false;
	}
	if (directionByGradient(teamMask, swimClass, x, y, gradient, dx, dy, true))
		return true;
	if (building->lastGlobalGradientUpdateStepCounter[swimClass]+STUCK_REBUILD_TICKS>game->stepCounter)
		return directionByGradient(teamMask, swimClass, x, y, gradient, dx, dy, false);

	// Stuck for a while: the gradient may be stale, rebuild it now.
	updateGlobalGradient(building, swimClass);
	if (building->locked[swimClass>0])
		return false;
	if (directionByGradient(teamMask, swimClass, x, y, gradient, dx, dy, true))
		return true;
	return directionByGradient(teamMask, swimClass, x, y, gradient, dx, dy, false);
}


void Map::dirtyBuildingGradients(int x, int y, int wl, int hl, int teamNumber)
{
	y &= hMask;
	x &= wMask;
	for (int hi=0; hi<hl; hi++)
	{
		for (int wi=0; wi<wl; wi++)
		{
			int bgid=tiles[coordToIndex(x + wi, y + hi)].building;
			if (bgid!=NOGBID)
				if (Building::GIDtoTeam(bgid)==teamNumber)
				{
					Building *b=game->teams[teamNumber]->myBuildings[Building::GIDtoID(bgid)];
					b->dirtyGradients();
				}
		}
	}
}
