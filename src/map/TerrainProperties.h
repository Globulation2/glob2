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
// Uniform terrain profiles remain unchanged.
static_assert(TERRAIN_PROPERTIES[ICE].groundSpeedQ8 == 128 && TERRAIN_PROPERTIES[ICE].groundHealthQ8 == -8 &&
	TERRAIN_PROPERTIES[TRAIL].groundSpeedQ8 == 512 && TERRAIN_PROPERTIES[TRAIL].buildable &&
	TERRAIN_PROPERTIES[SAND].inhibitionQ8 == 256 && TERRAIN_PROPERTIES[SAND].shoreSupportQ8 == 256 &&
	TERRAIN_PROPERTIES[WATER].swimmable && TERRAIN_PROPERTIES[WATER].fertilityQ8 == 256 &&
	TERRAIN_PROPERTIES[GRASS].buildable && TERRAIN_PROPERTIES[GRASS].farmMaterial == materialIndex(MaterialId::Food));

// Uniform built-ins keep their profiles; mixed shores now blend numeric
// ecology modifiers rather than inheriting the retired fixed shore profile.
static_assert([] {
	const auto& g = TERRAIN_PROPERTIES[GRASS];
	const auto& s = TERRAIN_PROPERTIES[SAND];
	const auto& w = TERRAIN_PROPERTIES[WATER];
	const auto beach = combineCornerRules(s, s, w, w);
	const auto gardenEdge = combineCornerRules(g, g, s, s);
	return beach.walkable && !beach.swimmable && !beach.buildable && beach.shoreline &&
		beach.inhibitionQ8 == 128 && beach.shoreSupportQ8 == 128 && beach.fertilityQ8 == 128 &&
		gardenEdge.inhibitionQ8 == 128 && gardenEdge.shoreSupportQ8 == 128;
}());
// Grass directly against water is legal; an even split favors walking.
static_assert(combineCornerRules(TERRAIN_PROPERTIES[GRASS], TERRAIN_PROPERTIES[GRASS],
	TERRAIN_PROPERTIES[WATER], TERRAIN_PROPERTIES[WATER]).walkable);
static_assert([] {
	for (unsigned a = 0; a < TERRAIN_COUNT; ++a)
		for (unsigned b = 0; b < TERRAIN_COUNT; ++b)
			if (!validTerrainProperties(combineCornerRules(TERRAIN_PROPERTIES[a], TERRAIN_PROPERTIES[a],
														   TERRAIN_PROPERTIES[b], TERRAIN_PROPERTIES[b])))
				return false;
	return true;
}(), "Every built-in pair must combine into valid rules");
