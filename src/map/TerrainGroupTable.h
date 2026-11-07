// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainGroup.h"
#include "TerrainType.h"
#include <array>

// The group of each built-in TerrainType, and therefore its simulation profile
// (TerrainProperties.h). Per-cell rules and generators read it, so it stays a
// constexpr table; names, labels and colours live in TerrainCatalog.cpp, whose
// edits rebuild only that file.
struct TerrainTypeGroup
{
	TerrainType id;
	TerrainGroup group;
};

inline constexpr std::array<TerrainTypeGroup, TERRAIN_COUNT> TERRAIN_TYPE_GROUPS = []
{
	using G = TerrainGroup;
	return std::array<TerrainTypeGroup, TERRAIN_COUNT>{{
		{WATER, G::Water},
		{SAND, G::Sand},
		{GRASS, G::Grass},
		{ICE, G::Ice},
		{TRAIL, G::Paths},
		{BOULDERS, G::Obstacles},
		{HEDGE, G::Obstacles},
		{THICKET, G::Obstacles},
		{RIDGE_ROCK, G::Ridges},
		{OUTCROP, G::Ridges},
		{DIRT, G::Barren},
		{CLAY, G::Barren},
		{SHALE, G::Barren},
		{HEATH, G::Barren},
		{MUD, G::Rough},
		{MUDFLAT, G::Rough},
		{DEEP_SNOW, G::Ice},
		{SCREE, G::Rough},
		{BRICK_PATH, G::Paths},
		{BOARDWALK, G::Paths},
		{LAVA, G::Lava},
		{EMBER_FIELD, G::Lava},
		{LOAM, G::Fertile},
		{MOSS, G::Fertile},
		{SPRING_MEADOW, G::Fertile},
		{DEEP_WATER, G::DeepWater},
		{DARK_WATER, G::DeepWater},
		{VOID_HOLE, G::Void},
		{CHASM, G::Void},
		{LUSH_GRASS, G::Grass},
		{DRY_GRASS, G::Grass},
		{DUNE_SAND, G::Sand},
		{PALE_SAND, G::Sand},
		{SANDSTONE, G::Paths},
		{PUMICE, G::Rough},
		{FROZEN_LAKE, G::Ice},
		{WIND_SWEPT_SNOW, G::Ice},
		{WEATHERED_SANDSTONE, G::Barren},
		{WINDY_SAND_DUNE, G::Sand},
		{HARDPAN, G::Rough},
		{TUNDRA, G::Barren},
		{VOLCANIC_ASH, G::Barren},
		{GLACIER_ICE, G::Ice},
		{OVERGROWN_ROAD, G::Paths},
		{LATERITE, G::Barren},
		{CLIFFS, G::Obstacles},
		{MOLTEN_LAVA_POOL, G::Lava},
		{SLATE_ASHLAR, G::Paths},
		{BASALT_CAUSEWAY, G::Paths},
		{SCORIA_BLOCKS, G::Paths},
		{RIME_ASHLAR, G::Paths},
		{MOONSTONE_LOZENGE, G::Paths},
		{FROST_PINWHEEL, G::Paths},
		{ARCTIC_TUNDRA, G::Barren},
		{GLACIER, G::Obstacles},
		{ICEFALL, G::Obstacles},
		{SANDSTONE_BENCHES, G::Obstacles},
		{OBSIDIAN_RIDGE, G::Obstacles},
		{GRANITE_TORS, G::Obstacles},
		{TRAVERTINE_TERRACES, G::Obstacles},
		{PENITENTES, G::Ridges},
		{TERMITE_MOUNDS, G::Ridges},
		{HOODOOS, G::Ridges},
		{PETRIFIED_STUMPS, G::Ridges},
	}};
}();

static_assert(
	[]
	{
		for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
			if (unsigned(TERRAIN_TYPE_GROUPS[i].id) != i || unsigned(TERRAIN_TYPE_GROUPS[i].group) >= TERRAIN_GROUP_COUNT)
				return false;
		return true;
	}(),
	"One group row per built-in, in identity order");

inline constexpr TerrainGroup terrainGroup(TerrainType type)
{
	return TERRAIN_TYPE_GROUPS[unsigned(type)].group;
}
// Paintable built-ins are editor brushes and import presets.
inline constexpr bool terrainPaintable(TerrainType type)
{
	return TERRAIN_GROUPS[unsigned(terrainGroup(type))].paletteVisible;
}
