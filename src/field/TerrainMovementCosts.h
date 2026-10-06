// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "GradientCosts.h"
#include "map/TerrainProperties.h"
#include <array>
#include <cstdint>

namespace gradient_kernel
{
// Derive the hot binary profile discriminator from the property table. With
// one swimming terrain the compiler emits a single comparison; additional
// swimming definitions automatically use the small flag table instead.
inline constexpr auto SWIMMING_TERRAINS = [] {
    std::array<bool,TERRAIN_COUNT> flags{};
    for (unsigned t=0;t<TERRAIN_COUNT;++t) flags[t]=terrainProperties(static_cast<TerrainType>(t)).swimmable;
    return flags;
}();
inline constexpr int SOLE_SWIMMING_TERRAIN = [] {
    int found=-1;
    for(unsigned t=0;t<TERRAIN_COUNT;++t) if(SWIMMING_TERRAINS[t])
    { if(found>=0)return -1; found=t; }
    return found;
}();
// With water, deep water and dark water all swimmable, the classic per-cell
// predicate stays a shift and mask rather than a table load.
inline constexpr std::uint64_t SWIMMING_TERRAIN_MASK = [] {
    std::uint64_t mask=0;
    for(unsigned t=0;t<TERRAIN_COUNT && t<64;++t) if(SWIMMING_TERRAINS[t]) mask|=std::uint64_t(1)<<t;
    return mask;
}();
constexpr bool terrainUsesSwimming(TerrainType type)
{
    if constexpr (SOLE_SWIMMING_TERRAIN>=0) return unsigned(type)==unsigned(SOLE_SWIMMING_TERRAIN);
    else if constexpr (TERRAIN_COUNT<=64) return (SWIMMING_TERRAIN_MASK>>unsigned(type))&1u;
    else return SWIMMING_TERRAINS[type];
}

// The same destination-entry costs drive eager/lazy fields, A* and direction
// selection. Movement profiles retain the established seven swimming ratios.
constexpr unsigned scaledTerrainStep(unsigned base, unsigned speedQ8)
{
    return std::max(1u, (base * 256u + speedQ8 / 2u) / speedQ8);
}
using TerrainEntryCosts = std::array<EntrySteps, TERRAIN_COUNT>;
constexpr TerrainEntryCosts terrainEntryCosts(int swim)
{
    TerrainEntryCosts result{};
    for (unsigned t = 0; t < TERRAIN_COUNT; ++t)
    {
        const auto &p = terrainProperties(static_cast<TerrainType>(t));
        const unsigned base = p.swimmable && swim > 0 ? WATER_STEP[swim] : GRADIENT_STEP;
        result[t] = entrySteps(scaledTerrainStep(base, p.groundSpeedQ8));
    }
    return result;
}
inline constexpr auto TERRAIN_ENTRY_COSTS = [] {
    std::array<TerrainEntryCosts, std::size(WATER_STEP)> result{};
    for (unsigned sw = 0; sw < result.size(); ++sw) result[sw] = terrainEntryCosts(sw);
    return result;
}();
// Keep the legacy admissible heuristic even when a built-in material is absent.
inline constexpr auto MINIMUM_TERRAIN_ENTRY_COSTS = []
{
	std::array<unsigned, std::size(WATER_STEP)> result{};
	for (unsigned sw = 0; sw < result.size(); ++sw)
	{
		result[sw] = GRADIENT_STEP;
		for (unsigned t = 0; t < TERRAIN_COUNT; ++t)
			if (TERRAIN_PROPERTIES[t].walkable || (sw && TERRAIN_PROPERTIES[t].swimmable))
				result[sw] = std::min(result[sw], TERRAIN_ENTRY_COSTS[sw][t].cardinal);
	}
	return result;
}();
static_assert([] {
    for (const auto &profile : TERRAIN_ENTRY_COSTS)
        for (const auto &cost : profile)
            if (cost.cardinal == 0 || cost.diagonal >= BUCKETS) return false;
    return true;
}(), "Terrain movement cost exceeds the gradient queue ring; enlarge GradientBucket::COUNT");
}
