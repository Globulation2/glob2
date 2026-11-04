// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainGroup.h"
#include "TerrainType.h"
#include <array>
#include <cstdint>
#include <string_view>

// One row per built-in TerrainType: its group (and therefore its simulation
// profile), external name, string-table label and semantic colours.
// TerrainProperties.h and TerrainPresentation.h derive from this table, so
// adding a look is one enumerator in TerrainType.h, one row here, one string-table
// label and one material file and binding in the terrain catalog, plus a file-format
// gate, since custom terrain IDs follow the built-ins (TerrainRegistry::currentTerrainId).
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
		authored(SHALE, G::Barren, "shale", "[shale]", {120, 114, 104}, {138, 130, 118}),
		// Heath, mudflat and brick path keep their pre-redesign image colours: exported
		// map images import catalogue types by exact colour.
		{HEATH, G::Barren, "heath", "[heath]", {104, 72, 86}, {118, 86, 100}, {60, 126, 48}, {104, 72, 86}},
		authored(MUD, G::Rough, "mud", "[mud]", {82, 64, 48}, {96, 76, 56}),
		{MUDFLAT, G::Rough, "mudflat", "[mudflat]", {140, 126, 108}, {156, 142, 123}, {74, 96, 78}, {140, 126, 108}},
		authored(DEEP_SNOW, G::Ice, "deep_snow", "[deep_snow]", {208, 216, 226}, {222, 228, 236}),
		authored(SCREE, G::Rough, "scree", "[scree]", {104, 108, 116}, {118, 122, 128}),
		{BRICK_PATH, G::Paths, "brick_path", "[brick_path]", {120, 90, 82}, {136, 103, 94}, {170, 142, 104}, {120, 90, 82}},
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
		authored(LUSH_GRASS, G::Grass, "lush_grass", "[lush_grass]", {25, 101, 49}, {25, 101, 49}),
		authored(DRY_GRASS, G::Grass, "dry_grass", "[dry_grass]", {120, 123, 52}, {120, 123, 52}),
		authored(DUNE_SAND, G::Sand, "dune_sand", "[dune_sand]", {204, 151, 85}, {204, 151, 85}),
		authored(PALE_SAND, G::Sand, "pale_sand", "[pale_sand]", {211, 202, 168}, {211, 202, 168}),
		// Laid ochre and grey flagstones; dark porous rock beside lava and ember fields.
		authored(SANDSTONE, G::Paths, "sandstone", "[sandstone]", {136, 126, 98}, {150, 140, 110}),
		authored(PUMICE, G::Rough, "pumice", "[pumice]", {44, 42, 42}, {58, 55, 54}),
		// Ice over a lake: plays exactly like ice.
		authored(FROZEN_LAKE, G::Ice, "frozen_lake", "[frozen_lake]", {128, 160, 174}, {141, 172, 185}),
		// Snow carved by the wind into drifts: plays exactly like deep snow.
		authored(WIND_SWEPT_SNOW, G::Ice, "wind_swept_snow", "[wind_swept_snow]", {202, 206, 226}, {202, 206, 226}),
		// Solid ochre bedrock worn into rounded slabs: plays exactly like dirt.
		authored(WEATHERED_SANDSTONE, G::Barren, "weathered_sandstone", "[weathered_sandstone]", {150, 124, 76},
				 {166, 138, 86}),
		// Sand in wind ripples: plays exactly like sand.
		authored(WINDY_SAND_DUNE, G::Sand, "windy_sand_dune", "[windy_sand_dune]", {212, 182, 134}, {214, 184, 136}),
		// Pale baked crust in small cracked pillows: plays exactly like mud and scree.
		authored(HARDPAN, G::Rough, "hardpan", "[hardpan]", {196, 188, 176}, {212, 204, 192}),
		// Olive moss, grey stones and dark cracked earth: plays exactly like dirt.
		authored(TUNDRA, G::Barren, "tundra", "[tundra]", {82, 78, 62}, {91, 87, 69}),
		// Charcoal ash with porous black stones: plays exactly like dirt.
		authored(VOLCANIC_ASH, G::Barren, "volcanic_ash", "[volcanic_ash]", {58, 56, 56}, {72, 70, 69}),
		// Blue glacier ice crossed by white fractures: plays exactly like ice.
		authored(GLACIER_ICE, G::Ice, "glacier_ice", "[glacier_ice]", {93, 158, 207}, {96, 160, 210}),
		// Hexagonal road setts reclaimed by moss: plays exactly like road.
		authored(OVERGROWN_ROAD, G::Paths, "overgrown_road", "[overgrown_road]", {122, 118, 98}, {134, 128, 106}),
		// Iron-red earth packed with hardened nodules: plays exactly like dirt.
		authored(LATERITE, G::Barren, "laterite", "[laterite]", {150, 76, 46}, {166, 90, 54}),
		// A raised rock plateau walled by cliffs: plays exactly like hedge and thicket.
		authored(CLIFFS, G::Obstacles, "cliffs", "[cliffs]", {118, 92, 70}, {136, 108, 82}),
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

// Earlier external names of renamed built-ins. Saved custom registries (their
// base and appearance), map sets, scripts, editor actions and authoring JSON may
// still name a type by one of these, so every name-to-type lookup resolves them
// through terrainTypeByName. Never write them: files and reports use the row name.
struct LegacyTerrainName
{
	const char *name;
	TerrainType type;
};
inline constexpr std::array<LegacyTerrainName, 4> LEGACY_TERRAIN_NAMES{{
	{"gravel", SHALE},
	{"flower_meadow", HEATH},
	{"marsh", MUDFLAT},
	{"dirt_track", BRICK_PATH},
}};

static_assert(
	[]
	{
		using namespace terrain_table_detail;
		for (const auto &legacy : LEGACY_TERRAIN_NAMES)
			for (const auto &row : TERRAIN_TYPES)
				if (sameText(legacy.name, row.name))
					return false;
		return true;
	}(),
	"A legacy terrain name must not shadow a current one");

// The built-in that a current or legacy external name denotes, or TERRAIN_COUNT
// when there is none. Callers apply their own paintable or registry checks.
inline constexpr TerrainType terrainTypeByName(std::string_view name)
{
	for (const auto &row : TERRAIN_TYPES)
		if (name == row.name)
			return row.id;
	for (const auto &legacy : LEGACY_TERRAIN_NAMES)
		if (name == legacy.name)
			return legacy.type;
	return TERRAIN_COUNT;
}
