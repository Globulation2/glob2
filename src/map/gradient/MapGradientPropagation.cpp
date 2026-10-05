// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/GradientRuntime.h"
#include "field/RuntimeTerrainGradient.h"

static_assert(std::size(gradient_kernel::WATER_STEP) == SWIM_CLASS_COUNT);
static_assert(gradient_kernel::COST_LIMIT == Map::GRADIENT_COST_LIMIT);

// Seeds are the cells above GRADIENT_UNREACHABLE; each starts at its own cost
// (0 for GRADIENT_AT_GOAL, anything up to COST_LIMIT otherwise, e.g. a resource
// tile seeded with its distance to a building). Seeds within one bucket rotation
// start in the queue; dearer ones join it when the frontier reaches their cost.
// Reseed before reuse; a propagated field is not a valid seed buffer.
// GRADIENT_FORBIDDEN cells are obstacles.
void Map::propagateGradient(Uint16 *gradient, int swimClass, int maxCost)
{
	PERF_SCOPE_TIME(Propagation);
	if (!hasTerrainMovementModifiers() && terrainRegistry().size() > TERRAIN_COUNT &&
		gradient_kernel::weightedClass(swimClass))
	{
		const auto water = frozenWaterSnapshot();
		gradient_kernel::propagateField(gradient, swimClass, maxCost, {getW(), getH()},
										gradientRuntime->workspaces[compute.slot()].propagation,
										[water = water->data()](size_t i)
										{ return water[i] != 0; });
		return;
	}
	if (hasTerrainMovementModifiers() && terrainRegistry().size() > TERRAIN_COUNT)
	{
		const auto profiles = frozenTerrainMovementSnapshot(swimClass);
		gradient_kernel::propagateTerrainProfiles(
			gradient, swimClass, maxCost, {getW(), getH()},
			gradientRuntime->workspaces[compute.slot()].propagation, profiles->data(),
			profiles->movement, terrainQueueBuckets());
		return;
	}
	gradient_kernel::propagateTerrainField(
		gradient, swimClass, maxCost, {getW(), getH()},
		gradientRuntime->workspaces[compute.slot()].propagation,
		[this](size_t i) { return terrainTypeAt(i); }, hasTerrainMovementModifiers(),
		terrainRegistry(), terrainQueueBuckets());
}
