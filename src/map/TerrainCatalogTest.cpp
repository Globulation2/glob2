// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "TerrainBuiltins.h"
#include "TerrainCatalog.h"
#include "TerrainGroupTable.h"
#include <array>
#include <string>

namespace
{
struct ExpectedTerrain
{
	const char *name, *label;
	TerrainGroup group;
	bool paintable;
	TerrainColor minimap, overview, image, preview;
};
using G = TerrainGroup;
// Golden rows for the first 64 built-ins. Names, labels and colours are file,
// script, material-binding and image-import contracts: change a row here only with
// a deliberate contract change. Later built-ins are checked for consistency only.
constexpr ExpectedTerrain EXPECTED[] = {
	{"water", "[water]", G::Water, true, {0, 40, 120}, {70, 50, 191}, {0, 64, 255}, {0, 40, 120}},
	{"sand", "[sand]", G::Sand, true, {170, 170, 0}, {182, 168, 48}, {240, 220, 140}, {170, 170, 0}},
	{"grass", "[grass]", G::Grass, true, {0, 90, 0}, {30, 113, 30}, {0, 128, 0}, {0, 90, 0}},
	{"ice", "[ice]", G::Ice, true, {190, 225, 240}, {190, 225, 240}, {190, 225, 240}, {190, 225, 240}},
	{"road", "[road]", G::Paths, true, {176, 138, 98}, {176, 138, 98}, {176, 138, 98}, {176, 138, 98}},
	{"boulders", "[boulders]", G::Obstacles, true, {70, 72, 74}, {88, 90, 92}, {88, 90, 92}, {70, 72, 74}},
	{"hedge", "[hedge]", G::Obstacles, true, {30, 62, 34}, {38, 78, 40}, {38, 78, 40}, {30, 62, 34}},
	{"thicket", "[thicket]", G::Obstacles, true, {42, 52, 30}, {54, 66, 34}, {54, 66, 34}, {42, 52, 30}},
	{"ridge_rock", "[ridge_rock]", G::Ridges, true, {78, 84, 94}, {98, 104, 112}, {98, 104, 112}, {78, 84, 94}},
	{"outcrop", "[outcrop]", G::Ridges, true, {90, 90, 86}, {110, 112, 108}, {110, 112, 108}, {90, 90, 86}},
	{"dirt", "[dirt]", G::Barren, true, {110, 90, 64}, {128, 104, 74}, {128, 104, 74}, {110, 90, 64}},
	{"clay", "[clay]", G::Barren, true, {140, 98, 72}, {158, 112, 84}, {158, 112, 84}, {140, 98, 72}},
	{"shale", "[shale]", G::Barren, true, {120, 114, 104}, {138, 130, 118}, {138, 130, 118}, {120, 114, 104}},
	{"heath", "[heath]", G::Barren, true, {104, 72, 86}, {118, 86, 100}, {60, 126, 48}, {104, 72, 86}},
	{"mud", "[mud]", G::Rough, true, {82, 64, 48}, {96, 76, 56}, {96, 76, 56}, {82, 64, 48}},
	{"mudflat", "[mudflat]", G::Rough, true, {140, 126, 108}, {156, 142, 123}, {74, 96, 78}, {140, 126, 108}},
	{"deep_snow", "[deep_snow]", G::Ice, true, {208, 216, 226}, {222, 228, 236}, {222, 228, 236}, {208, 216, 226}},
	{"scree", "[scree]", G::Rough, true, {104, 108, 116}, {118, 122, 128}, {118, 122, 128}, {104, 108, 116}},
	{"brick_path", "[brick_path]", G::Paths, true, {120, 90, 82}, {136, 103, 94}, {170, 142, 104}, {120, 90, 82}},
	{"boardwalk", "[boardwalk]", G::Paths, true, {142, 110, 72}, {164, 128, 84}, {164, 128, 84}, {142, 110, 72}},
	{"lava", "[lava]", G::Lava, true, {150, 60, 24}, {190, 80, 30}, {190, 80, 30}, {150, 60, 24}},
	{"ember_field", "[ember_field]", G::Lava, true, {70, 40, 32}, {90, 52, 38}, {90, 52, 38}, {70, 40, 32}},
	{"loam", "[loam]", G::Fertile, true, {62, 52, 36}, {74, 62, 40}, {74, 62, 40}, {62, 52, 36}},
	{"moss", "[moss]", G::Fertile, true, {38, 80, 38}, {46, 96, 42}, {46, 96, 42}, {38, 80, 38}},
	{"spring_meadow", "[spring_meadow]", G::Fertile, true, {80, 136, 52}, {92, 150, 58}, {92, 150, 58}, {80, 136, 52}},
	{"deep_water", "[deep_water]", G::DeepWater, true, {8, 40, 120}, {10, 60, 160}, {10, 60, 160}, {8, 40, 120}},
	{"dark_water", "[dark_water]", G::DeepWater, true, {10, 30, 64}, {14, 38, 78}, {14, 38, 78}, {10, 30, 64}},
	{"void_hole", "[void_hole]", G::Void, true, {8, 8, 12}, {10, 10, 14}, {10, 10, 14}, {8, 8, 12}},
	{"chasm", "[chasm]", G::Void, true, {18, 14, 20}, {24, 18, 26}, {24, 18, 26}, {18, 14, 20}},
	{"lush_grass", "[lush_grass]", G::Grass, true, {25, 101, 49}, {25, 101, 49}, {25, 101, 49}, {25, 101, 49}},
	{"dry_grass", "[dry_grass]", G::Grass, true, {120, 123, 52}, {120, 123, 52}, {120, 123, 52}, {120, 123, 52}},
	{"dune_sand", "[dune_sand]", G::Sand, true, {204, 151, 85}, {204, 151, 85}, {204, 151, 85}, {204, 151, 85}},
	{"pale_sand", "[pale_sand]", G::Sand, true, {211, 202, 168}, {211, 202, 168}, {211, 202, 168}, {211, 202, 168}},
	{"sandstone", "[sandstone]", G::Paths, true, {136, 126, 98}, {150, 140, 110}, {150, 140, 110}, {136, 126, 98}},
	{"pumice", "[pumice]", G::Rough, true, {44, 42, 42}, {58, 55, 54}, {58, 55, 54}, {44, 42, 42}},
	{"frozen_lake", "[frozen_lake]", G::Ice, true, {128, 160, 174}, {141, 172, 185}, {141, 172, 185}, {128, 160, 174}},
	{"wind_swept_snow", "[wind_swept_snow]", G::Ice, true, {202, 206, 226}, {202, 206, 226}, {202, 206, 226}, {202, 206, 226}},
	{"weathered_sandstone", "[weathered_sandstone]", G::Barren, true, {150, 124, 76}, {166, 138, 86}, {166, 138, 86}, {150, 124, 76}},
	{"windy_sand_dune", "[windy_sand_dune]", G::Sand, true, {212, 182, 134}, {214, 184, 136}, {214, 184, 136}, {212, 182, 134}},
	{"hardpan", "[hardpan]", G::Rough, true, {196, 188, 176}, {212, 204, 192}, {212, 204, 192}, {196, 188, 176}},
	{"tundra", "[tundra]", G::Barren, true, {82, 78, 62}, {91, 87, 69}, {91, 87, 69}, {82, 78, 62}},
	{"volcanic_ash", "[volcanic_ash]", G::Barren, true, {58, 56, 56}, {72, 70, 69}, {72, 70, 69}, {58, 56, 56}},
	{"glacier_ice", "[glacier_ice]", G::Ice, true, {93, 158, 207}, {96, 160, 210}, {96, 160, 210}, {93, 158, 207}},
	{"overgrown_road", "[overgrown_road]", G::Paths, true, {122, 118, 98}, {134, 128, 106}, {134, 128, 106}, {122, 118, 98}},
	{"laterite", "[laterite]", G::Barren, true, {150, 76, 46}, {166, 90, 54}, {166, 90, 54}, {150, 76, 46}},
	{"cliffs", "[cliffs]", G::Obstacles, true, {118, 92, 70}, {136, 108, 82}, {136, 108, 82}, {118, 92, 70}},
	{"molten_lava_pool", "[molten_lava_pool]", G::Lava, true, {214, 74, 14}, {183, 63, 12}, {183, 63, 12}, {214, 74, 14}},
	{"slate_ashlar", "[slate_ashlar]", G::Paths, true, {91, 94, 97}, {104, 108, 118}, {104, 108, 118}, {91, 94, 97}},
	{"basalt_causeway", "[basalt_causeway]", G::Paths, true, {63, 61, 60}, {76, 74, 76}, {76, 74, 76}, {63, 61, 60}},
	{"scoria_blocks", "[scoria_blocks]", G::Paths, true, {74, 68, 63}, {88, 76, 68}, {88, 76, 68}, {74, 68, 63}},
	{"rime_ashlar", "[rime_ashlar]", G::Paths, true, {181, 187, 212}, {181, 187, 212}, {181, 187, 212}, {181, 187, 212}},
	{"moonstone_lozenge", "[moonstone_lozenge]", G::Paths, true, {190, 192, 209}, {190, 192, 209}, {190, 192, 209}, {190, 192, 209}},
	{"frost_pinwheel", "[frost_pinwheel]", G::Paths, true, {185, 188, 210}, {185, 188, 210}, {185, 188, 210}, {185, 188, 210}},
	{"arctic_tundra", "[arctic_tundra]", G::Barren, true, {137, 136, 141}, {137, 136, 141}, {137, 136, 141}, {137, 136, 141}},
	{"glacier", "[glacier]", G::Obstacles, true, {196, 212, 230}, {176, 200, 226}, {176, 200, 226}, {196, 212, 230}},
	{"icefall", "[icefall]", G::Obstacles, true, {120, 172, 210}, {148, 196, 226}, {148, 196, 226}, {120, 172, 210}},
	{"sandstone_benches", "[sandstone_benches]", G::Obstacles, true, {196, 150, 104}, {214, 170, 124}, {214, 170, 124}, {196, 150, 104}},
	{"obsidian_ridge", "[obsidian_ridge]", G::Obstacles, true, {46, 46, 56}, {62, 62, 74}, {62, 62, 74}, {46, 46, 56}},
	{"granite_tors", "[granite_tors]", G::Obstacles, true, {120, 124, 104}, {140, 140, 124}, {140, 140, 124}, {120, 124, 104}},
	{"travertine_terraces", "[travertine_terraces]", G::Obstacles, true, {214, 206, 186}, {226, 218, 200}, {226, 218, 200}, {214, 206, 186}},
	{"penitentes", "[penitentes]", G::Ridges, true, {204, 218, 236}, {222, 232, 246}, {222, 232, 246}, {204, 218, 236}},
	{"termite_mounds", "[termite_mounds]", G::Ridges, true, {150, 96, 62}, {170, 112, 72}, {170, 112, 72}, {150, 96, 62}},
	{"hoodoos", "[hoodoos]", G::Ridges, true, {204, 140, 98}, {222, 160, 116}, {222, 160, 116}, {204, 140, 98}},
	{"petrified_stumps", "[petrified_stumps]", G::Ridges, true, {124, 110, 92}, {142, 128, 108}, {142, 128, 108}, {124, 110, 92}},
};
struct ExpectedLegacyName
{
	const char *name;
	TerrainType type;
};
constexpr ExpectedLegacyName EXPECTED_LEGACY[] = {
	{"gravel", SHALE},
	{"flower_meadow", HEATH},
	{"marsh", MUDFLAT},
	{"dirt_track", BRICK_PATH},
};

bool same(TerrainColor a, TerrainColor b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b;
}
} // namespace

TEST_SUITE("TerrainCatalog")
{
	TEST_CASE("built-in rows, presentations and name lookups match the former constexpr table")
	{
		static_assert(std::size(EXPECTED) == 64 && std::size(EXPECTED) <= TERRAIN_COUNT);
		for (unsigned i = 0; i < std::size(EXPECTED); ++i)
		{
			const auto type = TerrainType(i);
			const auto &expected = EXPECTED[i];
			CAPTURE(expected.name);
			const auto &row = terrainTypeDefinition(type);
			CHECK(row.id == type);
			CHECK(std::string(row.name) == expected.name);
			CHECK(std::string(row.label) == expected.label);
			CHECK(row.group == expected.group);
			CHECK(terrainGroup(type) == expected.group);
			CHECK(terrainPaintable(type) == expected.paintable);
			CHECK(same(row.minimap, expected.minimap));
			CHECK(same(row.overview, expected.overview));
			CHECK(same(row.image, expected.image));
			CHECK(same(row.preview, expected.preview));

			const auto &presentation = terrainPresentation(type);
			CHECK(std::string(presentation.name) == expected.name);
			CHECK(std::string(presentation.label) == expected.label);
			CHECK(same(presentation.minimap, expected.minimap));
			CHECK(same(presentation.overview, expected.overview));
			CHECK(same(presentation.image, expected.image));
			CHECK(same(presentation.preview, expected.preview));
			CHECK(presentation.editorSelectable == expected.paintable);

			CHECK(terrainTypeByName(expected.name) == type);
		}
		for (unsigned i = std::size(EXPECTED); i < TERRAIN_COUNT; ++i)
		{
			const auto type = TerrainType(i);
			CAPTURE(i);
			CHECK(terrainTypeDefinition(type).id == type);
			CHECK(terrainTypeDefinition(type).group == terrainGroup(type));
			CHECK(std::string(terrainPresentation(type).name) == terrainTypeDefinition(type).name);
			CHECK(terrainTypeByName(terrainTypeDefinition(type).name) == type);
		}
		for (const auto &legacy : EXPECTED_LEGACY)
		{
			CAPTURE(legacy.name);
			CHECK(terrainTypeByName(legacy.name) == legacy.type);
		}
		CHECK_FALSE(terrainTypeByName("no_such_terrain"));
		CHECK_FALSE(terrainTypeByName(""));
		CHECK_FALSE(terrainTypeByName("Water"));
	}
}
