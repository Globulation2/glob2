// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainProperties.h"

// Placement and growth are separate: a scenario may plant a crop on terrain
// where it never regrows. The resource's immutable category supplies the final
// permission for permanent deposits, without consulting occupancy or RNG.
constexpr bool terrainSupportsResource(const TerrainProperties& terrain, int resourceType,
	bool shrinkable)
{
	return resourceType >= 0 && resourceType < 16 &&
		(terrain.allowedResources & (1u << resourceType)) &&
		(shrinkable || terrain.nonGrowingResources);
}
