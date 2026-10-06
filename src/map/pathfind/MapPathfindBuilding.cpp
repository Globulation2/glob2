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


} // namespace

bool Map::prepareBuildingGradient(Building *building, int swimClass, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
	assert(building);
	Uint16 *&gradient=building->globalGradient[slot];
	Uint32 lastUpdate=building->lastGlobalGradientUpdateStepCounter[slot];
	Uint32 now=game->stepCounter;
	building->globalGradientUsedStep[slot]=now;
	bool rebuild=false;
	if (gradient==NULL)
	{
		gradient=acquireBuildingGradientBuffer();
		rebuild=true;
	}
	else if ((building->dirtyGradient[slot] || building->gradientGeneration[slot]!=topologyGeneration)
		&& lastUpdate+GRADIENT_DIRTY_REBUILD_TICKS<=now)
		rebuild=true;
	// A clearing flag's goals are resources, which grow and get cleared.
	else if (building->resolveRoute(route) == BuildingRoute::Clearing && lastUpdate+CLEARING_FLAG_REFRESH_TICKS<=now)
		rebuild=true;
	if (rebuild)
		updateGlobalGradient(building, swimClass, route);
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

bool Map::buildingGradientDirection(Building *building, int swimClass, int x, int y,
	int *dx, int *dy, bool strict, BuildingRoute route) const
{
	const int slot = building->routeSlot(swimClass, route);
	// Settling this layer also settles all equal/better neighbors the generic
	// direction picker can select. Keep partial-array access inside this adapter.
	buildingGradientValue(building, swimClass, coordToIndex(x, y), route);
	return directionByGradient(building->owner->me, swimClass, x, y,
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


const Uint16 *Map::roundTripGradient(Building *building, int resourceType, int swimClass)
{
	if (!prepareBuildingGradient(building, swimClass, BuildingRoute::Footprint))
		return NULL;
	Uint32 now=game->stepCounter;
	Uint16 *&gradient=building->roundTripGradient[resourceType][swimClass];
	building->roundTripGradientUsedStep[resourceType][swimClass]=now;
	if (gradient!=NULL && building->roundTripGradientStep[resourceType][swimClass]+ROUND_TRIP_REFRESH_TICKS>now)
		return gradient;
	if (gradient==NULL)
		gradient=acquireBuildingGradientBuffer();
	updateRoundTripGradient(building, resourceType, swimClass);
	return gradient;
}

bool Map::roundTripDistance(Building *building, int resourceType, int swimClass, int x, int y, int *dist)
{
	PERF_SCOPE_TIME(PathBuilding);
	// Only gradients a fetcher keeps alive: hiring looks at every needed
	// resource of every building, far more than ever get fetched.
	const Uint16 *gradient=building->roundTripGradient[resourceType][swimClass];
	if (gradient==NULL)
		return false;
	// It may be a few ticks older than the resource gradient the callers walk
	// by; never report a resource that one says is gone.
	if (!materialAvailable(building->owner->teamNumber, resourceType, swimClass, x, y, false, building))
		return false;
	building->roundTripGradientUsedStep[resourceType][swimClass]=game->stepCounter;
	Uint16 g=gradient[coordToIndex(x, y)];
	if (g<=GRADIENT_UNREACHABLE)
		return false;
	*dist=gradientTiles(g);
	return true;
}


bool Map::pathfindBuilding(Building *building, int swimClass, int x, int y, int *dx, int *dy, BuildingRoute route)
{
	const int slot = building->routeSlot(swimClass, route);
	PERF_SCOPE_TIME(PathBuilding);
	assert(building);
	assert(x>=0);
	assert(y>=0);
	if (((tiles[coordToIndex(x, y)].forbidden) & building->owner->me)!=0)
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
	if (buildingGradientDirection(building, swimClass, x, y, dx, dy, true, route))
		return true;
	if (building->lastGlobalGradientUpdateStepCounter[slot]+STUCK_REBUILD_TICKS>game->stepCounter)
		return buildingGradientDirection(building, swimClass, x, y, dx, dy, false, route);

	// Stuck for a while: the gradient may be stale, rebuild it now.
	updateGlobalGradient(building, swimClass, route);
	if (building->locked[building->routeAccess(swimClass, route)])
		return false;
	if (buildingGradientDirection(building, swimClass, x, y, dx, dy, true, route))
		return true;
	return buildingGradientDirection(building, swimClass, x, y, dx, dy, false, route);
}

void Map::advanceHiringGradients(Building *building)
{
	if (!computeEnabled(ComputeHiring)) return;
	++hiringPrepasses;
	// Most callers have at most one active class. Avoid a full unit scan when
	// there cannot be an independent pair of searches to advance.
	unsigned incomplete = 0;
	for (int swim=0; swim<SWIM_CLASS_COUNT; ++swim)
		if (const auto &search=building->globalGradientSearch[swim]; search && !search->complete()) ++incomplete;
	if (incomplete < 2) return;
	std::array<std::vector<size_t>, SWIM_CLASS_COUNT> targets;
	for (int n = 0; n < Unit::MAX_COUNT; ++n)
	{
		const Unit *unit = building->owner->myUnits[n];
		if (!unit || !unit->performance[HARVEST] || unit->activity != Unit::ACT_RANDOM
			|| unit->medical != Unit::MED_FREE) continue;
		const int swim = unit->swimClass();
		const auto &search = building->globalGradientSearch[swim];
		if (search && !search->complete()) targets[swim].push_back(coordToIndex(unit->posX, unit->posY));
	}
	std::vector<int> jobs;
	for (int swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
		if (!targets[swim].empty()) jobs.push_back(swim);
	// One field offers no inter-field parallelism; do not do speculative work.
	if (jobs.size() < 2) return;
	std::vector<std::uint64_t> popped(jobs.size());
	computeExecutor().run(jobs.size(), [&](size_t i) {
		const int swim = jobs[i];
		const auto before = building->globalGradientSearch[swim]->poppedEntries();
		for (size_t cell : targets[swim]) building->globalGradientSearch[swim]->resolve(cell);
		popped[i] = building->globalGradientSearch[swim]->poppedEntries() - before;
	});
	for (auto count : popped) hiringPoppedEntries += count;
}
