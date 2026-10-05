// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "BuildingType.h"
#include "Game.h"
#include "Unit.h"
#include "MapInternal.h"
#include "BuildingGradientSearch.h"
#include "BuildingGradientDiagnostics.h"
#include "GradientRuntime.h"

#include "BuildingGradientCapture.h"
using building_gradient::refreshDescription;

void Map::finishBuildingGradient(Building *building, int swimClass, const char *caller) const
{
	if (auto &search = building->globalGradientSearch[swimClass])
		search->finish(caller);
}

// updateGlobalGradient(Building*): the full-map gradient toward a building, a
// flag's zone or, for a clearing flag, the clearable resources in its range.
// updateRoundTripGradient: the gradient of the trip to a resource and on to
// the building.

void Map::updateGlobalGradient(Building *building, int swimClass, const char *reason)
{
	PERF_SCOPE_TIME(BuildingGradient);
	assert(building);
	assert(building->type);
	BuildingGradientDiagnostics::Scope evidence(buildingGradientDiagnostics(), building->gid,
												swimClass, "rebuild", reason, topologyGeneration,
												topologyGeneration);
	Uint16 *gradient = building->globalGradient[swimClass];
	assert(gradient);
	const auto destination = refreshDescription(*building, swimClass, 0);
	building_gradient::Terrain geometry;
	geometry.width = getW();
	geometry.height = getH();
	auto cellAt = [&](std::size_t i)
	{
		return building_gradient::Cell{
			tiles[i].forbidden,
			tiles[i].building,
			tiles[i].resource.type,
			immobileUnits[i],
			Uint8(tiles[i].building == NOGBID ? 0 : Building::GIDtoTeam(tiles[i].building)), terrainTypeAt(i)};
	};
	building->dirtyGradient[swimClass]=false;
	building->lastGlobalGradientUpdateStepCounter[swimClass]=game->stepCounter;
	building->gradientGeneration[swimClass]=topologyGeneration;
	if (destination.virtualBuilding)
	{
		std::fill(gradient, gradient + size, GRADIENT_UNREACHABLE);
		const auto resourceState =
			building_gradient::paintGoals(geometry, destination, gradient, cellAt);
		if (destination.clearing)
			building->anyResourceToClear[buildingAccessIndex(swimClass)] = resourceState;
	}
	initializeGradientCells(
		[&](std::size_t begin, std::size_t end)
		{
			for (auto i = begin; i < end; ++i)
				gradient[i] = building_gradient::seedCell(
					cellAt(i), destination,
					destination.virtualBuilding ? gradient[i] : GRADIENT_UNREACHABLE);
		});
	building->locked[buildingAccessIndex(swimClass)] =
		building_gradient::isLocked(geometry, destination, gradient);
	if (building->locked[buildingAccessIndex(swimClass)])
	{
		recycleBuildingGradientSearch(std::move(building->globalGradientSearch[swimClass]));
		return;
	}
	auto &search = building->globalGradientSearch[swimClass];
	if (!search) search = acquireBuildingGradientSearch();
	search->begin(*this, gradient, swimClass, building->gid);
}


void Map::updateRoundTripGradient(Building *building, int resourceType, int swimClass)
{
	PERF_SCOPE_TIME(RoundTripGradient);
	BuildingGradientDiagnostics::Scope evidence(
		buildingGradientDiagnostics(), building->gid, swimClass, "round_trip", "construct",
		topologyGeneration, building->gradientGeneration[swimClass]);
	// Only construction needs the parent in full; reading a cached round-trip
	// field must not force a newly refreshed walking field to finish.
	finishBuildingGradient(building, swimClass, "round_trip");
	Uint16 *gradient=building->roundTripGradient[resourceType][swimClass];
	assert(gradient);
	building->roundTripGradientStep[resourceType][swimClass]=game->stepCounter;
	const Uint16 *toBuilding=building->globalGradient[swimClass];
	const Uint16 *toResource=getResourceGradient(building->owner->teamNumber, resourceType, swimClass);
	// Same obstacles as the resource gradient. A resource tile is seeded with
	// the cost of carrying from the cheapest free cell next to it, where the
	// unit harvests, to the building.
	building_gradient::Terrain geometry;
	geometry.width = getW();
	geometry.height = getH();
	propagateGradient(gradient, swimClass,
					  building_gradient::seedTrip(geometry, toResource, toBuilding, gradient));
}
