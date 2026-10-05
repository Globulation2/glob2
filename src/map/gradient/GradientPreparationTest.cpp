// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "MapInternal.h"
#include "gradient/GradientRuntime.h"
#include <type_traits>

#include <algorithm>
#include <vector>

namespace
{
// Deliberately retain the original scalar predicates as an independent oracle.
// In particular resource goals ignore terrain/buildings, and clearing goals
// also ignore immobile units. Reordering these predicates changes the game.
void scalarResource(Map &m, int team, int resource, int swim, Uint16 *out, bool markets)
{
	markets = markets && m.marketsV2Enabled();
	const Uint32 mask = Team::teamNumberToMask(team);
	const bool hide = globalContainer->resourcesTypes.get(resource)->visibleToBeCollected;
	for (size_t i = 0; i < m.size; ++i)
	{
		const auto &c = m.tiles[i];
		if ((c.forbidden & mask) || m.immobileUnits[i] != IMMOBILE_UNIT_NONE)
			out[i] = GRADIENT_FORBIDDEN;
		else if (c.resource.type == NO_RES_TYPE)
		{
			if (c.building != NOGBID)
				out[i] = markets && m.isStockedMarketTile(c.building, team, resource)
					? GRADIENT_MARKET_SEED : GRADIENT_FORBIDDEN;
			else if (!m.terrainPropertiesAt(i).walkable && !(swim > 0 && m.terrainPropertiesAt(i).swimmable))
				out[i] = GRADIENT_FORBIDDEN;
			else
				out[i] = GRADIENT_UNREACHABLE;
		}
		else if (c.resource.type == resource)
			out[i] = hide && !(m.fogOfWar[i] & mask) ? GRADIENT_FORBIDDEN : GRADIENT_AT_GOAL;
		else
			out[i] = GRADIENT_FORBIDDEN;
	}
}

void scalarClear(Map &m, int team, int swim, Uint16 *out)
{
	const Uint32 mask = Team::teamNumberToMask(team);
	for (size_t i = 0; i < m.size; ++i)
	{
		const auto &c = m.tiles[i];
		if (c.forbidden & mask)
			out[i] = GRADIENT_FORBIDDEN;
		else if (m.isClearingTarget(i, mask, m.farmAreasEnabled()))
			out[i] = GRADIENT_AT_GOAL;
		else if (m.immobileUnits[i] != IMMOBILE_UNIT_NONE)
			out[i] = GRADIENT_FORBIDDEN;
		else if (c.resource.type != NO_RES_TYPE)
			out[i] = GRADIENT_FORBIDDEN;
		else if (c.building != NOGBID)
			out[i] = GRADIENT_FORBIDDEN;
		else if (!m.terrainPropertiesAt(i).walkable && !(swim > 0 && m.terrainPropertiesAt(i).swimmable))
			out[i] = GRADIENT_FORBIDDEN;
		else
			out[i] = GRADIENT_UNREACHABLE;
	}
}

void scalarGuard(Map &m, int team, int swim, Uint16 *out)
{
	const Uint32 mask = Team::teamNumberToMask(team);
	bool painted = false;
	for (size_t i = 0; i < m.size; ++i)
	{
		const auto &c = m.tiles[i];
		if (c.forbidden & mask)
			out[i] = GRADIENT_FORBIDDEN;
		else if (m.immobileUnits[i] != IMMOBILE_UNIT_NONE)
			out[i] = GRADIENT_FORBIDDEN;
		else if (c.resource.type != NO_RES_TYPE)
			out[i] = GRADIENT_FORBIDDEN;
		else if (c.building != NOGBID && ((1u << Building::GIDtoTeam(c.building)) & m.game->teams[team]->allies))
			out[i] = GRADIENT_FORBIDDEN;
		else if (!m.terrainPropertiesAt(i).walkable && !(swim > 0 && m.terrainPropertiesAt(i).swimmable))
			out[i] = GRADIENT_FORBIDDEN;
		else if (c.guardArea & mask)
		{
			out[i] = GRADIENT_AT_GOAL;
			painted = true;
		}
		else
			out[i] = GRADIENT_UNREACHABLE;
	}
	// Crowding itself is unchanged: this oracle checks the seeded input and
	// whether crowding runs, while reusing the production crowding operation.
	if (painted && m.game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing))
		m.seedGuardAreaCrowding(team, out);
}

void requireSameField(const Map &map, const char *field,
	const std::vector<Uint16> &actual, const std::vector<Uint16> &expected)
{
	REQUIRE(actual.size() == expected.size());
	const auto mismatch = std::mismatch(actual.begin(), actual.end(), expected.begin());
	const size_t index = mismatch.first - actual.begin();
	// Report a cell and its two values instead of dumping an entire map buffer.
	if (index < actual.size())
	{
		INFO("Field: " << field);
		CAPTURE(index);
		const size_t x = index & map.wMask;
		const size_t y = index >> map.wDec;
		CAPTURE(x);
		CAPTURE(y);
		REQUIRE(actual[index] == expected[index]);
	}
}
}

static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<Map &>().getTile(0, 0))>>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<Map &>().getResource(0, 0))>>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<Map &>().getTiles())>>);

TEST_SUITE("GradientPreparation")
{
	TEST_CASE("resource cache tracks cell writes and falls back through mutation bursts")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec=7, .hDec=7, .teams=Team::MAX_COUNT, .clearImmobile=true, .header=true});
		auto &m = world.game.map;
		auto &cache = m.gradientRuntime->resourceSeeds;
		std::vector<Uint16> expected(m.size), actual(m.size);
		auto compare = [&](int phase) {
			for (int resource = 0; resource < MAX_RESOURCES; ++resource)
			{
				const int team = phase % Team::MAX_COUNT, swim = phase % SWIM_CLASS_COUNT;
				CAPTURE(team);
				CAPTURE(swim);
				CAPTURE(resource);
				scalarResource(m, team, resource, swim, expected.data(), false);
				m.seedResourcesGradient(team, resource, swim, actual.data(), false);
				requireSameField(m, "tracked resource", actual, expected);
				m.seedResourcesGradientDirect(team, resource, swim, actual.data(), false);
				requireSameField(m, "direct resource", actual, expected);
			}
		};
		compare(0);
		compare(1);
		REQUIRE(cache.valid);
		CHECK(cache.allocatedBytes() <= 16 * m.size);
		CHECK(cache.allocatedBytes() <= 32 * 1024 * 1024);
		for (int phase = 0; phase < 160; ++phase)
		{
			CAPTURE(phase);
			const size_t index = (phase * 7919u) % m.size;
			const int x = index & m.wMask, y = index >> m.wDec;
			switch (phase % 10)
			{
			case 0: m.replaceResource(index, Resource{WHEAT, 0, 1, 0}); break;
			case 1: m.setCellTerrain(index, WATER); break;
			case 2: m.setBuilding(x, y, 1, 1, 42); break;
			case 3: m.markImmobileUnit(x, y, 0); break;
			case 4: m.addForbidden(x, y, phase % Team::MAX_COUNT); break;
			case 5: m.replaceResource(index, Resource{WOOD, 0, 1, 0}); m.decResource(x, y); break;
			case 6: m.setCellTerrain(index, GRASS); m.incResource(x, y, WHEAT, 0); break;
			case 7: m.setResource(x, y, WHEAT, 0); m.setNoResource(x, y, 0); break;
			case 8: m.clearImmobileUnit(x, y); m.removeForbidden(x, y, phase % Team::MAX_COUNT); break;
			case 9:
			{
				auto cell = m.getTile(index);
				cell.forbidden ^= Team::teamNumberToMask(phase % Team::MAX_COUNT);
				cell.resource = {STONE, 0, 1, 0};
				cell.building = NOGBID;
				m.replaceTile(index, cell);
				break;
			}
			}
			compare(phase);
		}
		// Publish each occupied state before removing it. Creating and deleting
		// between preparations alone would not detect a missing removal notice.
		const size_t at = m.coordToIndex(11, 11);
		m.replaceTile(at, Tile{});
		m.setCellTerrain(at, GRASS);
		m.clearImmobileUnit(11, 11);
		m.replaceResource(at, Resource{WHEAT, 0, 1, 0});
		compare(0);
		m.decResource(11, 11);
		compare(0);
		m.incResource(11, 11, WHEAT, 0);
		compare(0);
		m.setNoResource(11, 11, 0);
		compare(0);
		m.setBuilding(11, 11, 1, 1, 42);
		compare(0);
		m.setBuilding(11, 11, 1, 1, NOGBID);
		compare(0);
		m.markImmobileUnit(11, 11, 0);
		compare(0);
		m.clearImmobileUnit(11, 11);
		compare(0);
		m.addForbidden(11, 11, 0);
		compare(0);
		m.removeForbidden(11, 11, 0);
		compare(0);
		m.replaceResource(at, Resource{WHEAT, 0, 1, 0});
		compare(0);
		m.setCellTerrain(at, WATER);
		compare(0);
		m.removeUnallowedResources(11, 11, 1, 1);
		compare(0);
		// Repeated edits to one cell must not fill the deduplicated queue.
		for (int i = 0; i < 1024; ++i)
			m.setAreaMask(0, &Tile::forbidden, i & 1);
		CHECK(cache.valid);
		compare(0);
		// Reads and amount-only changes are irrelevant to seed contents.
		(void)m.getTile(0);
		(void)m.getResource(0);
		(void)m.getTiles();
		m.setResourceAmount(0, 2);
		m.setResourcesGrow(0, 0, 255);
		CHECK(m.getTile(0).canResourcesGrow == 255);
		CHECK(cache.changes == 0);
		for (size_t i = 0; i <= m.size / 64; ++i)
			m.setAreaMask(i, &Tile::forbidden, m.getTile(i).forbidden ^ 1);
		CHECK_FALSE(cache.valid);
		for (int phase = 0; phase < 4; ++phase) compare(phase);
		CHECK(cache.valid);
		// Bulk reset frees the cache; enough subsequent requests rebuild it.
		m.setSize(8, 8, WATER);
		expected.resize(m.size);
		actual.resize(m.size);
		REQUIRE_FALSE(cache.storage);
		compare(0);
		compare(1);
		CHECK(cache.valid);
		m.setSize(6, 6, GRASS);
		expected.resize(m.size);
		actual.resize(m.size);
		for (int phase = 0; phase < 4; ++phase) compare(phase);
		CHECK_FALSE(cache.storage);
	}

	TEST_CASE("concurrent preparation owns buffers and serializes dirty cache refresh")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec=7, .hDec=7, .teams=2, .clearImmobile=true, .header=true});
		auto &m = world.game.map;
		std::vector<Uint16> expected(m.size);
		for (int pass = 0; pass < 16; ++pass)
			m.seedResourcesGradient(0, WHEAT, 0, expected.data(), false);
		REQUIRE(m.gradientRuntime->resourceSeeds.valid);
		m.replaceResource(0, Resource{WHEAT, 0, 1, 0});
		m.setCellTerrain(1, WATER);
		m.addForbidden(0, 0, 1);
		std::vector<std::vector<Uint16>> outputs(16, std::vector<Uint16>(m.size));
		m.configureCompute(4, Map::ComputeInitialize);
		m.computeExecutor().run(outputs.size(), [&](size_t job) {
			m.seedResourcesGradient(job % 2, job % MAX_RESOURCES, job % SWIM_CLASS_COUNT,
				outputs[job].data(), false);
		});
		for (size_t job = 0; job < outputs.size(); ++job)
		{
			CAPTURE(job);
			scalarResource(m, job % 2, job % MAX_RESOURCES, job % SWIM_CLASS_COUNT, expected.data(), false);
			requireSameField(m, "concurrent preparation", outputs[job], expected);
		}
	}

	TEST_CASE("custom terrain and registry replacement preserve cached and direct seeds")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec=7, .hDec=7, .clearImmobile=true, .header=true});
		auto &m = world.game.map;
		auto importTerrain = [&](std::string_view definitions) {
			// Detached maps are the existing pre-match authoring interface.
			m.game = nullptr;
			m.importTerrainDefinitions(definitions);
			m.setGame(&world.game);
		};
		importTerrain(R"({"schemaVersion":1,"terrains":[{"key":"test:seed","name":"Seed terrain","base":"grass","appearance":"sand","properties":{"walkable":false,"swimmable":true}}]})");
		const auto terrain = *m.terrainRegistry().find("test:seed");
		m.setCellTerrain(0, terrain);
		m.replaceResource(1, Resource{WHEAT, 0, 1, 0});
		m.setCellTerrain(1, terrain);
		m.addFarmArea(1, 0, 0);
		world.game.gameHeader.getExperiments().set(ExperimentId::FarmAreas, true);
		std::vector<Uint16> expected(m.size), actual(m.size);
		for (int phase = 0; phase < 2; ++phase)
		{
			for (int pass = 0; pass < 24; ++pass)
			{
				const int swim = pass % SWIM_CLASS_COUNT, resource = pass % MAX_RESOURCES;
				scalarResource(m, 0, resource, swim, expected.data(), false);
				m.seedResourcesGradient(0, resource, swim, actual.data(), false);
				requireSameField(m, "custom cached terrain", actual, expected);
				m.seedResourcesGradientDirect(0, resource, swim, actual.data(), false);
				requireSameField(m, "custom direct terrain", actual, expected);
				scalarClear(m, 0, swim, expected.data());
				m.seedClearAreasGradient(0, swim, actual.data());
				requireSameField(m, "custom clearing terrain", actual, expected);
			}
			REQUIRE(m.gradientRuntime->resourceSeeds.valid);
			if (!phase)
			{
				importTerrain(R"({"schemaVersion":1,"terrains":[{"key":"test:seed","name":"Seed terrain","base":"water","appearance":"sand","properties":{"walkable":true,"swimmable":false}}]})");
				CHECK_FALSE(m.gradientRuntime->resourceSeeds.valid);
			}
		}
	}

	TEST_CASE("optional cache storage can be unavailable or over budget")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec=7, .hDec=7, .clearImmobile=true, .header=true});
		auto &m = world.game.map;
		auto &cache = m.gradientRuntime->resourceSeeds;
		// Exercise the permanent fallback state without exhausting process memory.
		cache.allocationFailed = true;
		m.replaceResource(0, Resource{WHEAT, 0, 1, 0});
		std::vector<Uint16> expected(m.size), actual(m.size);
		for (int pass = 0; pass < 24; ++pass)
		{
			scalarResource(m, 0, WHEAT, pass % SWIM_CLASS_COUNT, expected.data(), false);
			m.seedResourcesGradient(0, WHEAT, pass % SWIM_CLASS_COUNT, actual.data(), false);
			requireSameField(m, "unavailable cache", actual, expected);
		}
		CHECK_FALSE(cache.storage);
		// A 2048-square map exceeds the conservative 16-byte/cell gate.
		m.setSize(11, 11, GRASS);
		actual.resize(m.size);
		m.seedResourcesGradient(0, WHEAT, 0, actual.data(), false);
		CHECK_FALSE(cache.storage);
		CHECK(std::all_of(actual.begin(), actual.end(), [](Uint16 value) {
			return value == GRADIENT_UNREACHABLE;
		}));
	}

	TEST_CASE("seed cells match scalar predicates across policies and changing overlays")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec=8, .hDec=8, .teams=2, .clearImmobile=true, .loadDefaultRace=true, .header=true});
		auto &m = world.game.map;
		auto *market0 = world.addBuilding("market", 2, 2);
		auto *market1 = world.addBuilding("market", 8, 8, 0, 1);
		REQUIRE(market0);
		REQUIRE(market1);
		REQUIRE(world.addUnit(WARRIOR, 32, 32));
		REQUIRE(world.addUnit(WARRIOR, 33, 33, 1));
		// Cartesian product, including deliberately overlapping goals/blockers.
		size_t i = 0;
		for (unsigned terrain=0; terrain<TERRAIN_COUNT; ++terrain)
		for (int resource=0; resource<=MAX_RESOURCES; ++resource)
		for (unsigned forbidden=0; forbidden<4; ++forbidden)
		for (unsigned immobile=0; immobile<2; ++immobile)
		for (unsigned building=0; building<3; ++building)
		for (unsigned area=0; area<4; ++area)
		for (unsigned fog=0; fog<4; ++fog, ++i)
		{
			REQUIRE(i < m.size);
			auto &c = m.tiles[i];
			m.setCellTerrain(i, static_cast<TerrainType>(terrain));
			c.resource.type = resource == MAX_RESOURCES ? NO_RES_TYPE : resource;
			c.forbidden = forbidden;
			m.immobileUnits[i] = immobile ? 0 : IMMOBILE_UNIT_NONE;
			c.building = building == 0 ? NOGBID : building == 1 ? market0->gid : market1->gid;
			c.clearArea = area;
			c.farmArea = area ^ 3;
			c.guardArea = area;
			m.fogOfWarA[i] = fog;
			m.fogOfWarB[i] = fog ^ 3;
		}
		std::vector<Uint16> expected(m.size), actual(m.size);
		for (unsigned threads : {1, 2, 4})
		{
			m.configureCompute(threads, Map::ComputeInitialize);
			for (int phase=0; phase<4; ++phase)
			{
				world.game.gameHeader.getExperiments().set(ExperimentId::FarmAreas, phase & 1);
				world.game.gameHeader.getExperiments().set(ExperimentId::MarketsV2, phase & 2);
				world.game.gameHeader.getExperiments().set(ExperimentId::GuardAreaBalancing, phase & 1);
				m.fogOfWar = phase & 1 ? m.fogOfWarB.data() : m.fogOfWarA.data();
				world.game.teams[0]->allies = phase & 1 ? 3 : 1;
				for (int r=0; r<MAX_RESOURCES; ++r)
				{
					market0->resources[r] = phase & 1 ? 0 : 10;
					market1->resources[r] = phase & 1 ? 10 : 0;
				}
				for (int team=0; team<2; ++team)
				for (int swim=0; swim<SWIM_CLASS_COUNT; ++swim)
				{
					CAPTURE(threads);
					CAPTURE(phase);
					CAPTURE(team);
					CAPTURE(swim);
					for (int resource=0; resource<MAX_RESOURCES; ++resource)
					for (bool markets : {false, true})
					{
						CAPTURE(resource);
						CAPTURE(markets);
						scalarResource(m, team, resource, swim, expected.data(), markets);
						m.seedResourcesGradient(team, resource, swim, actual.data(), markets);
						requireSameField(m, "resource", actual, expected);
						m.seedResourcesGradientDirect(team, resource, swim, actual.data(), markets);
						requireSameField(m, "direct resource", actual, expected);
					}
					scalarClear(m, team, swim, expected.data());
					m.seedClearAreasGradient(team, swim, actual.data());
					requireSameField(m, "clearing", actual, expected);
					scalarGuard(m, team, swim, expected.data());
					m.seedGuardAreasGradient(team, swim, actual.data());
					requireSameField(m, "guard", actual, expected);
				}
			}
		}
	}

	TEST_CASE("all supported team masks match the scalar fields")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.teams=Team::MAX_COUNT, .clearImmobile=true, .header=true});
		auto &m = world.game.map;
		world.game.gameHeader.getExperiments().set(ExperimentId::FarmAreas, true);
		for (size_t i=0; i<m.size; ++i)
		{
			auto &c = m.tiles[i];
			const Uint32 mask = Team::teamNumberToMask(i % Team::MAX_COUNT);
			m.setCellTerrain(i, static_cast<TerrainType>(i % TERRAIN_COUNT));
			c.resource.type = i % (MAX_RESOURCES + 1) == MAX_RESOURCES ? NO_RES_TYPE : i % (MAX_RESOURCES + 1);
			c.forbidden = i & 1 ? mask : ~mask;
			c.clearArea = i & 2 ? mask : 0;
			c.farmArea = i & 4 ? mask : 0;
			m.fogOfWar[i] = i & 8 ? mask : ~mask;
		}
		std::vector<Uint16> expected(m.size), actual(m.size);
		for (int team=0; team<Team::MAX_COUNT; ++team)
		for (int swim=0; swim<SWIM_CLASS_COUNT; ++swim)
		{
			CAPTURE(team);
			CAPTURE(swim);
			for (int resource=0; resource<MAX_RESOURCES; ++resource)
			{
				CAPTURE(resource);
				scalarResource(m, team, resource, swim, expected.data(), false);
				m.seedResourcesGradient(team, resource, swim, actual.data(), false);
				requireSameField(m, "resource", actual, expected);
			}
			scalarClear(m, team, swim, expected.data());
			m.seedClearAreasGradient(team, swim, actual.data());
			requireSameField(m, "clearing", actual, expected);
		}
	}

	TEST_CASE("explicit clearing overrides farm crop protection but never forbidden paint")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.clearImmobile=true, .header=true});
		auto &m = world.game.map;
		const Uint32 teamMask = Team::teamNumberToMask(0);
		std::vector<Uint16> actual(m.size);
		for (TerrainType terrain : {GRASS, WATER})
		{
			CAPTURE(terrain);
			m.setCellTerrain(0, terrain);
			auto &cell = m.tiles[0];
			cell.resource.type = terrainProperties(terrain).farmCrop;
			REQUIRE(globalContainer->resourcesTypes.get(cell.resource.type)->clearable);
			cell.farmArea = teamMask;
			cell.building = 42;
			m.immobileUnits[0] = 0;
			for (bool farming : {false, true})
			{
				CAPTURE(farming);
				world.game.gameHeader.getExperiments().set(ExperimentId::FarmAreas, farming);
				cell.forbidden = 0;
				cell.clearArea = 0;
				m.seedClearAreasGradient(0, 0, actual.data());
				REQUIRE(actual[0] == GRADIENT_FORBIDDEN);

				// Explicit clearing wins even on water and an occupied cell.
				cell.clearArea = teamMask;
				m.seedClearAreasGradient(0, 0, actual.data());
				REQUIRE(actual[0] == GRADIENT_AT_GOAL);

				cell.forbidden = teamMask;
				m.seedClearAreasGradient(0, 0, actual.data());
				REQUIRE(actual[0] == GRADIENT_FORBIDDEN);
			}
		}
	}

	TEST_CASE("mutation and reset boundaries use current state")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec=6, .hDec=6, .teams=2, .clearImmobile=true, .header=true});
		auto &m = world.game.map;
		for (int phase=0; phase<12; ++phase)
		{
			CAPTURE(phase);
			if (phase == 8)
				m.setSize(7, 7, WATER);
			m.setResource(20, 20, phase % MAX_RESOURCES, 0);
			if (phase & 1)
				m.replaceResource(20, 20, Resource{});
			m.setBuilding(21, 20, 1, 1, phase & 1 ? NOGBID : 42);
			if (phase & 1)
				m.markImmobileUnit(22, 20, 0);
			else
				m.clearImmobileUnit(22, 20);
			m.addClearArea(20, 20, phase % 2);
			m.addForbidden(22, 20, phase % 2);
			std::vector<Uint16> expected(m.size), actual(m.size);
			for (int swim=0; swim<SWIM_CLASS_COUNT; ++swim)
			{
				CAPTURE(swim);
				scalarResource(m, 0, WHEAT, swim, expected.data(), false);
				m.seedResourcesGradient(0, WHEAT, swim, actual.data(), false);
				requireSameField(m, "resource", actual, expected);
				scalarClear(m, 0, swim, expected.data());
				m.seedClearAreasGradient(0, swim, actual.data());
				requireSameField(m, "clearing", actual, expected);
			}
		}
	}

	TEST_CASE("concurrent lazy requests publish one complete resource field")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec=7, .hDec=7, .teams=2, .clearImmobile=true, .header=true});
		auto &m = world.game.map;
		for (int resource=0; resource<3; ++resource)
			m.setResource(20 + resource, 20, resource, 0);
		std::vector<Uint16> warmup(m.size);
		for (int i = 0; i < 16; ++i) m.seedResourcesGradient(0, WHEAT, 0, warmup.data(), false);
		REQUIRE(m.gradientRuntime->resourceSeeds.valid);
		for (unsigned workers : {0, 1, 2})
		{
			CAPTURE(workers);
			// Use an unallocated field for every worker count.
			const int resource = workers;
			REQUIRE(m.resourcesGradient[0][resource][0] == nullptr);
			std::vector<Uint16> expected(m.size);
			scalarResource(m, 0, resource, 0, expected.data(), false);
			m.propagateGradient(expected.data(), 0);
			m.configureGradientPipeline(workers, 8);
			m.configureCompute(4, Map::ComputeAI);
			std::vector<Uint16 *> requests(8);
			m.computeExecutor().run(requests.size(), [&](size_t j) {
				requests[j] = m.getResourceGradient(0, resource, 0);
			});
			REQUIRE(requests[0] != nullptr);
			for (size_t j=1; j<requests.size(); ++j)
				REQUIRE(requests[j] == requests[0]);
			requireSameField(m, "lazy resource", std::vector<Uint16>(requests[0], requests[0] + m.size), expected);
		}
	}
}
