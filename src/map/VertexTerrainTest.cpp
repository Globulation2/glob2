// SPDX-License-Identifier: GPL-3.0-or-later
// Terrain stored per vertex: the corner combine rule, the per-map cell rule
// table, vertex edits and the conversion of files written before format 146.
#include "EngineFixtures.h"
#include "BinaryStream.h"
#include "TextStream.h"
#include "StreamBackend.h"
#include "FileFormatVersions.h"
#include "CellRules.h"
#include "UnitTraining.h"
#include "field/RuntimeTerrainGradient.h"
#include "BuildingGradientSearch.h"
#include "MovementConnections.h"
#include "field/TerrainTravel.h"
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
	// Rotations/reflections share a rule; adjacency remains part of its identity.
	CHECK(table.intern(SAND, GRASS, GRASS, SAND) == shore);
	CHECK(table.intern(GRASS, GRASS, SAND, SAND) != shore);
	CHECK(table.intern(GRASS, GRASS, GRASS, SAND) == TERRAIN_COUNT + 2);
	CHECK_FALSE(table[shore].uniform());
	CHECK(sameTerrainProperties(table[shore].properties, CLASSIC_SHORE_PROPERTIES));
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
			CHECK(live[rule] ==
				  (table[rule].allowsMovement(swim) ? GRADIENT_UNREACHABLE : GRADIENT_FORBIDDEN));
		CHECK(live[mixed] ==
			  (table[mixed].allowsMovement(swim) ? GRADIENT_UNREACHABLE : GRADIENT_FORBIDDEN));
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

TEST_CASE("terrain IDs of older files skip the retired shores and follow the current built-ins")
{
	using R = TerrainRegistry;
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_TERRAIN_CATALOGUE - 1) == TERRAIN_COUNT_BEFORE_CATALOGUE);
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_VERTEX_TERRAIN - 1) == TERRAIN_COUNT_BEFORE_VERTEX);
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_VERTEX_TERRAIN) == TERRAIN_COUNT_BEFORE_GROUND_LOOKS);
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_GROUND_LOOKS - 1) == TERRAIN_COUNT_BEFORE_GROUND_LOOKS);
	CHECK(R::savedBuiltinCount(FILE_FORMAT_VERSION_GROUND_LOOKS) == TERRAIN_COUNT);
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
	// Formats 146 to 186 keep every built-in and move custom IDs behind the looks.
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_GROUND_LOOKS, 5) == BOULDERS);
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_GROUND_LOOKS, CHASM) == CHASM);
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_GROUND_LOOKS, TERRAIN_COUNT_BEFORE_GROUND_LOOKS) == TERRAIN_COUNT);
	CHECK(R::currentTerrainId(TERRAIN_COUNT_BEFORE_GROUND_LOOKS, 40) == 40 - TERRAIN_COUNT_BEFORE_GROUND_LOOKS + TERRAIN_COUNT);
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
TEST_CASE("conservative movement is separate from mixed ecology rules")
{
	glob2test::HeadlessGlobals globals;
	CellRuleTable table(TerrainRegistry::builtins(), ResourceRegistry::builtins());
	const auto rock = table.intern(OUTCROP, OUTCROP, OUTCROP, GRASS);
	CHECK(table[rock].properties.walkable); // unchanged ecology profile
	for (int movement : {0, 1, WATER_ONLY_CLASS})
		CHECK_FALSE(table[rock].allowsMovement(movement));
	CHECK(table[rock].allowsMovement(AIR_MOVEMENT_CLASS));
	const auto beach = table.intern(SAND, WATER, WATER, DEEP_WATER);
	CHECK_FALSE(table[beach].allowsMovement(0));
	CHECK_FALSE(table[beach].allowsMovement(WATER_ONLY_CLASS));
	CHECK(table[beach].allowsMovement(1));
	CHECK(table[beach].usesSwimming);
	CHECK(table[beach].movementSpeedQ8 == terrainProperties(DEEP_WATER).groundSpeedQ8);
	CHECK(table[beach].ground[1].cardinal ==
		  gradient_kernel::scaledTerrainStep(gradient_kernel::WATER_STEP[1],
											 terrainProperties(DEEP_WATER).groundSpeedQ8));
	const auto water = table.intern(WATER, DEEP_WATER, DARK_WATER, WATER);
	CHECK(table[water].allowsMovement(WATER_ONLY_CLASS));
	const auto land = table.intern(GRASS, SAND, TRAIL, GRASS);
	CHECK(table[land].allowsMovement(0));
	CHECK_FALSE(table[land].usesSwimming);
}

TEST_CASE("diagonal passage respects hard sides and ignores transient side traffic")
{
	glob2test::HeadlessGlobals globals;
	Map map;
	map.setSize(4, 4, GRASS);
	CHECK(map.canTraverse(4, 4, 1, 1, 0, 1));
	map.setGroundUnit(5, 4, 123);
	CHECK(map.canTraverse(4, 4, 1, 1, 0, 1));
	map.setGroundUnit(5, 4, NOGUID);
	map.setBuilding(5, 4, 1, 1, 123);
	CHECK_FALSE(map.canTraverse(4, 4, 1, 1, 0, 1));
	CHECK(map.canTraverse(4, 4, 0, 1, 0, 1));
	map.setBuilding(5, 4, 1, 1, NOGBID);
	map.setResourceByIndex(5, 4, WOOD, 1);
	CHECK_FALSE(map.canTraverse(4, 4, 1, 1, 0, 1));
	map.setNoResource(5, 4, 1);
	map.addForbidden(5, 4, 0);
	CHECK_FALSE(map.canTraverse(4, 4, 1, 1, 0, 1));
	CHECK(map.canTraverse(4, 4, 1, 1, 0, 2));
	map.removeForbidden(5, 4, 0);
	map.setVertexTerrain(6, 4, OUTCROP); // blocks side (5,4), not source/destination
	CHECK(map.terrainAllowsMovementAt(5, 5, 0));
	CHECK_FALSE(map.canTraverse(4, 4, 1, 1, 0, 1));
	CHECK(map.canTraverse(4, 4, 1, 1, AIR_MOVEMENT_CLASS, 1));
	map.setVertexTerrain(6, 4, GRASS);
	CHECK(map.canTraverse(15, 15, 1, 1, 0, 1));
	map.setVertexTerrain(1, 15, OUTCROP);
	CHECK_FALSE(map.canTraverse(15, 15, 1, 1, 0, 1));
}

TEST_CASE("A star and eager and resumed gradients route around a blocked diagonal")
{
	glob2test::HeadlessGlobals globals;
	Map map;
	map.setSize(4, 4, GRASS);
	map.setVertexTerrain(6, 4, OUTCROP);
	int dx = 0, dy = 0;
	REQUIRE(map.pathfindPointToPoint(4, 4, 5, 5, &dx, &dy, 0, 1, 16));
	CHECK_FALSE((dx == 1 && dy == 1));
	CHECK(map.canTraverse(4, 4, dx, dy, 0, 1));
	CHECK((map.terrainConnections(4, 4, 0) & (1u << field::directionIndex(1, 1))) == 0);
	std::vector<Uint16> seeds(map.cellCount(), GRADIENT_UNREACHABLE);
	seeds[map.coordToIndex(5, 5)] = GRADIENT_AT_GOAL;
	auto eager = seeds, lazy = seeds;
	map.propagateGradient(eager.data(), 0);
	BuildingGradientSearch search;
	search.begin(map, lazy.data(), 0);
	search.resolve(map.coordToIndex(4, 4));
	search.finish();
	CHECK(lazy == eager);
	CHECK(eager[map.coordToIndex(4, 4)] == GRADIENT_AT_GOAL - 2 * GRADIENT_STEP);
}

TEST_CASE("resource ascent and finite peaks ignore disconnected higher neighbors")
{
	glob2test::HeadlessGlobals globals;
	Map map;
	map.setSize(4, 4, GRASS);
	map.setVertexTerrain(6, 4, OUTCROP);
	std::vector<Uint16> values(map.cellCount(), 0);
	values[map.coordToIndex(4, 4)] = GRADIENT_AT_GOAL - 20;
	values[map.coordToIndex(3, 4)] = GRADIENT_AT_GOAL - 10;
	values[map.coordToIndex(5, 5)] = GRADIENT_AT_GOAL;
	Sint32 x = -1, y = -1;
	CHECK_FALSE(MapState::gradientDestination(map.cellView(), values.data(), 4, 4, &x, &y, 0, 1));
	CHECK(x == 3);
	CHECK(y == 4);
	CHECK(MapState::gradientPeak(map.cellView(), values.data(), 3, 4, 0, 1));
	values[map.coordToIndex(3, 4)] = 0;
	CHECK(MapState::gradientPeak(map.cellView(), values.data(), 4, 4, 0, 1));
	CHECK_FALSE(MapState::gradientDestination(map.cellView(), values.data(), 4, 4, &x, &y, 0, 1));
	CHECK(x == 4);
	CHECK(y == 4);
}

TEST_CASE("forward terrain influence respects the forward edge direction")
{
	glob2test::HeadlessGlobals globals;
	constexpr int width = 8, height = 8;
	auto edges = std::make_shared<std::vector<std::uint8_t>>(width * height, 0);
	(*edges)[18] = 1u << field::directionIndex(1, 0); // A -> B only
	const auto grid = field::Grid(width, height).withConnections(edges);
	std::vector<Uint16> terrain(width * height, GRASS);
	for (int source : {18, 19})
	{
		std::vector<Uint8> values(width * height, 1);
		values[source] = 10;
		field::expandTerrainInfluence(
			values.data(), width, height, [&](size_t i) { return TerrainType(terrain[i]); },
			*TerrainRegistry::builtins(), grid);
		CHECK(values[18] == (source == 18 ? 10 : 1));
		CHECK(values[19] == (source == 19 ? 10 : 9));
	}
}

TEST_CASE("reverse propagation honors forward directed connections")
{
	glob2test::HeadlessGlobals globals;
	constexpr int width = 8, height = 8;
	auto edges = std::make_shared<std::vector<std::uint8_t>>(width * height, 0);
	// Only A(2,2)->B(3,2) exists; reverse propagation must reach A from B.
	(*edges)[2 + 2 * width] = 1u << 4; // E in field::Surrounding
	const auto grid = field::Grid(width, height).withConnections(edges);
	for (const auto backend : {gradient_kernel::Backend::Scalar, gradient_kernel::cpuBackend()})
	{
		gradient_kernel::CPUSelectionScope selection(backend);
		for (const int goal : {2 + 2 * width, 3 + 2 * width})
		{
			std::vector<Uint16> cells(width * height, GRADIENT_UNREACHABLE);
			cells[goal] = GRADIENT_AT_GOAL;
			GradientWorkspace workspace;
			gradient_kernel::propagateFieldCPU(cells.data(), 0, gradient_kernel::COST_LIMIT, grid,
											   workspace, [](size_t) { return false; });
			CHECK(cells[2 + 2 * width] ==
				  (goal == 2 + 2 * width ? GRADIENT_AT_GOAL : GRADIENT_AT_GOAL - GRADIENT_STEP));
			CHECK(cells[3 + 2 * width] ==
				  (goal == 3 + 2 * width ? GRADIENT_AT_GOAL : GRADIENT_UNREACHABLE));
		}
	}
}
}

TEST_SUITE("VertexTerrain")
{
	TEST_CASE("shore edges walk while isolated tips and opposite land corners do not")
	{
		glob2test::HeadlessGlobals globals;
		CellRuleTable table(TerrainRegistry::builtins(), ResourceRegistry::builtins());
		for (unsigned mask = 0; mask < 16; ++mask)
		{
			const auto t = [&](int bit) { return mask & (1u << bit) ? SAND : WATER; };
			const auto id = table.intern(t(0), t(1), t(2), t(3));
			const bool edge =
				(mask & 3) == 3 || (mask & 12) == 12 || (mask & 5) == 5 || (mask & 10) == 10;
			CHECK(table[id].allowsMovement(0) == edge);
			CHECK(table[id].allowsMovement(1));
			CHECK(table[id].allowsMovement(WATER_ONLY_CLASS) == (mask == 0));
			CHECK(table[id].usesSwimming == !edge);
			CHECK(sameTerrainProperties(table[id].properties, combine(t(0), t(1), t(2), t(3))));
		}
	}

	TEST_CASE("shore connections reject water corners and water edges while preserving beaches")
	{
		glob2test::HeadlessGlobals globals;
		Map map;
		map.setSize(4, 4, GRASS);
		map.setVertexTerrain(5, 5, WATER);
		for (const auto p : {std::pair{4, 4}, std::pair{5, 4}, std::pair{4, 5}, std::pair{5, 5}})
			CHECK(map.terrainAllowsMovementAt(p.first, p.second, 0));
		CHECK_FALSE(map.canTraverse(4, 4, 1, 1, 0, 1)); // shared water corner
		CHECK(map.canTraverse(4, 4, 1, 0, 0, 1));
		CHECK(map.canTraverse(4, 4, 1, 1, 1, 1)); // amphibious travel can swim across
		map.setVertexTerrain(5, 5, GRASS);
		map.setVertexTerrain(6, 4, WATER);
		CHECK(map.canTraverse(4, 4, 1, 1, 0, 1)); // shared dry corner and clear sides
		{
			auto edit = map.editTerrain();
			for (int y = 0; y < 16; ++y)
				for (int x = 0; x < 16; ++x)
					map.setVertexTerrain(x, y, x == 0 ? WATER : GRASS);
		}
		CHECK(map.terrainAllowsMovementAt(15, 5, 0));
		CHECK(map.terrainAllowsMovementAt(0, 5, 0));
		CHECK_FALSE(map.canTraverse(15, 5, 1, 0, 0, 1)); // all-water shared edge at seam
		CHECK(map.canTraverse(15, 5, 0, 1, 0, 1));       // ordinary shoreline
	}

	TEST_CASE("walkers gather aquatic shore resources without entering them or bridging water")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world(
			{.wDec = 4, .hDec = 4, .discovered = true, .loadDefaultRace = true, .header = true});
		auto &map = world.game.map;
		{
			auto edit = map.editTerrain();
			for (int y = 0; y < 16; ++y)
				for (int x = 0; x < 16; ++x)
					map.setVertexTerrain(x, y,
										 x == 0 || x == 1 || x == 5 || x == 6 ? WATER : GRASS);
		}
		map.setResourceByIndex(5, 5, ALGA, 5);
		auto *worker = world.addUnit(WORKER, 4, 5);
		REQUIRE(worker);
		CHECK(map.doesUnitTouchMaterialSource(worker, MaterialId::Algae).has_value());
		std::vector<Uint16> values(map.cellCount(), 0);
		map.seedMaterialGradient(0, ALGA, 0, values.data());
		map.propagateGradient(values.data(), 0, gradient_kernel::COST_LIMIT,
							  gradient_kernel::Family::Materials, 1);
		CHECK(values[map.coordToIndex(4, 5)] > GRADIENT_UNREACHABLE);
		CHECK(values[map.coordToIndex(6, 5)] > GRADIENT_UNREACHABLE);
		CHECK_FALSE(map.canTraverse(4, 5, 1, 0, 0, 1));
		Sint32 tx = -1, ty = -1;
		CHECK(MapState::gradientDestination(map.cellView(), values.data(), 4, 5, &tx, &ty, 0, 1));
		CHECK(tx == 5);
		CHECK(ty == 5);
		int dx = 0, dy = 0;
		CHECK(map.pathfindPointToPoint(3, 9, 5, 5, &dx, &dy, 0, 1, 16));
		CHECK(map.canTraverse(3, 9, dx, dy, 0, 1));
		CHECK(map.pathfindPointToPoint(4, 5, 5, 5, &dx, &dy, 0, 1, 16));
		CHECK(dx == 0);
		CHECK(dy == 0);
		CHECK(map.takeHarvest(4, 5, 1, 0, MaterialId::Algae, 1));
		CHECK(worker->posX == 4);
		CHECK(worker->posY == 5);
		for (size_t i = 0; i < values.size(); ++i)
			values[i] = map.cellRule(i).allowsMovement(0) ? GRADIENT_UNREACHABLE : 0;
		values[map.coordToIndex(6, 5)] = GRADIENT_AT_GOAL;
		map.propagateGradient(values.data(), 0);
		CHECK(values[map.coordToIndex(4, 5)] == GRADIENT_UNREACHABLE);
	}

	TEST_CASE("oriented shoreline changes invalidate routes even when canonical rules match")
	{
		glob2test::HeadlessGlobals globals;
		Map map;
		map.setSize(4, 4, GRASS);
		{
			auto edit = map.editTerrain();
			for (int y = 0; y < 16; ++y)
				for (int x = 0; x < 16; ++x)
					map.setVertexTerrain(x, y, x % 2 ? WATER : SAND);
		}
		const auto ids =
			std::vector<Uint16>(map.cellRuleState().begin(), map.cellRuleState().end());
		const auto generation = map.topologyGeneration;
		const auto old = MapState::movementGrid(map.cellView(), 0, 1, false);
		{
			auto edit = map.editTerrain();
			for (int y = 0; y < 16; ++y)
				for (int x = 0; x < 16; ++x)
					map.setVertexTerrain(x, y, y % 2 ? WATER : SAND);
		}
		CHECK(std::equal(ids.begin(), ids.end(), map.cellRuleState().begin()));
		CHECK(map.topologyGeneration > generation);
		const auto now = MapState::movementGrid(map.cellView(), 0, 1, false);
		CHECK(old.incomingMask(map.coordToIndex(0, 0)) != now.incomingMask(map.coordToIndex(0, 0)));
	}

	TEST_CASE("amphibious globs walk shore routes and swim across their water side")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world(
			{.wDec = 4, .hDec = 4, .discovered = true, .loadDefaultRace = true, .header = true});
		world.game.map.setVertexTerrain(5, 5, WATER);
		auto *unit = world.addUnit(WORKER, 5, 5, 0, 1);
		REQUIRE(unit);
		REQUIRE(unit->performance[SWIM] > 0);
		unit->dx = 0;
		unit->dy = 1;
		unit->selectPreferredGroundMovement();
		CHECK(unit->action == WALK);
		unit->dx = unit->dy = 1;
		unit->selectPreferredGroundMovement();
		CHECK(unit->action == SWIM);
		CHECK_FALSE(world.game.map.canTraverse(4, 4, 1, 1, 0, 1,
											   Map::DestinationAccess::InteractionEndpoint));
		CHECK(world.game.map.canTraverse(4, 4, 1, 1, unit->movementClass(), 1,
										 Map::DestinationAccess::InteractionEndpoint));
	}
}

TEST_SUITE("VertexTerrain")
{
	TEST_CASE("existing building exits and training reject water covered shared edges")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world(
			{.wDec = 5, .hDec = 5, .loadDefaultRace = true, .header = true});
		auto *inn = world.addBuilding("inn", 10, 8);
		REQUIRE(inn);
		auto &map = world.game.map;
		map.fillTerrain(OUTCROP);
		for (int y = inn->posY; y < inn->posY + inn->type->height; ++y)
			for (int x = inn->posX; x < inn->posX + inn->type->width; ++x)
				map.paintCell(x, y, GRASS);
		for (int x = inn->posX; x <= inn->posX + inn->type->width; ++x)
			map.setVertexTerrain(x, inn->posY, WATER);
		map.setVertexTerrain(inn->posX, inn->posY - 1, GRASS);
		map.setVertexTerrain(inn->posX + 1, inn->posY - 1, GRASS);
		REQUIRE(map.terrainAllowsMovementAt(inn->posX, inn->posY - 1, 0));
		int x = 0, y = 0, dx = 0, dy = 0;
		CHECK_FALSE(inn->findGroundExit(&x, &y, &dx, &dy, false, true));
		CHECK_FALSE(UnitTraining::exitTerrainSafe(3, 1, inn->posX, inn->posY, inn->type->width,
												  inn->type->height, map.cellView()));
		CHECK(inn->findGroundExit(&x, &y, &dx, &dy, true, true));
		CHECK_FALSE(inn->findGroundExit(&x, &y, &dx, &dy, false, false));
		CHECK_FALSE(inn->findExpelTile(false, false, &x, &y, &dx, &dy, false));
		map.setVertexTerrain(inn->posX, inn->posY, GRASS);
		CHECK(inn->findGroundExit(&x, &y, &dx, &dy, false, true));
		CHECK(dx == 0);
		CHECK(dy == -1);
		CHECK(UnitTraining::exitTerrainSafe(3, 1, inn->posX, inn->posY, inn->type->width,
											inn->type->height, map.cellView()));
	}
}

TEST_SUITE("VertexTerrain")
{
	TEST_CASE("learning flight requires an interior exit even when walking remains available")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world(
			{.wDec = 5, .hDec = 5, .loadDefaultRace = true, .header = true});
		auto *inn = world.addBuilding("inn", 10, 8);
		REQUIRE(inn);
		auto &map = world.game.map;
		map.importTerrainDefinitions(
			R"({"schemaVersion":1,"terrains":[{"key":"fixture:exit-no-fly","name":"No fly","base":"grass","properties":{"flyable":false},"appearance":"grass"}]})");
		const auto noFly = map.terrainRegistry().find("fixture:exit-no-fly");
		REQUIRE(noFly);
		for (int y = inn->posY; y < inn->posY + inn->type->height; ++y)
			for (int x = inn->posX; x < inn->posX + inn->type->width; ++x)
				map.paintCell(x, y, *noFly);
		int x = 0, y = 0, dx = 0, dy = 0;
		CHECK_FALSE(inn->findAirExit(&x, &y, &dx, &dy));
		CHECK(inn->findGroundExit(&x, &y, &dx, &dy, false, true));
		for (unsigned after : {4u, 5u})
			CHECK_FALSE(UnitTraining::exitTerrainSafe(1, after, inn->posX, inn->posY,
													  inn->type->width, inn->type->height,
													  map.cellView()));
		map.paintCell(inn->posX, inn->posY, GRASS);
		CHECK(inn->findAirExit(&x, &y, &dx, &dy));
		for (unsigned after : {4u, 5u})
			CHECK(UnitTraining::exitTerrainSafe(1, after, inn->posX, inn->posY, inn->type->width,
												inn->type->height, map.cellView()));
	}
}
