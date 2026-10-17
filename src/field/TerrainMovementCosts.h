// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "GradientCosts.h"
#include "unit/UnitConsts.h"
#include "TerrainHazardCost.h"
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
    // Only built-in IDs reach this classic path; anything else is land here.
    else if constexpr (TERRAIN_COUNT<=64) return unsigned(type)<TERRAIN_COUNT && ((SWIMMING_TERRAIN_MASK>>unsigned(type))&1u);
    else return unsigned(type)<TERRAIN_COUNT && SWIMMING_TERRAINS[type];
}

// The same destination-entry costs drive eager/lazy fields, A* and direction
// selection. Movement profiles retain the established seven swimming ratios.
constexpr bool terrainAllowsGround(const TerrainProperties& terrain, int swim)
{
    return swim==WATER_ONLY_CLASS ? terrain.swimmable : terrain.walkable || (swim>0 && terrain.swimmable);
}
constexpr bool terrainAllowsMovement(const TerrainProperties& terrain, int movement)
{
    return movement==AIR_MOVEMENT_CLASS ? terrain.flyable : terrainAllowsGround(terrain,movement);
}
constexpr unsigned scaledTerrainStep(unsigned base, unsigned speedQ8)
{
    return std::max(1u, (base * 256u + speedQ8 / 2u) / speedQ8);
}
// Damage-weighted cost of entering a cell with these rules, by swim class.
constexpr EntrySteps terrainEntrySteps(const TerrainProperties &p, int swim)
{
    if (swim==AIR_MOVEMENT_CLASS)
        return entrySteps(hazardRouteCost(scaledTerrainStep(GRADIENT_STEP,p.airSpeedQ8),p.airHealthQ8));
    const unsigned base = p.swimmable && swim > 0 ? WATER_STEP[swim] : GRADIENT_STEP;
    return entrySteps(hazardRouteCost(scaledTerrainStep(base, p.groundSpeedQ8), p.groundHealthQ8));
}
using TerrainEntryCosts = std::array<EntrySteps, TERRAIN_COUNT>;
constexpr TerrainEntryCosts terrainEntryCosts(int swim)
{
    TerrainEntryCosts result{};
    for (unsigned t = 0; t < TERRAIN_COUNT; ++t)
        result[t] = terrainEntrySteps(terrainProperties(static_cast<TerrainType>(t)), swim);
    return result;
}
inline constexpr auto TERRAIN_ENTRY_COSTS = [] {
    std::array<TerrainEntryCosts, SWIM_CLASS_COUNT> result{};
    for (unsigned sw = 0; sw < result.size(); ++sw) result[sw] = terrainEntryCosts(sw);
    return result;
}();
// Keep the legacy admissible heuristic even when a built-in material is absent.
inline constexpr auto MINIMUM_TERRAIN_ENTRY_COSTS = []
{
	std::array<unsigned, SWIM_CLASS_COUNT> result{};
	for (unsigned sw = 0; sw < result.size(); ++sw)
	{
		result[sw] = GRADIENT_STEP;
		for (unsigned t = 0; t < TERRAIN_COUNT; ++t)
			if (terrainAllowsMovement(TERRAIN_PROPERTIES[t], sw))
				result[sw] = std::min(result[sw], TERRAIN_ENTRY_COSTS[sw][t].cardinal);
	}
	return result;
}();
// Map-owned profiles append air without changing the classic ground kernel's
// constexpr ratio table or its 64-bucket fast path.
inline constexpr auto MINIMUM_MOVEMENT_ENTRY_COSTS=[] {
    std::array<unsigned,MOVEMENT_CLASS_COUNT> result{};
    for(unsigned sw=0;sw<SWIM_CLASS_COUNT;++sw) result[sw]=MINIMUM_TERRAIN_ENTRY_COSTS[sw];
    result[AIR_MOVEMENT_CLASS]=GRADIENT_STEP;
    for(const auto& p:TERRAIN_PROPERTIES) if(p.flyable)
        result[AIR_MOVEMENT_CLASS]=std::min(result[AIR_MOVEMENT_CLASS],terrainEntrySteps(p,AIR_MOVEMENT_CLASS).cardinal);
    return result;
}();
static_assert([] {
    for (const auto &profile : TERRAIN_ENTRY_COSTS)
        for (const auto &cost : profile)
            if (cost.cardinal == 0 || cost.diagonal >= BUCKETS) return false;
    return true;
}(), "Terrain movement cost exceeds the gradient queue ring; enlarge GradientBucket::COUNT");
}
