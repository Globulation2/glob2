// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "BuildingType.h"
#include "EngineTiming.h"
#include "Game.h"
#include "Utilities.h"
#include "Unit.h"
#include "MapInternal.h"
#include "BuildingGradientSearch.h"
#include "gradient/BuildingGradientStats.h"



// Building pathfinding (buildingGradient, buildingAvailable, pathfindBuilding)

namespace {

// A unit that cannot make progress forces a rebuild at most this often (~5 s).
constexpr Uint32 STUCK_REBUILD_TICKS = 128;


} // namespace

bool Map::prepareBuildingGradient(Building *building, int swimClass, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
	assert(building);
	Uint16 *&gradient=building->globalGradient[slot];
	Uint32 lastUpdate=building->lastGlobalGradientUpdateStepCounter[slot];
	Uint32 now=game->stepCounter;
	building->globalGradientUsedStep[slot]=now;
	bool rebuild=false, cold=false;
	using Reason = BuildingGradientStats::Reason;
	Reason reason = Reason::Other;
	if (gradient==NULL)
	{
		gradient=acquireBuildingGradientBuffer();
		rebuild=cold=true;
		reason=Reason::Null;
	}
	else if ((building->dirtyGradient[slot] || building->gradientGeneration[slot]!=topologyGeneration)
		&& lastUpdate+GRADIENT_DIRTY_REBUILD_TICKS<=now)
	{
		rebuild=true;
		reason=building->dirtyGradient[slot] ? Reason::Dirty : Reason::Generation;
	}
	// A clearing flag's goals are resources, which grow and get cleared.
	else if (building->resolveRoute(route) == BuildingRoute::Clearing && lastUpdate+CLEARING_FLAG_REFRESH_TICKS<=now)
	{
		rebuild=true;
		reason=Reason::Clearing;
	}
	// With scheduled building gradients a refresh is requested and the old
	// field keeps serving until its publication; a cold field has none.
	if (rebuild && (cold || !requestBuildingRefresh(building, slot)))
	{
		using Sync = BuildingSyncReason;
		if (cold)
			switch (building->gradientDrop[slot])
			{
			case Building::GradientDrop::New: noteBuildingSyncReason(Sync::ColdNew); break;
			case Building::GradientDrop::Idle: noteBuildingSyncReason(Sync::ColdIdle); break;
			case Building::GradientDrop::Own: noteBuildingSyncReason(Sync::ColdOwn); break;
			case Building::GradientDrop::Team: noteBuildingSyncReason(Sync::ColdTeam); break;
			case Building::GradientDrop::Area: noteBuildingSyncReason(Sync::ColdArea); break;
			}
		else noteBuildingSyncReason(Sync::Other);
		if (gradientStats) gradientStats->setPendingReason(reason);
		updateGlobalGradient(building, swimClass, route);
	}
	return !building->locked[building->routeAccess(swimClass, route)];
}

const Uint16 *Map::buildingGradient(Building *building, int swimClass, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
	if (!prepareBuildingGradient(building, swimClass, route)) return NULL;
	finishBuildingGradient(building, swimClass, route);
	return building->globalGradient[slot];
}

Uint16 Map::buildingGradientValue(Building *building, int swimClass, size_t cell, BuildingRoute route) const
{
	const int slot = building->routeSlot(swimClass, route);
	assert(building->globalGradient[slot]);
	assert(cell < size);
	if (auto &search = building->globalGradientSearch[slot]) search->resolve(cell);
	return building->globalGradient[slot][cell];
}

bool Map::buildingGradientDirection(EntityRandom& random, Building *building, int swimClass, int x, int y,
	int *dx, int *dy, bool strict, BuildingRoute route) const
{
	const int slot = building->routeSlot(swimClass, route);
	// Settling this layer also settles all equal/better neighbors the generic
	// direction picker can select. Keep partial-array access inside this adapter.
	buildingGradientValue(building, swimClass, coordToIndex(x, y), route);
	return directionByGradient(random, building->owner->me, swimClass, x, y,
		building->globalGradient[slot], dx, dy, strict);
}

bool Map::buildingAvailable(Building *building, int swimClass, int x, int y, int *dist, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
	PERF_SCOPE_TIME(PathBuilding);
	if (!prepareBuildingGradient(building, swimClass, route))
		return false;
	// The unit's own cell can be an obstacle in this building's field - it may be
	// standing on another building, or in a forbidden area - while a cell next to
	// it is on a route. Take the first of the nine that carries a distance.
	Uint16 g=buildingGradientValue(building, swimClass, coordToIndex(x, y), route);
	for (int d=0; d<8 && g<=GRADIENT_UNREACHABLE; d++)
		g=buildingGradientValue(building, swimClass, coordToIndex(x+tabClose[d][0], y+tabClose[d][1]), route);
	if (g<=GRADIENT_UNREACHABLE)
		return false;
	*dist=gradientTiles(g);
	return true;
}


bool Map::pathfindBuilding(EntityRandom& random, Building *building, int swimClass, int x, int y, int *dx, int *dy, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
	PERF_SCOPE_TIME(PathBuilding);
	assert(building);
	assert(x>=0);
	assert(y>=0);
	if (((areaCells[coordToIndex(x, y)].forbidden) & building->owner->me)!=0)
	{
		// This escape path reads the cached field directly as a tie-breaker.
		// Preserve its old age (do not call buildingGradient here).
		finishBuildingGradient(building, swimClass, route);
		return pathfindForbidden(building->globalGradient[slot], building->owner->teamNumber, swimClass, x, y, dx, dy);
	}

	if (!prepareBuildingGradient(building, swimClass, route))
		return false;
	if (building->resolveRoute(route) == BuildingRoute::Clearing
		&& buildingGradientValue(building, swimClass, coordToIndex(x, y), route)==GRADIENT_AT_GOAL)
	{
		// Standing where one of the flag's resources was: it is gone, the gradient is stale.
		building->dirtyGradient[slot]=true;
		if (gradientStats) gradientStats->clearingGoalGone();
		return false;
	}
	if (buildingGradientDirection(random, building, swimClass, x, y, dx, dy, true, route))
		return true;
	if (building->lastGlobalGradientUpdateStepCounter[slot]+STUCK_REBUILD_TICKS>game->stepCounter)
		return buildingGradientDirection(random, building, swimClass, x, y, dx, dy, false, route);

	// Stuck for a while: the gradient may be stale. A scheduled refresh keeps
	// sidestepping on the old field until it publishes; otherwise rebuild now.
	if (requestBuildingRefresh(building, slot))
		return buildingGradientDirection(random, building, swimClass, x, y, dx, dy, false, route);
	noteBuildingSyncReason(BuildingSyncReason::Other);
	if (gradientStats) gradientStats->setPendingReason(BuildingGradientStats::Reason::Stuck);
	updateGlobalGradient(building, swimClass, route);
	if (building->locked[building->routeAccess(swimClass, route)])
		return false;
	if (buildingGradientDirection(random, building, swimClass, x, y, dx, dy, true, route))
		return true;
	return buildingGradientDirection(random, building, swimClass, x, y, dx, dy, false, route);
}
