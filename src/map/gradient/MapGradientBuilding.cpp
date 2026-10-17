// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "BuildingType.h"
#include "Game.h"
#include "Unit.h"
#include "MapInternal.h"
#include "gradient/GradientRuntime.h"
#include "BuildingGradientSearch.h"
#include "SharedBuildingGradient.h"
#include <algorithm>
#include <array>
#include "Team.h"
#include "SnapshotGradient.h"
#include "SeedCells.h"



void Map::finishBuildingGradient(Building *building, int swimClass, BuildingRoute route) const
{
	const int slot=building->routeSlot(swimClass,route);
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
    if (auto &search = building->globalGradientSearch[slot]) search->finish();
}

// updateGlobalGradient(Building*): the full-map gradient toward a building, a
// flag's zone or, for a clearing flag, the clearable resources in its range.

void Map::updateGlobalGradient(Building *building, int swimClass, BuildingRoute route)
{
	PERF_SCOPE_TIME(BuildingGradient);
	route = building->resolveRoute(route);
	const int slot = building->routeSlot(swimClass, route);
    auto guard=SharedBuildingGradient::lock(building->sharedGradient[slot]);
	const int access = building->routeAccess(swimClass, route);
	assert(building);
	assert(building->type);

	Uint16 *gradient=building->globalGradient[slot];
	assert(gradient);
	// Diagnostics read the replaced field and its search before reinitialization.
	// A synchronous build supersedes any queued or pending scheduled refresh.
	building->supersedeGradient(slot);
	gradientRuntime->countSynchronous();
	if (const auto &previous = building->globalGradientSearch[slot])
		building->settledCostHint[slot] = Uint16(std::min(previous->requiredCost(), 0xFFFE));
	if (building->sharedGradient[slot])
	{
		building->retireSharedGradient(slot);
		building->globalGradient[slot] = gradient = acquireBuildingGradientBuffer();
	}
    // A rebuild replaces the old search and its frozen terrain snapshot.
	// Keep bucket capacity when possible; a locked field has no pending search.
	building->dirtyGradient[slot]=false;
	building->lastGlobalGradientUpdateStepCounter[slot]=game->stepCounter;
	building->gradientGeneration[slot]=topologyGeneration;

    const auto seed=gradient_preparation::captureBuildingSeed(*building,swimClass,route);
    const auto seeded=gradient_preparation::buildingCells(stateView(),seed,gradient,
        [&](auto fn) { initializeGradientCells(fn); });
    if(seeded.resourceState) building->anyResourceToClear[swimAccessVariant(swimClass)]=seeded.resourceState;

	const auto routeInputs = BuildingGradientSearch::Inputs::of(*this, swimClass);

	if (route == BuildingRoute::Footprint)
	{
        const bool reachable=!seeded.locked;
        building->setLocked(access,!reachable);
		if (!reachable)
		{
			recycleBuildingGradientSearch(std::move(building->globalGradientSearch[slot]));
			return;
		}
	}
	else
		building->setLocked(access,false);

	auto &search = building->globalGradientSearch[slot];
	if (!search) search = acquireBuildingGradientSearch();
	search->begin(routeInputs, gradient, swimClass, w, h);
}
