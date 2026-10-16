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
#include "SharedBuildingGradient.h"



// Building pathfinding (buildingGradient, buildingAvailable, pathfindBuilding)

namespace {

// A unit that cannot make progress forces a rebuild at most this often (~5 s).
constexpr Uint32 STUCK_REBUILD_TICKS = 128;


} // namespace

FieldReadiness Map::requestBuildingField(Building *building, int swimClass, BuildingRoute route)
{
	assert(building);
	if (compute.insideJob())
		return lookupBuildingField(building, swimClass, route) ? FieldReadiness::Ready : FieldReadiness::Pending;
	ensureBuildingGradientPipeline();
	const int slot = building->routeSlot(swimClass, route);
	const Uint32 now = game->stepCounter;
	building->globalGradientUsedStep[slot] = now;
	const bool cold = !building->globalGradient[slot];
	const bool dirty = (building->dirtyGradient[slot] || building->gradientGeneration[slot] != topologyGeneration)
		&& building->lastGlobalGradientUpdateStepCounter[slot] + GRADIENT_DIRTY_REBUILD_TICKS <= now;
	const bool clearing = building->resolveRoute(route) == BuildingRoute::Clearing
		&& building->lastGlobalGradientUpdateStepCounter[slot] + CLEARING_FLAG_REFRESH_TICKS <= now;
	if (cold || dirty || clearing)
	{
		if (!requestBuildingRefresh(building, slot))
		{
			// Legacy inactive-scheduler fallback. The building kernel still
			// requires a Game; Game-backed queries normally enroll above.
			if (!building->globalGradient[slot]) building->globalGradient[slot] = acquireBuildingGradientBuffer();
			updateGlobalGradient(building, swimClass, route);
			finishBuildingGradient(building, swimClass, route);
		}
	}
	return building->globalGradient[slot] ? FieldReadiness::Ready : FieldReadiness::Pending;
}

const Uint16 *Map::lookupBuildingField(const Building *building, int swimClass, BuildingRoute route) const
{
	return building->globalGradient[building->routeSlot(swimClass, route)];
}

bool Map::prepareBuildingGradient(Building *building, int swimClass, BuildingRoute route)
{
	return requestBuildingField(building, swimClass, route) == FieldReadiness::Ready
		&& !building->isLocked(building->routeAccess(swimClass, route));
}

const Uint16 *Map::buildingGradient(Building *building, int swimClass, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
	if (!prepareBuildingGradient(building, swimClass, route)) return NULL;
	return building->globalGradient[slot];
}

std::shared_ptr<SharedBuildingGradient> Map::leaseBuildingRoute(Building *building, int swimClass, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
	// Staleness does not invalidate the immutable published generation.
	if (!building->globalGradient[slot]) return {};
	return building->leaseGradient(slot);
}

Uint16 Map::buildingGradientValue(Building *building, int swimClass, size_t cell, BuildingRoute route) const
{
	const int slot = building->routeSlot(swimClass, route);
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
	assert(building->globalGradient[slot]);
	assert(cell < size);
	assert(!building->globalGradientSearch[slot] || building->globalGradientSearch[slot]->complete());
	return building->globalGradient[slot][cell];
}

bool Map::buildingGradientDirection(EntityRandom& random, Building *building, int swimClass, int x, int y,
	int *dx, int *dy, bool strict, BuildingRoute route) const
{
	const int slot = building->routeSlot(swimClass, route);
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
	// Published values and their neighbors are complete and immutable.
	buildingGradientValue(building, swimClass, coordToIndex(x, y), route);
	return directionByGradient(random, building->owner->me, swimClass, x, y,
		building->globalGradient[slot], dx, dy, strict);
}

RouteResult Map::queryBuildingDistance(Building *building, int swimClass, int x, int y, int *dist, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
	PERF_SCOPE_TIME(PathBuilding);
	if (requestBuildingField(building, swimClass, route) == FieldReadiness::Pending)
		return RouteResult::Pending;
	if (building->isLocked(building->routeAccess(swimClass, route))) return RouteResult::Unreachable;
	// The unit's own cell can be an obstacle in this building's field - it may be
	// standing on another building, or in a forbidden area - while a cell next to
	// it is on a route. Take the first of the nine that carries a distance.
	Uint16 g=buildingGradientValue(building, swimClass, coordToIndex(x, y), route);
	for (int d=0; d<8 && g<=GRADIENT_UNREACHABLE; d++)
		g=buildingGradientValue(building, swimClass, coordToIndex(x+tabClose[d][0], y+tabClose[d][1]), route);
	if (g<=GRADIENT_UNREACHABLE)
		return RouteResult::Unreachable;
	*dist=gradientTiles(g);
	return RouteResult::Reachable;
}


StepResult Map::queryBuildingStep(EntityRandom& random, Building *building, int swimClass, int x, int y, int *dx, int *dy, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
	PERF_SCOPE_TIME(PathBuilding);
	assert(building);
	assert(x>=0);
	assert(y>=0);
	if (requestBuildingField(building, swimClass, route) == FieldReadiness::Pending) return StepResult::Pending;
	if (((areaCells[coordToIndex(x, y)].forbidden) & building->owner->me)!=0)
	{
		// This escape path reads the cached field directly as a tie-breaker.
		// Preserve its old age (do not call buildingGradient here).
		return queryForbiddenStep(building->globalGradient[slot], building->owner->teamNumber, swimClass, x, y, dx, dy);
	}

	if (!prepareBuildingGradient(building, swimClass, route))
		return StepResult::Blocked;
	if (building->resolveRoute(route) == BuildingRoute::Clearing
		&& buildingGradientValue(building, swimClass, coordToIndex(x, y), route)==GRADIENT_AT_GOAL)
	{
		// Standing where one of the flag's resources was: it is gone, the gradient is stale.
		building->dirtyGradient[slot]=true;
		return StepResult::Blocked;
	}
	if (buildingGradientValue(building, swimClass, coordToIndex(x, y), route) == GRADIENT_AT_GOAL)
	{ *dx = *dy = 0; return StepResult::AtGoal; }
	if (buildingGradientDirection(random, building, swimClass, x, y, dx, dy, true, route))
		return StepResult::Step;
	if (building->lastGlobalGradientUpdateStepCounter[slot]+STUCK_REBUILD_TICKS>game->stepCounter)
		return buildingGradientDirection(random, building, swimClass, x, y, dx, dy, false, route) ? StepResult::Step : StepResult::Blocked;

	// Local blockage requests delayed guidance; it never runs a kernel here.
	requestBuildingRefresh(building, slot);
	return buildingGradientDirection(random, building, swimClass, x, y, dx, dy, false, route) ? StepResult::Step : StepResult::Blocked;
}

// Compatibility adapters for callers which cannot retain an assignment.
bool Map::buildingAvailable(Building *building, int swimClass, int x, int y, int *dist, BuildingRoute route)
{
	return queryBuildingDistance(building, swimClass, x, y, dist, route) == RouteResult::Reachable;
}
bool Map::pathfindBuilding(EntityRandom& random, Building *building, int swimClass, int x, int y, int *dx, int *dy, BuildingRoute route)
{
	return queryBuildingStep(random, building, swimClass, x, y, dx, dy, route) == StepResult::Step;
}
