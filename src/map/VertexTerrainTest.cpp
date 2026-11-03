// SPDX-License-Identifier: GPL-3.0-or-later
// Terrain stored per vertex: the corner combine rule, the per-map cell rule
// table, vertex edits and the conversion of files written before format 146.
#include "EngineFixtures.h"
#include "BinaryStream.h"
#include "TextStream.h"
#include "StreamBackend.h"
#include "FileFormatVersions.h"
#include "Version.h"
#include "CellRules.h"
#include "LegacyTerrainFrames.h"
#include <algorithm>
#include <memory>
#include <stdexcept>

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

TEST_CASE("classic transitions vote on movement and blend ecology")
{
	const TerrainType classic[] = {WATER, SAND, GRASS};
	for (auto a : classic)
		for (auto b : classic)
			for (auto c : classic)
				for (auto d : classic)
				{
					const unsigned water = unsigned(a == WATER) + unsigned(b == WATER) + unsigned(c == WATER) + unsigned(d == WATER);
					const unsigned sand = unsigned(a == SAND) + unsigned(b == SAND) + unsigned(c == SAND) + unsigned(d == SAND);
					const auto p = combine(a, b, c, d);
					CHECK_EQ(p.walkable, water <= 2);
					CHECK_EQ(p.swimmable, water >= 3);
					CHECK_EQ(p.shoreline, sand > 0);
					CHECK_EQ(p.buildable, a == GRASS && b == GRASS && c == GRASS && d == GRASS);
					CHECK_EQ(p.fertilityQ8, water * 64);
					CHECK_EQ(p.inhibitionQ8, sand * 64);
					CHECK_EQ(p.shoreSupportQ8, p.inhibitionQ8);
				}
}

TEST_CASE("water-majority grass transitions are swimmable and unbuildable")
{
	const auto p = combine(GRASS, WATER, WATER, WATER);
	CHECK_FALSE(p.walkable);
	CHECK(p.swimmable);
	CHECK_FALSE(p.buildable);
	CHECK_FALSE(p.shoreline);
	// Both corners grow resources, but they share none.
	CHECK_EQ(p.allowedResources, 0);
	CHECK_EQ(p.farmMaterial, 255);
}

TEST_CASE("corner movement votes cover every arrangement and tie")
{
	// Movement is a categorical vote, independent of numeric averages.
	TerrainProperties walking, swimming, blocked;
	walking.walkable = true;
	swimming.swimmable = true;
	const TerrainProperties categories[] = {walking, swimming, blocked};
	for (unsigned pattern = 0; pattern < 81; ++pattern)
	{
		CAPTURE(pattern);
		unsigned remaining = pattern;
		std::array<unsigned, 3> counts{};
		std::array<unsigned, 4> corners{};
		for (auto& corner : corners)
		{
			corner = remaining % 3;
			remaining /= 3;
			++counts[corner];
		}
		// Rows are walking counts, columns swimming counts; the remainder is
		// blocked. Covers all fifteen count triples, including (1, 2, 1).
		constexpr unsigned winners[5][5] = {
			{2, 2, 2, 1, 1}, {2, 2, 1, 1, 99}, {2, 0, 0, 99, 99},
			{0, 0, 99, 99, 99}, {0, 99, 99, 99, 99}};
		const unsigned winner = winners[counts[0]][counts[1]];
		const auto p = combineCornerRules(categories[corners[0]], categories[corners[1]],
			categories[corners[2]], categories[corners[3]]);
		REQUIRE(winner < 3);
		CHECK_EQ(p.walkable, winner == 0);
		CHECK_EQ(p.swimmable, winner == 1);
		CHECK(validTerrainProperties(p));
	}
}

TEST_CASE("all numeric modifiers use four-corner means in every arrangement")
{
	std::array<TerrainProperties, 4> corners;
	corners[0].walkable = corners[3].walkable = true;
	corners[1].swimmable = true;
	// Different expected values catch cross-wiring between fields. Signed and
	// unsigned fractional sums exercise rounding; every corner contributes.
	constexpr std::uint16_t groundSpeed[] = {64, 129, 511, 1024};
	constexpr std::uint16_t airSpeed[] = {128, 255, 256, 512};
	constexpr std::int16_t groundHealth[] = {-32768, -9, 4, 32767};
	constexpr std::int16_t airHealth[] = {-32768, -16, -7, 32767};
	constexpr std::uint16_t growth[] = {0, 0, 512, 1024};
	constexpr std::int16_t fertility[] = {-1024, -127, 128, 1024};
	constexpr std::uint16_t inhibition[] = {0, 128, 768, 1024};
	constexpr std::uint16_t shore[] = {0, 0, 256, 256};
	for (unsigned i = 0; i < corners.size(); ++i)
	{
		auto& p = corners[i];
		p.groundSpeedQ8 = groundSpeed[i]; p.airSpeedQ8 = airSpeed[i];
		p.groundHealthQ8 = groundHealth[i]; p.airHealthQ8 = airHealth[i];
		p.growthQ8 = growth[i]; p.fertilityQ8 = fertility[i];
		p.inhibitionQ8 = inhibition[i]; p.shoreSupportQ8 = shore[i];
		REQUIRE(validTerrainProperties(p));
	}
	std::array<unsigned, 4> order{0, 1, 2, 3};
	do
	{
		const auto p = combineCornerRules(corners[order[0]], corners[order[1]], corners[order[2]], corners[order[3]]);
		CHECK(p.walkable);
		CHECK_EQ(p.groundSpeedQ8, 432);
		CHECK_EQ(p.airSpeedQ8, 287);
		CHECK_EQ(p.groundHealthQ8, -1);
		CHECK_EQ(p.airHealthQ8, -6);
		CHECK_EQ(p.growthQ8, 384);
		CHECK_EQ(p.fertilityQ8, 0);
		CHECK_EQ(p.inhibitionQ8, 480);
		CHECK_EQ(p.shoreSupportQ8, 128);
		CHECK(validTerrainProperties(p));
	} while (std::next_permutation(order.begin(), order.end()));
}

TEST_CASE("mean health rates neither overflow nor clip authored extremes")
{
	for (const auto rates : {std::array<std::int16_t, 4>{32764, 32765, 32766, 32767},
		std::array<std::int16_t, 4>{-32768, -32767, -32766, -32765}})
	{
		std::array<unsigned, 4> order{0, 1, 2, 3};
		do
		{
			std::array<TerrainProperties, 4> corners;
			for (unsigned i = 0; i < corners.size(); ++i)
				corners[i].groundHealthQ8 = corners[i].airHealthQ8 = rates[order[i]];
			const auto p = combineCornerRules(corners[0], corners[1], corners[2], corners[3]);
			CHECK_EQ(p.groundHealthQ8, rates[0] > 0 ? 32765 : -32766);
			CHECK_EQ(p.airHealthQ8, p.groundHealthQ8);
		} while (std::next_permutation(order.begin(), order.end()));
	}
}

TEST_CASE("different water profiles join without a movement barrier")
{
	const auto mixed = combine(WATER, DEEP_WATER, DARK_WATER, WATER);
	CHECK_FALSE(mixed.walkable);
	CHECK(mixed.swimmable);
	CHECK_FALSE(mixed.buildable);
	CHECK_EQ(mixed.groundSpeedQ8, 224); // Half ordinary, half deep water: 87.5%.
	// All four corners contribute, including the minority ice corner.
	const auto icyPool = combine(DEEP_WATER, DEEP_WATER, WATER, ICE);
	CHECK(icyPool.swimmable);
	CHECK_EQ(icyPool.groundSpeedQ8, rules(DEEP_WATER).groundSpeedQ8);
	CHECK_EQ(icyPool.groundHealthQ8, -2);
}

TEST_CASE("compiled movement queries respect corner votes and vertex edits")
{
	glob2test::HeadlessGlobals globals;
	Map map;
	map.setSize(4, 4, WATER);
	map.setVertexTerrain(5, 5, GRASS);
	CHECK_FALSE(map.isFreeForMovement(5, 5, 0, 0));
	CHECK(map.isFreeForMovement(5, 5, WATER_ONLY_CLASS, 0));
	map.setVertexTerrain(6, 5, GRASS);
	CHECK(map.isFreeForMovement(5, 5, 0, 0));
	CHECK_FALSE(map.isFreeForMovement(5, 5, WATER_ONLY_CLASS, 0));
	map.setVertexTerrain(5, 5, CHASM);
	map.setVertexTerrain(6, 5, CHASM);
	CHECK_FALSE(map.isFreeForMovement(5, 5, 0, 0));
	CHECK_FALSE(map.isFreeForMovement(5, 5, WATER_ONLY_CLASS, 0));
	CHECK_FALSE(map.isFreeForGroundUnit(5, 5, true, 0));
	map.setVertexTerrain(6, 5, DEEP_WATER);
	CHECK(map.isFreeForMovement(5, 5, WATER_ONLY_CLASS, 0));
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
			CHECK_EQ(p.swimmable, rules(TerrainType(a)).swimmable && rules(TerrainType(b)).swimmable);
			CHECK_FALSE(p.buildable);
			const auto& first = rules(TerrainType(a));
			const auto& second = rules(TerrainType(b));
			CHECK_EQ(p.walkable, (first.walkable || second.walkable) &&
				(first.walkable || first.swimmable) && (second.walkable || second.swimmable));
			CHECK_EQ(p.flyable, rules(TerrainType(a)).flyable && rules(TerrainType(b)).flyable);
			CHECK_EQ(p.projectileBlocks, rules(TerrainType(a)).projectileBlocks || rules(TerrainType(b)).projectileBlocks);
		}
}

TEST_CASE("numeric terrain effects blend proportionally at boundaries")
{
	const auto chasm = combine(CHASM, CHASM, CHASM, GRASS);
	CHECK_FALSE(chasm.walkable);
	CHECK_FALSE(chasm.swimmable);
	CHECK_FALSE(chasm.flyable);
	CHECK(chasm.projectileBlocks);
	// A lone corner contributes one quarter of its speed or health adjustment.
	CHECK_EQ(combine(MUD, GRASS, GRASS, GRASS).groundSpeedQ8, 232);
	CHECK_EQ(combine(ICE, GRASS, GRASS, GRASS).groundHealthQ8, -2);
	CHECK_EQ(combine(ICE, GRASS, GRASS, GRASS).groundSpeedQ8, 224);
	CHECK_EQ(combine(TRAIL, GRASS, GRASS, GRASS).groundSpeedQ8, 320);
	CHECK_EQ(combine(LAVA, GRASS, GRASS, GRASS).airHealthQ8, -16);
	// Even splits create one intermediate cell between speeds and hazards.
	CHECK_EQ(combine(ICE, ICE, GRASS, GRASS).groundSpeedQ8, 192);
	CHECK_EQ(combine(ICE, ICE, GRASS, GRASS).groundHealthQ8, -4);
	CHECK_EQ(combine(TRAIL, TRAIL, GRASS, GRASS).groundSpeedQ8, 384);
	CHECK_EQ(combine(TRAIL, TRAIL, TRAIL, GRASS).groundSpeedQ8, 448);
	CHECK_EQ(combine(ICE, ICE, ICE, GRASS).groundSpeedQ8, 160);
	CHECK_EQ(combine(ICE, ICE, ICE, GRASS).groundHealthQ8, -6);
	CHECK_EQ(combine(LAVA, LAVA, GRASS, GRASS).airHealthQ8, -32);
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
	CHECK(table[shore].properties.walkable);
	CHECK(table[shore].properties.shoreline);
	CHECK_FALSE(table[shore].properties.buildable);
	CHECK_EQ(table[shore].properties.inhibitionQ8, 128);
	CHECK_EQ(table[shore].properties.shoreSupportQ8, 128);
	// Costs come from the combined rules.
	const auto mud = table.intern(MUD, MUD, GRASS, GRASS);
	CHECK(table[mud].ground[0].cardinal == gradient_kernel::terrainEntrySteps(combine(MUD, MUD, GRASS, GRASS), 0).cardinal);
	CHECK(table.movement(0).entries.size() == table.size());
	CHECK(table.movement(0).profileIds.size() == table.size());
}

TEST_CASE("compiled CPU seed columns preserve frozen rules when mixed rules grow")
{
	glob2test::HeadlessGlobals globals;
	CellRuleTable table(TerrainRegistry::builtins(), ResourceRegistry::builtins());
	const auto frozen = table;
	const auto mixed = table.intern(WATER, GRASS, SAND, WATER);
	REQUIRE(table.size() == frozen.size() + 1);
	for (unsigned swim = 0; swim < SWIM_CLASS_COUNT; ++swim)
	{
		const auto variant = swimAccessVariant(swim);
		const auto &live = table.openTerrainSeeds(variant);
		const auto &previous = frozen.openTerrainSeeds(variant);
		REQUIRE(live.size() == table.size());
		REQUIRE(previous.size() == frozen.size());
		CHECK(std::equal(previous.begin(), previous.end(), live.begin()));
		for (unsigned rule = 0; rule < table.size(); ++rule)
			CHECK(live[rule] == (gradient_kernel::terrainAllowsGround(table.properties(rule), swim)
				? GRADIENT_UNREACHABLE : GRADIENT_FORBIDDEN));
		CHECK(live[mixed] == (gradient_kernel::terrainAllowsGround(table.properties(mixed), swim)
			? GRADIENT_UNREACHABLE : GRADIENT_FORBIDDEN));
	}
	CHECK_THROWS_AS(table.openTerrainSeeds(CellRuleTable::OpenTerrainClasses.size()), std::out_of_range);
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
	map.setVertexTerrain(0, 0, GRASS);
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
				// Side neighbours have two blocked corners; diagonals have one.
				CHECK_EQ(map.terrainPropertiesAt(x, y).walkable, x != 6 && y != 6);
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

TEST_CASE("terrain IDs of older files skip the retired shores and follow the current built-ins")
{
	using R = TerrainRegistry;
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_TERRAIN_CATALOGUE - 1) == TERRAIN_COUNT_BEFORE_CATALOGUE);
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_VERTEX_TERRAIN - 1) == TERRAIN_COUNT_BEFORE_VERTEX);
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_VERTEX_TERRAIN) == TERRAIN_COUNT_BEFORE_GROUND_LOOKS);
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_GROUND_LOOKS - 1) == TERRAIN_COUNT_BEFORE_GROUND_LOOKS);
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_GROUND_LOOKS) == TERRAIN_COUNT_BEFORE_SANDSTONE_PUMICE);
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_SANDSTONE_PUMICE - 1) == TERRAIN_COUNT_BEFORE_SANDSTONE_PUMICE);
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_SANDSTONE_PUMICE) == TERRAIN_COUNT);
	CHECK(R::savedBuiltinCount(VERSION_MINOR) == TERRAIN_COUNT);
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
	// Formats 146 to 190 keep every built-in and move custom IDs behind the looks.
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_GROUND_LOOKS, 5) == BOULDERS);
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_GROUND_LOOKS, CHASM) == CHASM);
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_GROUND_LOOKS, TERRAIN_COUNT_BEFORE_GROUND_LOOKS) == TERRAIN_COUNT);
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_GROUND_LOOKS, 40) == 40 - TERRAIN_COUNT_BEFORE_GROUND_LOOKS + TERRAIN_COUNT);
	// Formats 191 to 194 keep every built-in through PALE_SAND and move custom IDs behind
	// sandstone, pumice and frozen lake.
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_SANDSTONE_PUMICE, CHASM) == CHASM);
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_SANDSTONE_PUMICE, PALE_SAND) == PALE_SAND);
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_SANDSTONE_PUMICE, TERRAIN_COUNT_BEFORE_SANDSTONE_PUMICE) == TERRAIN_COUNT);
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_SANDSTONE_PUMICE, 40) == 40 - TERRAIN_COUNT_BEFORE_SANDSTONE_PUMICE + TERRAIN_COUNT);
	CHECK(R::currentTerrainId(TERRAIN_COUNT, SANDSTONE) == SANDSTONE);
	CHECK(R::currentTerrainId(TERRAIN_COUNT, PUMICE) == PUMICE);
	CHECK(R::currentTerrainId(TERRAIN_COUNT, FROZEN_LAKE) == FROZEN_LAKE);
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
	}
}
}
