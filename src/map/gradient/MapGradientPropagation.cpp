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
	// Cells carry rules, not built-in types: unmodified costs only need to
	// know which cells swim, and modified ones the map's compact profiles.
	if (!hasTerrainMovementModifiers())
	{
		const auto water = frozenWaterSnapshot();
		gradient_kernel::propagateField(gradient, swimClass, maxCost, {getW(), getH()},
										gradientRuntime->workspaces[compute.slot()].propagation,
										[water = water->data()](size_t i)
										{ return water[i] != 0; });
		return;
	}
	const auto profiles = frozenTerrainMovementSnapshot(swimClass);
	gradient_kernel::propagateTerrainProfiles(
		gradient, swimClass, maxCost, {getW(), getH()},
		gradientRuntime->workspaces[compute.slot()].propagation, profiles->data(),
		profiles->movement, terrainQueueBuckets());
}
