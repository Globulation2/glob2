// SPDX-License-Identifier: GPL-3.0-or-later
// Terrain stored per vertex: the corner combine rule, the per-map cell rule
// table, vertex edits and the conversion of files written before format 146.
#include "EngineFixtures.h"
#include "BinaryStream.h"
#include "TextStream.h"
#include "StreamBackend.h"
#include "FileFormatVersions.h"
#include "CellRules.h"
#include "LegacyTerrainFrames.h"
#include <memory>

namespace
{
const TerrainProperties &rules(TerrainType type) { return terrainProperties(type); }
TerrainProperties combine(TerrainType a, TerrainType b, TerrainType c, TerrainType d)
{
	return combineCornerRules(rules(a), rules(b), rules(c), rules(d));
}
}

TEST_SUITE("VertexTerrain")
{
TEST_CASE("equal corners keep their terrain's exact rules")
{
	for (unsigned t = 0; t < TERRAIN_COUNT; ++t)
	{
		const auto type = TerrainType(t);
		CHECK(sameTerrainProperties(combine(type, type, type, type), rules(type)));
	}
	// Group members share a profile, so a dirt/clay border is still barren land.
	CHECK(sameTerrainProperties(combine(DIRT, CLAY, DIRT, CLAY), rules(DIRT)));
}

TEST_CASE("mixed classic corners are exactly the retired shore profile")
{
	const TerrainType classic[] = {WATER, SAND, GRASS};
	for (auto a : classic)
		for (auto b : classic)
			for (auto c : classic)
				for (auto d : classic)
				{
					const bool uniform = a == b && b == c && c == d;
					const bool grassWater = (a == GRASS || b == GRASS || c == GRASS || d == GRASS) &&
											(a == WATER || b == WATER || c == WATER || d == WATER);
					if (uniform || grassWater)
						continue;
					CHECK(sameTerrainProperties(combine(a, b, c, d), CLASSIC_SHORE_PROPERTIES));
				}
}

TEST_CASE("grass directly against water is walkable and unbuildable")
{
	const auto p = combine(GRASS, WATER, WATER, WATER);
	CHECK(p.walkable);
	CHECK_FALSE(p.swimmable);
	CHECK_FALSE(p.buildable);
	CHECK_FALSE(p.shoreline);
	// Both corners grow resources, but they share none.
	CHECK_EQ(p.allowedResources, 0);
	CHECK_EQ(p.farmMaterial, 255);
}

TEST_CASE("every built-in pair combines into valid transition rules")
{
	for (unsigned a = 0; a < TERRAIN_COUNT; ++a)
		for (unsigned b = 0; b < TERRAIN_COUNT; ++b)
		{
			const auto p = combine(TerrainType(a), TerrainType(a), TerrainType(b), TerrainType(b));
			CAPTURE(a);
			CAPTURE(b);
			CHECK(validTerrainProperties(p));
			if (sameTerrainProperties(rules(TerrainType(a)), rules(TerrainType(b))))
				continue;
			CHECK_FALSE(p.swimmable);
			CHECK_FALSE(p.buildable);
			CHECK_EQ(p.walkable, rules(TerrainType(a)).walkable || rules(TerrainType(b)).walkable);
			CHECK_EQ(p.flyable, rules(TerrainType(a)).flyable && rules(TerrainType(b)).flyable);
			CHECK_EQ(p.projectileBlocks, rules(TerrainType(a)).projectileBlocks || rules(TerrainType(b)).projectileBlocks);
		}
}

TEST_CASE("ground costs follow the walkable corners and hazards take the worst")
{
	// One grass corner opens three chasm corners to walking at grass speed.
	const auto chasm = combine(CHASM, CHASM, CHASM, GRASS);
	CHECK(chasm.walkable);
	CHECK_FALSE(chasm.flyable);
	CHECK(chasm.projectileBlocks);
	CHECK_EQ(chasm.groundSpeedQ8, 256);
	// Slow mud slows the transition; ice damages it.
	CHECK_EQ(combine(MUD, GRASS, GRASS, GRASS).groundSpeedQ8, rules(MUD).groundSpeedQ8);
	CHECK_EQ(combine(ICE, GRASS, GRASS, GRASS).groundHealthQ8, rules(ICE).groundHealthQ8);
	CHECK_EQ(combine(ICE, GRASS, GRASS, GRASS).groundSpeedQ8, rules(ICE).groundSpeedQ8);
	// Fast trail never speeds up its border.
	CHECK_EQ(combine(TRAIL, TRAIL, GRASS, GRASS).groundSpeedQ8, 256);
	// Deep water does not walk, so its swim speed does not slow a sand border.
	CHECK_EQ(combine(DEEP_WATER, SAND, SAND, SAND).groundSpeedQ8, 256);
	CHECK_EQ(combine(LAVA, GRASS, GRASS, GRASS).airHealthQ8, rules(LAVA).airHealthQ8);
}

TEST_CASE("the rule table numbers uniform rules by terrain and interns mixed ones in order")
{
	glob2test::HeadlessGlobals globals;
	CellRuleTable table(TerrainRegistry::builtins(), ResourceRegistry::builtins());
	REQUIRE(table.size() == TERRAIN_COUNT);
	for (unsigned t = 0; t < TERRAIN_COUNT; ++t)
	{
		CHECK(table[std::uint16_t(t)].uniform());
		CHECK(table.intern(TerrainType(t), TerrainType(t), TerrainType(t), TerrainType(t)) == t);
	}
	const auto shore = table.intern(GRASS, SAND, SAND, GRASS);
	CHECK(shore == TERRAIN_COUNT);
	// The corner order does not matter, only the multiset.
	CHECK(table.intern(SAND, GRASS, GRASS, SAND) == shore);
	CHECK(table.intern(GRASS, GRASS, SAND, SAND) == shore);
	CHECK(table.intern(GRASS, GRASS, GRASS, SAND) == TERRAIN_COUNT + 1);
	CHECK_FALSE(table[shore].uniform());
	CHECK(sameTerrainProperties(table[shore].properties, CLASSIC_SHORE_PROPERTIES));
	// Costs come from the combined rules.
	const auto mud = table.intern(MUD, MUD, GRASS, GRASS);
	CHECK(table[mud].ground[0].cardinal == gradient_kernel::terrainEntrySteps(combine(MUD, MUD, GRASS, GRASS), 0).cardinal);
	CHECK(table.movement(0).entries.size() == table.size());
	CHECK(table.movement(0).profileIds.size() == table.size());
}

TEST_CASE("mixed cells take the shore habitat and intersect explicit resource lists")
{
	glob2test::HeadlessGlobals globals;
	const auto resources = ResourceRegistry::builtins();
	CellRuleTable table(TerrainRegistry::builtins(), resources);
	const auto shore = table.intern(GRASS, SAND, SAND, SAND);
	for (unsigned r = 0; r < resources->size(); ++r)
	{
		const auto &p = resources->properties(static_cast<ResourceId>(r));
		const bool shoreHabitat = (p.habitatMask & (ResourceShore | ResourceDesert)) && !p.requiresGrowthTerrain &&
								  !p.requiresPermanentDepositsTerrain;
		CAPTURE(resources->key(static_cast<ResourceId>(r)));
		CHECK(table.supportsResource(shore, r) == shoreHabitat);
	}
	CHECK(table.farmResource(shore) == NO_RES_TYPE);
	CHECK(table.farmResource(GRASS) != NO_RES_TYPE);
}

TEST_CASE("a vertex write re-derives the four cells around it")
{
	glob2test::HeadlessGlobals globals;
	Map map;
	map.setSize(4, 4, GRASS);
	const auto generation = map.terrainGeneration();
	CHECK(map.terrainCounts[GRASS] == 256);
	map.setVertexTerrain(5, 6, WATER);
	CHECK(map.terrainGeneration() > generation);
	CHECK(map.vertexTerrainAt(5, 6) == WATER);
	CHECK(map.terrainCounts[GRASS] == 255);
	CHECK(map.terrainCounts[WATER] == 1);
	// Vertex (5,6) is a corner of cells (4,5), (5,5), (4,6) and (5,6).
	for (const auto [x, y] : {std::pair{4, 5}, {5, 5}, {4, 6}, {5, 6}})
	{
		CAPTURE(x);
		CAPTURE(y);
		CHECK(map.terrainTypeAt(x, y) == MIXED_TERRAIN);
		CHECK(map.terrainPropertiesAt(x, y).walkable);
		CHECK_FALSE(map.terrainPropertiesAt(x, y).buildable);
	}
	CHECK(map.terrainTypeAt(6, 6) == GRASS);
	CHECK(map.terrainTypeAt(3, 6) == GRASS);
	CHECK(map.cellCorners(4, 5) == std::array{GRASS, GRASS, GRASS, WATER});
	// The torus wraps vertices too.
	map.setVertexTerrain(0, 0, ICE);
	CHECK(map.cellCorners(15, 15) == std::array{GRASS, GRASS, GRASS, ICE});
	CHECK(map.requiredTerrainExperiments().has(ExperimentId::IceTerrain));
	map.setVertexTerrain(0, 0, GRASS);
	CHECK_FALSE(map.requiredTerrainExperiments().has(ExperimentId::IceTerrain));
	map.fillTerrain(SAND);
	CHECK(map.terrainCounts[SAND] == 256);
	CHECK(map.terrainTypeAt(5, 5) == SAND);
}

TEST_CASE("a cell's four corners paint it uniform and its neighbours mixed")
{
	glob2test::HeadlessGlobals globals;
	Map map;
	map.setSize(4, 4, GRASS);
	map.paintCell(6, 6, BOULDERS);
	CHECK(map.terrainTypeAt(6, 6) == BOULDERS);
	CHECK_FALSE(map.terrainPropertiesAt(6, 6).walkable);
	CHECK(map.terrainPropertiesAt(6, 6).projectileBlocks);
	for (int y = 5; y <= 7; ++y)
		for (int x = 5; x <= 7; ++x)
			if (x != 6 || y != 6)
			{
				CHECK(map.terrainTypeAt(x, y) == MIXED_TERRAIN);
				CHECK(map.terrainPropertiesAt(x, y).walkable);
				CHECK(map.terrainPropertiesAt(x, y).projectileBlocks);
			}
}

TEST_CASE("snapshots keep the rule table they were taken with")
{
	glob2test::HeadlessGlobals globals;
	Map map;
	map.setSize(4, 4, GRASS);
	const auto rules = map.frozenCellRules();
	const auto size = rules->size();
	map.setVertexTerrain(3, 3, LAVA);
	CHECK(rules->size() == size);
	CHECK(map.cellRuleTableRef().size() > size);
	CHECK(&map.cellRuleTableRef() != rules.get());
	CHECK(map.cellRuleAt(map.coordToIndex(3, 3)) >= size);
}

TEST_CASE("old cell terrain moves onto the vertices it covers")
{
	glob2test::HeadlessGlobals globals;
	Map map;
	map.setSize(4, 4, GRASS);
	std::vector<TerrainType> cells(map.cellCount(), MIXED_TERRAIN);
	cells[map.coordToIndex(3, 3)] = ICE;
	cells[map.coordToIndex(4, 3)] = TRAIL;
	cells[map.coordToIndex(10, 10)] = MUD;
	map.convertLegacyCellTerrain(cells);
	map.rebuildTerrainCounts();
	// An isolated whole-cell terrain stays uniform and spreads half a cell.
	CHECK(map.terrainTypeAt(10, 10) == MUD);
	CHECK(map.vertexTerrainAt(11, 11) == MUD);
	CHECK(map.terrainTypeAt(11, 10) == MIXED_TERRAIN);
	// A vertex prefers the cell it is the top-left corner of: (4,3) and (4,4)
	// take trail, so the ice cell becomes an ice/trail transition.
	CHECK(map.vertexTerrainAt(3, 3) == ICE);
	CHECK(map.vertexTerrainAt(4, 3) == TRAIL);
	CHECK(map.vertexTerrainAt(4, 4) == ICE);
	CHECK(map.vertexTerrainAt(5, 4) == TRAIL);
	CHECK(map.cellCorners(3, 3) == std::array{ICE, TRAIL, ICE, ICE});
	CHECK(map.cellCorners(4, 3) == std::array{TRAIL, TRAIL, ICE, TRAIL});
}

TEST_CASE("terrain IDs of older files skip the retired shores")
{
	using R = TerrainRegistry;
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_TERRAIN_CATALOGUE - 1) == TERRAIN_COUNT_BEFORE_CATALOGUE);
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_VERTEX_TERRAIN - 1) == TERRAIN_COUNT_BEFORE_VERTEX);
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_VERTEX_TERRAIN) == TERRAIN_COUNT);
	for (unsigned builtins : {TERRAIN_COUNT_BEFORE_CATALOGUE, TERRAIN_COUNT_BEFORE_VERTEX})
	{
		CHECK(R::currentTerrainId(builtins, WATER) == WATER);
		CHECK(R::currentTerrainId(builtins, TRAIL) == TRAIL);
		CHECK_FALSE(R::currentTerrainId(builtins, 5));
		CHECK_FALSE(R::currentTerrainId(builtins, 6));
		CHECK(R::currentTerrainId(builtins, builtins) == TERRAIN_COUNT);
	}
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_VERTEX, 7) == BOULDERS);
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_VERTEX, 30) == CHASM);
	CHECK(R::currentTerrainId(TERRAIN_COUNT, 5) == BOULDERS);
	CHECK(legacyFrameCorners(16) == std::array{SAND, SAND, SAND, GRASS});
	CHECK(legacyFrameCorners(3) == std::array{GRASS, GRASS, GRASS, GRASS});
	CHECK_FALSE(legacyFrameCorners(272));
}

TEST_CASE("vertex terrain round-trips through packed and text saves")
{
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
	auto &map = world.game.map;
	map.setVertexTerrain(4, 4, WATER);
	map.setVertexTerrain(5, 4, SAND);
	map.setVertexTerrain(8, 8, LAVA);
	map.setVertexTerrain(9, 8, CHASM);
	map.setVertexTerrain(12, 3, DEEP_WATER);
	const std::vector<TerrainType> vertices(map.vertexTerrainState().begin(), map.vertexTerrainState().end());
	for (auto text : {false, true})
	{
		CAPTURE(text);
		auto *bytes = new GAGCore::MemoryStreamBackend;
		std::unique_ptr<GAGCore::OutputStream> output(text
			? static_cast<GAGCore::OutputStream *>(new GAGCore::TextOutputStream(bytes))
			: static_cast<GAGCore::OutputStream *>(new GAGCore::BinaryOutputStream(bytes)));
		world.game.save(output.get(), false, "vertices");
		output->flush();
		auto *storage = new GAGCore::MemoryStreamBackend(std::string(bytes->getBuffer(), bytes->getPosition()));
		std::unique_ptr<GAGCore::InputStream> input(text
			? static_cast<GAGCore::InputStream *>(new GAGCore::TextInputStream(storage))
			: static_cast<GAGCore::InputStream *>(new GAGCore::BinaryInputStream(storage)));
		GameGUI loaded;
		REQUIRE(loaded.game.load(input.get()));
		const auto reloaded = loaded.game.map.vertexTerrainState();
		CHECK(std::equal(reloaded.begin(), reloaded.end(), vertices.begin(), vertices.end()));
		for (size_t i = 0; i < map.cellCount(); ++i)
			if (!sameTerrainProperties(loaded.game.map.terrainPropertiesAt(i), map.terrainPropertiesAt(i)))
			{
				FAIL_CHECK("cell rules differ at " << i);
				break;
			}
		CHECK(loaded.game.map.checkSum(true) == map.checkSum(true));
		CHECK(loaded.game.map.requiredTerrainExperiments() == map.requiredTerrainExperiments());
	}
}
}
