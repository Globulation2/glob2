// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "BuildingType.h"
#include "EngineTiming.h"
#include "Game.h"
#include "Utilities.h"
#include "Unit.h"
#include "MapInternal.h"



// Building pathfinding (buildingGradient, buildingAvailable, roundTripGradient,
// roundTripDistance, pathfindBuilding)

namespace {

// A unit that cannot make progress forces a rebuild at most this often (~5 s).
constexpr Uint32 STUCK_REBUILD_TICKS = 128;
// A round-trip gradient follows its two parents with at most this delay (the
// resource gradient stays authoritative for reachability, so staleness only
// costs a detour). Building::freeIdleGradients drops unused ones.
// This timer is the only thing that refreshes the child: a topology generation
// bump rebuilds the parent walking field on its next use, but does not
// propagate to the round-trip fields derived from it, so for up to this many
// ticks a fetcher can be priced against the ground as it was before the
// change. It still cannot walk into a wall - directionByGradient re-tests live
// passability on every candidate step - so the cost is a detour, and the
// alternative (stamping the parent's generation on the child) would rebuild
// every live child on every structural change, which measured far worse than
// the detour it removes.
constexpr Uint32 ROUND_TRIP_REFRESH_TICKS = 120;

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
	building->globalGradientUsedStep[swimClass]=now;
	bool rebuild=false;
	if (gradient==NULL)
	{
		gradient=new Uint16[size];
		rebuild=true;
	}
	else if ((building->dirtyGradient[swimClass] || building->gradientGeneration[swimClass]!=topologyGeneration)
		&& lastUpdate+GRADIENT_DIRTY_REBUILD_TICKS<=now)
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

bool Map::buildingAvailable(Building *building, int swimClass, int x, int y, int *dist)
{
	const Uint16 *gradient=buildingGradient(building, swimClass);
	if (gradient==NULL)
		return false;
	// The unit's own cell can be an obstacle in this building's field - it may be
	// standing on another building, or in a forbidden area - while a cell next to
	// it is on a route. Take the first of the nine that carries a distance.
	Uint16 g=gradient[coordToIndex(x, y)];
	for (int d=0; d<8 && g<=GRADIENT_UNREACHABLE; d++)
		g=gradient[coordToIndex(x+tabClose[d][0], y+tabClose[d][1])];
	if (g<=GRADIENT_UNREACHABLE)
		return false;
	*dist=gradientTiles(g);
	return true;
}


const Uint16 *Map::roundTripGradient(Building *building, int resourceType, int swimClass)
{
	if (buildingGradient(building, swimClass)==NULL)
		return NULL;
	Uint32 now=game->stepCounter;
	Uint16 *&gradient=building->roundTripGradient[resourceType][swimClass];
	building->roundTripGradientUsedStep[resourceType][swimClass]=now;
	if (gradient!=NULL && building->roundTripGradientStep[resourceType][swimClass]+ROUND_TRIP_REFRESH_TICKS>now)
		return gradient;
	if (gradient==NULL)
		gradient=new Uint16[size];
	updateRoundTripGradient(building, resourceType, swimClass);
	return gradient;
}

bool Map::roundTripDistance(Building *building, int resourceType, int swimClass, int x, int y, int *dist)
{
	// Only gradients a fetcher keeps alive: hiring looks at every needed
	// resource of every building, far more than ever get fetched.
	const Uint16 *gradient=building->roundTripGradient[resourceType][swimClass];
	if (gradient==NULL)
		return false;
	// It may be a few ticks older than the resource gradient the callers walk
	// by; never report a resource that one says is gone.
	if (!resourceAvailable(building->owner->teamNumber, resourceType, swimClass, x, y))
		return false;
	building->roundTripGradientUsedStep[resourceType][swimClass]=game->stepCounter;
	Uint16 g=gradient[coordToIndex(x, y)];
	if (g<=GRADIENT_UNREACHABLE)
		return false;
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
