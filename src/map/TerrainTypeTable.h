// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainGroup.h"
#include "TerrainType.h"
#include <array>
#include <cstdint>

// One row per built-in TerrainType: its group (and therefore its simulation
// profile and experiment), external name, string-table label, semantic colours
// and the frozen saved-frame contract. TerrainProperties.h, TerrainPresentation.h,
// TerrainCompatibility.h and TerrainExperiments.h all derive from this table, so
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
	// Saved Tile::terrain frame range. Frames are stable identities checked on
	// load and hashed into map checksums; they are not drawing frames.
	int firstFrame, variants;
	bool legacyCorners;
};

namespace terrain_table_detail
{
// Rows 7 onward take sixteen abstract frames each after the legacy ranges
// (0–303 classic tiles, 304–333 retired edge masks).
inline constexpr int AUTHORED_FRAME_BASE = 336;
inline constexpr int AUTHORED_FRAMES = 16;
constexpr TerrainTypeDefinition authored(TerrainType id, TerrainGroup group, const char *name,
										  const char *label, TerrainColor minimap,
										  TerrainColor mid)
{
	return {id,      group, name, label, minimap, mid, mid, minimap,
			AUTHORED_FRAME_BASE + AUTHORED_FRAMES * (int(id) - int(TERRAIN_COUNT_BEFORE_CATALOGUE)),
			AUTHORED_FRAMES, false};
}
} // namespace terrain_table_detail

inline constexpr auto TERRAIN_TYPES = []
{
	using namespace terrain_table_detail;
	using G = TerrainGroup;
	std::array<TerrainTypeDefinition, TERRAIN_COUNT> rows{{
		// Frozen built-ins: names, colours and frame ranges are saved-file contracts.
		{WATER, G::Water, "water", "[water]", {0, 40, 120}, {70, 50, 191}, {0, 64, 255}, {0, 40, 120}, 256, 16, true},
		{SAND, G::Sand, "sand", "[sand]", {170, 170, 0}, {182, 168, 48}, {240, 220, 140}, {170, 170, 0}, 128, 16, true},
		{GRASS, G::Grass, "grass", "[grass]", {0, 90, 0}, {30, 113, 30}, {0, 128, 0}, {0, 90, 0}, 0, 16, true},
		{ICE, G::Ice, "ice", "[ice]", {190, 225, 240}, {190, 225, 240}, {190, 225, 240}, {190, 225, 240}, 272, 16, false},
		// Trail retains its legacy external name/key for scripts and files.
		{TRAIL, G::Paths, "road", "[road]", {176, 138, 98}, {176, 138, 98}, {176, 138, 98}, {176, 138, 98}, 288, 16, false},
		{GRASS_SAND_SHORE, G::Shore, "grass_sand_border", "[sand]", {85, 130, 0}, {106, 140, 39}, {240, 220, 140}, {85, 130, 0}, 16, 112, true},
		{SAND_WATER_SHORE, G::Shore, "sand_water_border", "[sand]", {85, 105, 60}, {126, 109, 119}, {240, 220, 140}, {85, 105, 60}, 144, 112, true},
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
// Paintable built-ins are editor brushes and import presets; the legacy shore
// profiles are corner adapters that only old files and the corner editor produce.
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
constexpr bool frozenRow(const TerrainTypeDefinition &row, const char *name, int firstFrame,
						 int variants, bool legacyCorners)
{
	return sameText(row.name, name) && row.firstFrame == firstFrame &&
		   row.variants == variants && row.legacyCorners == legacyCorners;
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
				!row.label || row.firstFrame < 0 || row.variants <= 0 ||
				row.firstFrame + row.variants > 65536)
				return false;
			if (row.legacyCorners != !TERRAIN_GROUPS[unsigned(row.group)].paletteVisible &&
				i >= TERRAIN_COUNT_BEFORE_CATALOGUE)
				return false;
			for (unsigned j = 0; j < i; ++j)
			{
				const auto &other = TERRAIN_TYPES[j];
				if (sameText(row.name, other.name))
					return false;
				// Frame ranges are saved identities and must never overlap.
				if (row.firstFrame < other.firstFrame + other.variants &&
					other.firstFrame < row.firstFrame + row.variants)
					return false;
				// Image import resolves whole-cell colours by nearest match.
				if (!row.legacyCorners && !other.legacyCorners && sameColor(row.image, other.image))
					return false;
				// Every paintable type is a distinct import/export colour.
				if (terrainPaintable(row.id) && terrainPaintable(other.id) && sameColor(row.image, other.image))
					return false;
			}
		}
		return true;
	}(),
	"Terrain rows need unique names, image colours and non-overlapping saved frame ranges");

// Existing saves, replays and checksums depend on these seven rows byte for byte.
static_assert(
	[]
	{
		using namespace terrain_table_detail;
		return TERRAIN_COUNT_BEFORE_CATALOGUE == 7 &&
			   frozenRow(TERRAIN_TYPES[WATER], "water", 256, 16, true) &&
			   frozenRow(TERRAIN_TYPES[SAND], "sand", 128, 16, true) &&
			   frozenRow(TERRAIN_TYPES[GRASS], "grass", 0, 16, true) &&
			   frozenRow(TERRAIN_TYPES[ICE], "ice", 272, 16, false) &&
			   frozenRow(TERRAIN_TYPES[TRAIL], "road", 288, 16, false) &&
			   frozenRow(TERRAIN_TYPES[GRASS_SAND_SHORE], "grass_sand_border", 16, 112, true) &&
			   frozenRow(TERRAIN_TYPES[SAND_WATER_SHORE], "sand_water_border", 144, 112, true);
	}(),
	"Format-136 terrain identities are frozen");
