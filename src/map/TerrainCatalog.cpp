// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerrainCatalog.h"
#include "TerrainBuiltins.h"
#include "TerrainGroupTable.h"
#include <array>

// Adding a look is one enumerator in TerrainType.h with its group in
// TerrainGroupTable.h, one row here, one string-table label and one material file
// and binding in the terrain catalog (data/terrain/), plus a file-format gate and
// the previous built-in count in io/TerrainFormatHistory.h, since custom terrain
// IDs follow the built-ins (TerrainRegistry::currentTerrainId).
namespace
{
// A row without its group, which TerrainGroupTable.h owns.
struct Row
{
	TerrainType id;
	const char *name, *label;
	TerrainColor minimap, overview, image, preview;
};

constexpr Row authored(TerrainType id, const char *name, const char *label, TerrainColor minimap, TerrainColor mid)
{
	return {id, name, label, minimap, mid, mid, minimap};
}

constexpr auto TERRAIN_TYPES = []
{
	const std::array<Row, TERRAIN_COUNT> rows{{
		// Frozen built-ins: names and colours are file and script contracts.
		{WATER, "water", "[water]", {0, 40, 120}, {70, 50, 191}, {0, 64, 255}, {0, 40, 120}},
		{SAND, "sand", "[sand]", {170, 170, 0}, {182, 168, 48}, {240, 220, 140}, {170, 170, 0}},
		{GRASS, "grass", "[grass]", {0, 90, 0}, {30, 113, 30}, {0, 128, 0}, {0, 90, 0}},
		{ICE, "ice", "[ice]", {190, 225, 240}, {190, 225, 240}, {190, 225, 240}, {190, 225, 240}},
		// Trail retains its legacy external name/key for scripts and files.
		{TRAIL, "road", "[road]", {176, 138, 98}, {176, 138, 98}, {176, 138, 98}, {176, 138, 98}},
		// Catalogue types: minimap colour, then the shared overview/image colour.
		authored(BOULDERS, "boulders", "[boulders]", {70, 72, 74}, {88, 90, 92}),
		authored(HEDGE, "hedge", "[hedge]", {30, 62, 34}, {38, 78, 40}),
		authored(THICKET, "thicket", "[thicket]", {42, 52, 30}, {54, 66, 34}),
		authored(RIDGE_ROCK, "ridge_rock", "[ridge_rock]", {78, 84, 94}, {98, 104, 112}),
		authored(OUTCROP, "outcrop", "[outcrop]", {90, 90, 86}, {110, 112, 108}),
		authored(DIRT, "dirt", "[dirt]", {110, 90, 64}, {128, 104, 74}),
		authored(CLAY, "clay", "[clay]", {140, 98, 72}, {158, 112, 84}),
		authored(SHALE, "shale", "[shale]", {120, 114, 104}, {138, 130, 118}),
		// Heath, mudflat and brick path keep their pre-redesign image colours: exported
		// map images import catalogue types by exact colour.
		{HEATH, "heath", "[heath]", {104, 72, 86}, {118, 86, 100}, {60, 126, 48}, {104, 72, 86}},
		authored(MUD, "mud", "[mud]", {82, 64, 48}, {96, 76, 56}),
		{MUDFLAT, "mudflat", "[mudflat]", {140, 126, 108}, {156, 142, 123}, {74, 96, 78}, {140, 126, 108}},
		authored(DEEP_SNOW, "deep_snow", "[deep_snow]", {208, 216, 226}, {222, 228, 236}),
		authored(SCREE, "scree", "[scree]", {104, 108, 116}, {118, 122, 128}),
		{BRICK_PATH, "brick_path", "[brick_path]", {120, 90, 82}, {136, 103, 94}, {170, 142, 104}, {120, 90, 82}},
		authored(BOARDWALK, "boardwalk", "[boardwalk]", {142, 110, 72}, {164, 128, 84}),
		authored(LAVA, "lava", "[lava]", {150, 60, 24}, {190, 80, 30}),
		authored(EMBER_FIELD, "ember_field", "[ember_field]", {70, 40, 32}, {90, 52, 38}),
		authored(LOAM, "loam", "[loam]", {62, 52, 36}, {74, 62, 40}),
		authored(MOSS, "moss", "[moss]", {38, 80, 38}, {46, 96, 42}),
		authored(SPRING_MEADOW, "spring_meadow", "[spring_meadow]", {80, 136, 52}, {92, 150, 58}),
		authored(DEEP_WATER, "deep_water", "[deep_water]", {8, 40, 120}, {10, 60, 160}),
		authored(DARK_WATER, "dark_water", "[dark_water]", {10, 30, 64}, {14, 38, 78}),
		authored(VOID_HOLE, "void_hole", "[void_hole]", {8, 8, 12}, {10, 10, 14}),
		authored(CHASM, "chasm", "[chasm]", {18, 14, 20}, {24, 18, 26}),
		authored(LUSH_GRASS, "lush_grass", "[lush_grass]", {25, 101, 49}, {25, 101, 49}),
		authored(DRY_GRASS, "dry_grass", "[dry_grass]", {120, 123, 52}, {120, 123, 52}),
		authored(DUNE_SAND, "dune_sand", "[dune_sand]", {204, 151, 85}, {204, 151, 85}),
		authored(PALE_SAND, "pale_sand", "[pale_sand]", {211, 202, 168}, {211, 202, 168}),
		// Laid ochre and grey flagstones; dark porous rock beside lava and ember fields.
		authored(SANDSTONE, "sandstone", "[sandstone]", {136, 126, 98}, {150, 140, 110}),
		authored(PUMICE, "pumice", "[pumice]", {44, 42, 42}, {58, 55, 54}),
		// Ice over a lake: plays exactly like ice.
		authored(FROZEN_LAKE, "frozen_lake", "[frozen_lake]", {128, 160, 174}, {141, 172, 185}),
		// Snow carved by the wind into drifts: plays exactly like deep snow.
		authored(WIND_SWEPT_SNOW, "wind_swept_snow", "[wind_swept_snow]", {202, 206, 226}, {202, 206, 226}),
		// Solid ochre bedrock worn into rounded slabs: plays exactly like dirt.
		authored(WEATHERED_SANDSTONE, "weathered_sandstone", "[weathered_sandstone]", {150, 124, 76}, {166, 138, 86}),
		// Sand in wind ripples: plays exactly like sand.
		authored(WINDY_SAND_DUNE, "windy_sand_dune", "[windy_sand_dune]", {212, 182, 134}, {214, 184, 136}),
		// Pale baked crust in small cracked pillows: plays exactly like mud and scree.
		authored(HARDPAN, "hardpan", "[hardpan]", {196, 188, 176}, {212, 204, 192}),
		// Olive moss, grey stones and dark cracked earth: plays exactly like dirt.
		authored(TUNDRA, "tundra", "[tundra]", {82, 78, 62}, {91, 87, 69}),
		// Charcoal ash with porous black stones: plays exactly like dirt.
		authored(VOLCANIC_ASH, "volcanic_ash", "[volcanic_ash]", {58, 56, 56}, {72, 70, 69}),
		// Blue glacier ice crossed by white fractures: plays exactly like ice.
		authored(GLACIER_ICE, "glacier_ice", "[glacier_ice]", {93, 158, 207}, {96, 160, 210}),
		// Hexagonal road setts reclaimed by moss: plays exactly like road.
		authored(OVERGROWN_ROAD, "overgrown_road", "[overgrown_road]", {122, 118, 98}, {134, 128, 106}),
		// Iron-red earth packed with hardened nodules: plays exactly like dirt.
		authored(LATERITE, "laterite", "[laterite]", {150, 76, 46}, {166, 90, 54}),
		// A raised rock plateau walled by cliffs: plays exactly like hedge and thicket.
		authored(CLIFFS, "cliffs", "[cliffs]", {118, 92, 70}, {136, 108, 82}),
		// Red-hot crust on bright yellow molten channels: plays exactly like lava.
		authored(MOLTEN_LAVA_POOL, "molten_lava_pool", "[molten_lava_pool]", {214, 74, 14}, {183, 63, 12}),
		// Blue-grey slate slabs in random ashlar: plays exactly like road.
		authored(SLATE_ASHLAR, "slate_ashlar", "[slate_ashlar]", {91, 94, 97}, {104, 108, 118}),
		// Polygonal basalt in pale ash joints: plays exactly like road.
		authored(BASALT_CAUSEWAY, "basalt_causeway", "[basalt_causeway]", {63, 61, 60}, {76, 74, 76}),
		// Dressed porous scoria in basketweave: plays exactly like road.
		authored(SCORIA_BLOCKS, "scoria_blocks", "[scoria_blocks]", {74, 68, 63}, {88, 76, 68}),
		// Glacier ice blocks in ashlar under drifting snow: plays exactly like road.
		authored(RIME_ASHLAR, "rime_ashlar", "[rime_ashlar]", {181, 187, 212}, {181, 187, 212}),
		// White marble lozenges with moonstone keystones: plays exactly like road.
		authored(MOONSTONE_LOZENGE, "moonstone_lozenge", "[moonstone_lozenge]", {190, 192, 209}, {190, 192, 209}),
		// Frosted quartz pavers in a pinwheel around ice squares: plays exactly like road.
		authored(FROST_PINWHEEL, "frost_pinwheel", "[frost_pinwheel]", {185, 188, 210}, {185, 188, 210}),
		// Snow drifts, grey stones and frozen cracked earth: plays exactly like dirt.
		authored(ARCTIC_TUNDRA, "arctic_tundra", "[arctic_tundra]", {137, 136, 141}, {137, 136, 141}),
		// A raised snowfield walled by blue ice: plays exactly like cliffs.
		authored(GLACIER, "glacier", "[glacier]", {196, 212, 230}, {176, 200, 226}),
		// The other Barriers masses play exactly like cliffs; the Outcrops looks like rock spires.
		authored(ICEFALL, "icefall", "[icefall]", {120, 172, 210}, {148, 196, 226}),
		authored(SANDSTONE_BENCHES, "sandstone_benches", "[sandstone_benches]", {196, 150, 104}, {214, 170, 124}),
		authored(OBSIDIAN_RIDGE, "obsidian_ridge", "[obsidian_ridge]", {46, 46, 56}, {62, 62, 74}),
		authored(GRANITE_TORS, "granite_tors", "[granite_tors]", {120, 124, 104}, {140, 140, 124}),
		authored(TRAVERTINE_TERRACES, "travertine_terraces", "[travertine_terraces]", {214, 206, 186}, {226, 218, 200}),
		authored(PENITENTES, "penitentes", "[penitentes]", {204, 218, 236}, {222, 232, 246}),
		authored(TERMITE_MOUNDS, "termite_mounds", "[termite_mounds]", {150, 96, 62}, {170, 112, 72}),
		authored(HOODOOS, "hoodoos", "[hoodoos]", {204, 140, 98}, {222, 160, 116}),
		authored(PETRIFIED_STUMPS, "petrified_stumps", "[petrified_stumps]", {124, 110, 92}, {142, 128, 108}),
	}};
	std::array<TerrainTypeDefinition, TERRAIN_COUNT> definitions{};
	for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
	{
		const auto &row = rows[i];
		definitions[i] = {row.id, terrainGroup(row.id), row.name, row.label, row.minimap, row.overview, row.image,
						  row.preview};
	}
	return definitions;
}();

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

static_assert(
	[]
	{
		for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
		{
			const auto &row = TERRAIN_TYPES[i];
			if (unsigned(row.id) != i || !row.name || !row.label)
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
static_assert(sameText(TERRAIN_TYPES[WATER].name, "water") && sameText(TERRAIN_TYPES[SAND].name, "sand") &&
				  sameText(TERRAIN_TYPES[GRASS].name, "grass") && sameText(TERRAIN_TYPES[ICE].name, "ice") &&
				  sameText(TERRAIN_TYPES[TRAIL].name, "road"),
			  "The classic terrain names are frozen");

// Earlier external names of renamed built-ins (see terrainTypeByName). Never
// write them: files and reports use the row name.
struct LegacyTerrainName
{
	const char *name;
	TerrainType type;
};
constexpr std::array<LegacyTerrainName, 4> LEGACY_TERRAIN_NAMES{{
	{"gravel", SHALE},
	{"flower_meadow", HEATH},
	{"marsh", MUDFLAT},
	{"dirt_track", BRICK_PATH},
}};

static_assert(
	[]
	{
		for (const auto &legacy : LEGACY_TERRAIN_NAMES)
			for (const auto &row : TERRAIN_TYPES)
				if (sameText(legacy.name, row.name))
					return false;
		return true;
	}(),
	"A legacy terrain name must not shadow a current one");

constexpr std::array<TerrainPresentation, TERRAIN_COUNT> presentations()
{
	std::array<TerrainPresentation, TERRAIN_COUNT> definitions{};
	for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
	{
		const auto &row = TERRAIN_TYPES[i];
		definitions[i] = {row.name, row.label, row.minimap, row.overview, row.image, row.preview,
						  terrainPaintable(TerrainType(i))};
	}
	return definitions;
}
} // namespace

// Constant initialization: static constructors elsewhere (the built-in registry)
// may read it before any dynamic initializer of this file runs.
constinit const std::array<TerrainPresentation, TERRAIN_COUNT> TerrainPresentations = presentations();

const TerrainTypeDefinition &terrainTypeDefinition(TerrainType type)
{
	return TERRAIN_TYPES[unsigned(type)];
}

std::optional<TerrainType> terrainTypeByName(std::string_view name)
{
	for (const auto &row : TERRAIN_TYPES)
		if (name == row.name)
			return row.id;
	for (const auto &legacy : LEGACY_TERRAIN_NAMES)
		if (name == legacy.name)
			return legacy.type;
	return std::nullopt;
}
