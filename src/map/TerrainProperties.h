// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TerrainPropertiesLayout.h"
#include "TerrainGroupTable.h"
#include <array>
#include <cassert>
#include <cstdint>

// Compile-time built-in profiles, derived from each type's group so every member
// of a group is byte-identical. Runtime registries preserve this layout.
inline constexpr auto TERRAIN_PROPERTIES = [] {
	std::array<TerrainProperties, TERRAIN_COUNT> definitions{};
	for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
		definitions[i] = TERRAIN_GROUPS[unsigned(TERRAIN_TYPE_GROUPS[i].group)].properties;
	return definitions;
}();

constexpr bool validTerrainType(unsigned value) { return value < TERRAIN_COUNT; }
constexpr const TerrainProperties& terrainProperties(TerrainType type)
{
	assert(validTerrainType(type));
	return TERRAIN_PROPERTIES[static_cast<unsigned>(type)];
}

// Compile-time checks of these tables live in TerrainProperties.cpp, so that
// the translation units including this header do not each re-evaluate them.
