// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TerrainType.h"
#include <array>
#include <cassert>
#include <cstdint>

// Q8 factors use 256 for one. Health is signed HP per tick in the same scale.
// Keep presentation out of this table: hot simulation queries only need these
// immutable, compiler-visible values. Resource masks use the resource wire IDs.
struct TerrainProperties
{
	bool walkable = false, swimmable = false, flyable = true;
	bool resourcesGrow = false, fertilitySource = false, nonGrowingResources = false;
	bool buildable = false, projectileBlocks = false, shoreline = false;
	std::uint16_t groundSpeedQ8 = 256, airSpeedQ8 = 256;
	std::int16_t groundHealthQ8 = 0, airHealthQ8 = 0;
	std::uint16_t growthQ8 = 256;
	std::int16_t fertilityQ8 = 0;
	std::uint16_t inhibitionQ8 = 0, shoreSupportQ8 = 0;
	std::uint16_t allowedResources = 0;
	std::uint8_t farmCrop = 255;
};

inline constexpr auto TERRAIN_PROPERTIES = [] {
	std::array<TerrainProperties, TERRAIN_COUNT> definitions{};
	auto& water = definitions[WATER];
	water.swimmable = true;
	water.resourcesGrow = water.fertilitySource = true;
	water.fertilityQ8 = 256;
	water.allowedResources = (1u << 4); // algae
	water.farmCrop = 4;
	auto& sand = definitions[SAND];
	sand.walkable = sand.shoreline = true;
	sand.inhibitionQ8 = sand.shoreSupportQ8 = 256;
	auto& grass = definitions[GRASS];
	grass.walkable = grass.resourcesGrow = grass.nonGrowingResources = grass.buildable = true;
	grass.allowedResources = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) |
		(1u << 5) | (1u << 6) | (1u << 7);
	grass.farmCrop = 1;
	auto& ice = definitions[ICE];
	ice.walkable = true;
	ice.groundSpeedQ8 = 128;
	ice.groundHealthQ8 = -8;
	auto& trail = definitions[TRAIL];
	trail.walkable = trail.buildable = true;
	trail.groundSpeedQ8 = 512;
	definitions[GRASS_SAND_SHORE].walkable = definitions[GRASS_SAND_SHORE].shoreline = true;
	definitions[SAND_WATER_SHORE].walkable = definitions[SAND_WATER_SHORE].shoreline = true;
	return definitions;
}();

constexpr bool validTerrainType(unsigned value) { return value < TERRAIN_COUNT; }
constexpr const TerrainProperties& terrainProperties(TerrainType type)
{
	assert(validTerrainType(type));
	return TERRAIN_PROPERTIES[static_cast<unsigned>(type)];
}

// Irrigation heuristics need a beneficial source, not merely an enabled
// (possibly zero or negative) contribution to the signed ecology field.
constexpr bool terrainProvidesFertility(const TerrainProperties& p)
{
    return p.fertilitySource && p.fertilityQ8>0;
}

constexpr bool validTerrainProperties(const TerrainProperties& p)
{
	return !(p.walkable && p.swimmable) && (p.allowedResources & ~0xffu) == 0 &&
		p.groundSpeedQ8 >= 64 &&
		p.groundSpeedQ8 <= 1024 && p.airSpeedQ8 >= 64 && p.airSpeedQ8 <= 1024 &&
		p.growthQ8 <= 1024 && p.inhibitionQ8 <= 1024 && p.shoreSupportQ8 <= 1024 &&
		p.fertilityQ8 >= -1024 && p.fertilityQ8 <= 1024 &&
		(p.farmCrop == 255 || (p.farmCrop < 8 && (p.allowedResources & (1u << p.farmCrop))));
}
static_assert([] { for (const auto& p : TERRAIN_PROPERTIES) if (!validTerrainProperties(p)) return false; return true; }());

// Only old-format import and the legacy corner editor use this adapter.
constexpr TerrainType legacyTerrainType(std::uint16_t sprite)
{
	return sprite < 16 ? GRASS : sprite < 128 ? GRASS_SAND_SHORE :
		sprite < 144 ? SAND : sprite < 256 ? SAND_WATER_SHORE : WATER;
}
