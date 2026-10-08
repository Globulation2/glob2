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
	TERRAIN_PROPERTIES[GRASS].buildable && TERRAIN_PROPERTIES[GRASS].farmMaterial == materialIndex(MaterialId::Food));

// The classic shore profile the retired GRASS_SAND_SHORE and SAND_WATER_SHORE
// types carried: walkable and a shoreline, with every other field at its default.
// Mixed grass/sand and sand/water cells must keep exactly these rules.
inline constexpr TerrainProperties CLASSIC_SHORE_PROPERTIES = [] {
	TerrainProperties shore;
	shore.walkable = shore.shoreline = true;
	return shore;
}();
static_assert([] {
	const auto& g = TERRAIN_PROPERTIES[GRASS];
	const auto& s = TERRAIN_PROPERTIES[SAND];
	const auto& w = TERRAIN_PROPERTIES[WATER];
	for (const auto& mixed : {combineCornerRules(g, g, g, s), combineCornerRules(g, s, s, s),
							  combineCornerRules(s, g, s, g), combineCornerRules(s, s, s, w),
							  combineCornerRules(w, w, s, w), combineCornerRules(w, s, s, w)})
		if (!sameTerrainProperties(mixed, CLASSIC_SHORE_PROPERTIES)) return false;
	return true;
}(), "Mixed classic cells must keep the frozen shore rules");
// Grass directly against water is a legal transition: walkable, never buildable.
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
