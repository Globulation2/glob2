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
#include "gradient/GradientRuntime.h"
#include "BuildingGradientSearch.h"
#include "SharedBuildingGradient.h"



// Building pathfinding (buildingGradient, buildingAvailable, pathfindBuilding)

namespace {

// A unit that cannot make progress forces a rebuild at most this often (~5 s).
constexpr Uint32 STUCK_REBUILD_TICKS = 128;


} // namespace

FieldReadiness Map::requestBuildingField(Building* building,int swimClass,BuildingRoute route)
{
    assert(building);
    ensureBuildingGradientPipeline();
    const int slot=building->routeSlot(swimClass,route);
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
    const Uint32 now=game->stepCounter;
    building->globalGradientUsedStep[slot]=now;
    const bool cold=!building->globalGradient[slot];
    const auto last=building->lastGlobalGradientUpdateStepCounter[slot];
    const bool dirty=(building->dirtyGradient[slot] || building->gradientGeneration[slot]!=topologyGeneration)
        && last+GRADIENT_DIRTY_REBUILD_TICKS<=now;
    const bool clearing=building->resolveRoute(route)==BuildingRoute::Clearing && last+CLEARING_FLAG_REFRESH_TICKS<=now;
    if (cold || dirty || clearing) {
        if (!requestBuildingRefresh(building,slot)) {
            if (cold) building->globalGradient[slot]=acquireBuildingGradientBuffer();
            updateGlobalGradient(building,swimClass,route);
        }
    }
    return building->globalGradient[slot] ? FieldReadiness::Ready : FieldReadiness::Pending;
}
bool Map::prepareBuildingGradient(Building* building,int swimClass,BuildingRoute route)
{
    return requestBuildingField(building,swimClass,route)==FieldReadiness::Ready
        && !building->isLocked(building->routeAccess(swimClass,route));
}
const Uint16* Map::lookupBuildingField(const Building* building,int swim,BuildingRoute route) const
{
    return building->globalGradient[building->routeSlot(swim,route)];
}
RouteResult Map::queryBuildingDistance(Building* building,int swim,int x,int y,int* distance,BuildingRoute route)
{
    if (requestBuildingField(building,swim,route)==FieldReadiness::Pending) return RouteResult::Pending;
    return buildingAvailable(building,swim,x,y,distance,route) ? RouteResult::Reachable : RouteResult::Unreachable;
}
StepResult Map::queryBuildingStep(EntityRandom& random,Building* building,int swim,int x,int y,int* dx,int* dy,BuildingRoute route)
{
    if (requestBuildingField(building,swim,route)==FieldReadiness::Pending) return StepResult::Pending;
    return pathfindBuilding(random,building,swim,x,y,dx,dy,route) ? StepResult::Step : StepResult::Blocked;
}

const Uint16 *Map::buildingGradient(Building *building, int swimClass, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
	if (!prepareBuildingGradient(building, swimClass, route)) return NULL;
	finishBuildingGradient(building, swimClass, route);
	return building->globalGradient[slot];
}

std::shared_ptr<SharedBuildingGradient> Map::leaseBuildingRoute(Building *building, int swimClass, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
	// Observation borrows a usable generation without enrolling, allocating,
	// seeding or refreshing a field. Live admission owns normal preparation.
	if (!building->globalGradient[slot] || building->dirtyGradient[slot] ||
		building->gradientGeneration[slot] != topologyGeneration ||
		(building->resolveRoute(route) == BuildingRoute::Clearing &&
		 building->lastGlobalGradientUpdateStepCounter[slot] + CLEARING_FLAG_REFRESH_TICKS <= game->stepCounter))
		return {};
	return building->leaseGradient(slot);
}

Uint16 Map::buildingGradientValue(Building *building, int swimClass, size_t cell, BuildingRoute route) const
{
	const int slot = building->routeSlot(swimClass, route);
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
	assert(building->globalGradient[slot]);
	assert(cell < size);
    if(auto& search=building->globalGradientSearch[slot]) {
        if(auto& stats=gradientRuntime->workStats) {
            const auto before=search->settledCost();const bool complete=search->complete();
            search->resolve(cell);
            if(before!=search->settledCost() || complete!=search->complete())
                stats->partial(GradientWorkStats::buildingKey(building->owner->teamNumber,swimClass,Building::GIDtoID(building->gid),building->scriptIdentity,unsigned(building->resolveRoute(route))),game->stepCounter,before,search->settledCost());
        } else search->resolve(cell);
    }
	return building->globalGradient[slot][cell];
}

bool Map::buildingGradientDirection(EntityRandom& random, Building *building, int swimClass, int x, int y,
	int *dx, int *dy, bool strict, BuildingRoute route) const
{
	const int slot = building->routeSlot(swimClass, route);
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
	// Settling this layer also settles all equal/better neighbors the generic
	// direction picker can select. Keep partial-array access inside this adapter.
	buildingGradientValue(building, swimClass, coordToIndex(x, y), route);
	return directionByGradient(random, building->owner->me, swimClass, x, y,
		building->globalGradient[slot], dx, dy, strict);
}

bool Map::buildingAvailable(Building *building, int swimClass, int x, int y, int *dist, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
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
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
	PERF_SCOPE_TIME(PathBuilding);
	assert(building);
	assert(x>=0);
	assert(y>=0);
	if (requestBuildingField(building,swimClass,route)==FieldReadiness::Pending) return false;
	if (swimClass!=AIR_MOVEMENT_CLASS && ((areaCells[coordToIndex(x, y)].forbidden) & building->owner->me)!=0)
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
	updateGlobalGradient(building, swimClass, route);
	if (building->isLocked(building->routeAccess(swimClass, route)))
		return false;
	if (buildingGradientDirection(random, building, swimClass, x, y, dx, dy, true, route))
		return true;
	return buildingGradientDirection(random, building, swimClass, x, y, dx, dy, false, route);
}
