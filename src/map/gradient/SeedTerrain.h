// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Map.h"
#include "MapInternal.h"
#include <array>

namespace gradient_preparation
{
struct SeedTerrain
{
	Uint16 open;
	Uint8 farmCrop;
};

// The common built-in registry fits a stack table. Runtime-defined terrain uses
// its compiled cell properties without allocating an unbounded per-call table.
// Dispatch outside the grid loop lets both resource and clearing kernels retain
// their simple built-in lookup without rejecting custom terrain identities.
template<class Function> void withTerrain(const Map &map, bool canSwim, Function seed)
{
	auto policy = [canSwim](const TerrainProperties &terrain) {
		return SeedTerrain{Uint16(terrain.walkable || (canSwim && terrain.swimmable)
			? GRADIENT_UNREACHABLE : GRADIENT_FORBIDDEN), terrain.farmCrop};
	};
	if (map.terrainRegistry().size() == TERRAIN_COUNT)
	{
		std::array<SeedTerrain, TERRAIN_COUNT> table;
		for (unsigned type = 0; type < table.size(); ++type)
			table[type] = policy(map.terrainProperties(static_cast<TerrainType>(type)));
		seed([&](size_t index) { return table[map.terrainTypeAt(index)]; });
	}
	else
		seed([&](size_t index) { return policy(map.terrainPropertiesAt(index)); });
}
}
