// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "MapInternal.h"

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

TEST_SUITE("GradientPreparation")
{
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
			m.terrainIds[i] = static_cast<TerrainType>(terrain);
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
			m.terrainIds[i] = static_cast<TerrainType>(i % TERRAIN_COUNT);
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
			m.terrainIds[0] = terrain;
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
				m.getTile(20, 20).resource.clear();
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
		glob2test::HeadlessGame world({.wDec=6, .hDec=6, .teams=2, .clearImmobile=true, .header=true});
		auto &m = world.game.map;
		for (int resource=0; resource<3; ++resource)
			m.setResource(20 + resource, 20, resource, 0);
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
