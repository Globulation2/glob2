// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainGroup.h"
#include "TerrainType.h"
#include <array>
#include <cstdint>

// One row per built-in TerrainType: its group (and therefore its simulation
// profile and experiment), external name, string-table label and semantic
// colours. TerrainProperties.h, TerrainPresentation.h and TerrainExperiments.h
// all derive from this table, so
// adding a look is one enumerator in TerrainType.h, one row here, one string-table
// label and one material binding in data/terrain/tileset.json.
struct TerrainColor
{
	std::uint8_t r, g, b;
};

struct TerrainTypeDefinition
{
	TerrainType id;
	TerrainGroup group;
	// External name for scripts, files and material bindings; label is a string-table key.
	const char *name, *label;
	TerrainColor minimap, overview, image, preview;
};

namespace terrain_table_detail
{
constexpr TerrainTypeDefinition authored(TerrainType id, TerrainGroup group, const char *name,
										  const char *label, TerrainColor minimap,
										  TerrainColor mid)
{
	return {id, group, name, label, minimap, mid, mid, minimap};
}
} // namespace terrain_table_detail

inline constexpr auto TERRAIN_TYPES = []
{
	using namespace terrain_table_detail;
	using G = TerrainGroup;
	std::array<TerrainTypeDefinition, TERRAIN_COUNT> rows{{
		// Frozen built-ins: names and colours are file and script contracts.
		{WATER, G::Water, "water", "[water]", {0, 40, 120}, {70, 50, 191}, {0, 64, 255}, {0, 40, 120}},
		{SAND, G::Sand, "sand", "[sand]", {170, 170, 0}, {182, 168, 48}, {240, 220, 140}, {170, 170, 0}},
		{GRASS, G::Grass, "grass", "[grass]", {0, 90, 0}, {30, 113, 30}, {0, 128, 0}, {0, 90, 0}},
		{ICE, G::Ice, "ice", "[ice]", {190, 225, 240}, {190, 225, 240}, {190, 225, 240}, {190, 225, 240}},
		// Trail retains its legacy external name/key for scripts and files.
		{TRAIL, G::Paths, "road", "[road]", {176, 138, 98}, {176, 138, 98}, {176, 138, 98}, {176, 138, 98}},
		// Catalogue types: minimap colour, then the shared overview/image colour.
		authored(BOULDERS, G::Obstacles, "boulders", "[boulders]", {70, 72, 74}, {88, 90, 92}),
		authored(HEDGE, G::Obstacles, "hedge", "[hedge]", {30, 62, 34}, {38, 78, 40}),
		authored(THICKET, G::Obstacles, "thicket", "[thicket]", {42, 52, 30}, {54, 66, 34}),
		authored(RIDGE_ROCK, G::Ridges, "ridge_rock", "[ridge_rock]", {78, 84, 94}, {98, 104, 112}),
		authored(OUTCROP, G::Ridges, "outcrop", "[outcrop]", {90, 90, 86}, {110, 112, 108}),
		authored(DIRT, G::Barren, "dirt", "[dirt]", {110, 90, 64}, {128, 104, 74}),
		authored(CLAY, G::Barren, "clay", "[clay]", {140, 98, 72}, {158, 112, 84}),
		authored(GRAVEL, G::Barren, "gravel", "[gravel]", {120, 114, 104}, {138, 130, 118}),
		authored(FLOWER_MEADOW, G::Barren, "flower_meadow", "[flower_meadow]", {52, 118, 46}, {60, 126, 48}),
		authored(MUD, G::Rough, "mud", "[mud]", {82, 64, 48}, {96, 76, 56}),
		authored(MARSH, G::Rough, "marsh", "[marsh]", {62, 82, 66}, {74, 96, 78}),
		authored(DEEP_SNOW, G::Rough, "deep_snow", "[deep_snow]", {208, 216, 226}, {222, 228, 236}),
		authored(SCREE, G::Rough, "scree", "[scree]", {104, 108, 116}, {118, 122, 128}),
		authored(DIRT_TRACK, G::Paths, "dirt_track", "[dirt_track]", {156, 128, 92}, {170, 142, 104}),
		authored(BOARDWALK, G::Paths, "boardwalk", "[boardwalk]", {142, 110, 72}, {164, 128, 84}),
		authored(LAVA, G::Lava, "lava", "[lava]", {150, 60, 24}, {190, 80, 30}),
		authored(EMBER_FIELD, G::Lava, "ember_field", "[ember_field]", {70, 40, 32}, {90, 52, 38}),
		authored(LOAM, G::Fertile, "loam", "[loam]", {62, 52, 36}, {74, 62, 40}),
		authored(MOSS, G::Fertile, "moss", "[moss]", {38, 80, 38}, {46, 96, 42}),
		authored(SPRING_MEADOW, G::Fertile, "spring_meadow", "[spring_meadow]", {80, 136, 52}, {92, 150, 58}),
		authored(DEEP_WATER, G::DeepWater, "deep_water", "[deep_water]", {8, 40, 120}, {10, 60, 160}),
		authored(DARK_WATER, G::DeepWater, "dark_water", "[dark_water]", {10, 30, 64}, {14, 38, 78}),
		authored(VOID_HOLE, G::Void, "void_hole", "[void_hole]", {8, 8, 12}, {10, 10, 14}),
		authored(CHASM, G::Void, "chasm", "[chasm]", {18, 14, 20}, {24, 18, 26}),
	}};
	return rows;
}();

inline constexpr const TerrainTypeDefinition &terrainTypeDefinition(TerrainType type)
{
	return TERRAIN_TYPES[unsigned(type)];
}
inline constexpr TerrainGroup terrainGroup(TerrainType type)
{
	return TERRAIN_TYPES[unsigned(type)].group;
}
// Paintable built-ins are editor brushes and import presets.
inline constexpr bool terrainPaintable(TerrainType type)
{
	return TERRAIN_GROUPS[unsigned(TERRAIN_TYPES[unsigned(type)].group)].paletteVisible;
}

namespace terrain_table_detail
{
constexpr bool sameText(const char *a, const char *b)
{
	for (; *a && *b; ++a, ++b)
		if (*a != *b)
			return false;
	return *a == *b;
}
constexpr bool sameColor(TerrainColor a, TerrainColor b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b;
}
constexpr bool frozenRow(const TerrainTypeDefinition &row, const char *name)
{
	return sameText(row.name, name);
}
} // namespace terrain_table_detail

static_assert(
	[]
	{
		using namespace terrain_table_detail;
		for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
		{
			const auto &row = TERRAIN_TYPES[i];
			if (unsigned(row.id) != i || unsigned(row.group) >= TERRAIN_GROUP_COUNT || !row.name ||
				!row.label)
				return false;
			for (unsigned j = 0; j < i; ++j)
			{
				const auto &other = TERRAIN_TYPES[j];
				if (sameText(row.name, other.name))
					return false;
				// Every paintable type is a distinct import/export colour.
				if (terrainPaintable(row.id) && terrainPaintable(other.id) && sameColor(row.image, other.image))
					return false;
			}
		}
		return true;
	}(),
	"Terrain rows need unique names and image colours");

// Scripts, files and material bindings name the classic rows.
static_assert(
	[]
	{
		using namespace terrain_table_detail;
		return frozenRow(TERRAIN_TYPES[WATER], "water") && frozenRow(TERRAIN_TYPES[SAND], "sand") &&
			   frozenRow(TERRAIN_TYPES[GRASS], "grass") && frozenRow(TERRAIN_TYPES[ICE], "ice") &&
			   frozenRow(TERRAIN_TYPES[TRAIL], "road");
	}(),
	"The classic terrain names are frozen");
