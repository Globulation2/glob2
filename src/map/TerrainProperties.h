// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TerrainPropertiesLayout.h"
#include "TerrainTypeTable.h"
#include <array>
#include <cassert>
#include <cstdint>

// Compile-time built-in profiles, derived from each type's group so every member
// of a group is byte-identical. Runtime registries preserve this layout.
inline constexpr auto TERRAIN_PROPERTIES = [] {
	std::array<TerrainProperties, TERRAIN_COUNT> definitions{};
	for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
		definitions[i] = TERRAIN_GROUPS[unsigned(TERRAIN_TYPES[i].group)].properties;
	return definitions;
}();

constexpr bool validTerrainType(unsigned value) { return value < TERRAIN_COUNT; }
constexpr const TerrainProperties& terrainProperties(TerrainType type)
{
	assert(validTerrainType(type));
	return TERRAIN_PROPERTIES[static_cast<unsigned>(type)];
}

static_assert([] { for (const auto& p : TERRAIN_PROPERTIES) if (!validTerrainProperties(p)) return false; return true; }());
// Classic rules are frozen: the legacy types keep their exact profiles.
static_assert(TERRAIN_PROPERTIES[ICE].groundSpeedQ8 == 128 && TERRAIN_PROPERTIES[ICE].groundHealthQ8 == -8 &&
	TERRAIN_PROPERTIES[TRAIL].groundSpeedQ8 == 512 && TERRAIN_PROPERTIES[TRAIL].buildable &&
	TERRAIN_PROPERTIES[SAND].inhibitionQ8 == 256 && TERRAIN_PROPERTIES[SAND].shoreSupportQ8 == 256 &&
	TERRAIN_PROPERTIES[WATER].swimmable && TERRAIN_PROPERTIES[WATER].fertilityQ8 == 256 &&
	TERRAIN_PROPERTIES[GRASS].buildable && TERRAIN_PROPERTIES[GRASS].farmMaterial == materialIndex(MaterialId::Food) &&
	TERRAIN_PROPERTIES[GRASS_SAND_SHORE].shoreline && !TERRAIN_PROPERTIES[GRASS_SAND_SHORE].buildable);

// Only old-format import and the legacy corner editor use this adapter.
constexpr TerrainType legacyTerrainType(std::uint16_t sprite)
{
	return sprite < 16 ? GRASS : sprite < 128 ? GRASS_SAND_SHORE :
		sprite < 144 ? SAND : sprite < 256 ? SAND_WATER_SHORE : WATER;
}
