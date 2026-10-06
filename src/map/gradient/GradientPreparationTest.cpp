// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "MapInternal.h"
#include "gradient/GradientRuntime.h"
#include <type_traits>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
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
    for (size_t i=0;i<m.size;++i)
    {
        const auto& c=m.tiles[i];
        if ((c.forbidden&mask) || m.immobileUnits[i]!=IMMOBILE_UNIT_NONE)
            out[i]=GRADIENT_FORBIDDEN;
        else if (m.materialAmountAtSlot(i,resource)>0 && (!m.resourceVisibleToHarvest(i) || (m.fogOfWar[i]&mask)))
            out[i]=GRADIENT_AT_GOAL;
        else if (m.resourceBlocksGround(i)) out[i]=GRADIENT_FORBIDDEN;
        else if (c.building!=NOGBID)
            out[i]=markets && m.isStockedMarketTile(c.building,team,resource) ? GRADIENT_AT_GOAL-5*GRADIENT_STEP : GRADIENT_FORBIDDEN;
        else if (!m.terrainPropertiesAt(i).walkable && !(swim>0 && m.terrainPropertiesAt(i).swimmable))
            out[i]=GRADIENT_FORBIDDEN;
        else out[i]=GRADIENT_UNREACHABLE;
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
		else if (m.resourceBlocksGround(i))
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
		else if (m.resourceBlocksGround(i))
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
	TEST_CASE("warm preparation keeps custom supplier unions exclusions penalties and overlays live")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec=7, .hDec=7, .teams=2, .discovered=true, .clearImmobile=true, .loadDefaultRace=true, .header=true});
		auto& game=world.game; auto& m=game.map;
		auto catalog=nlohmann::json::parse(game.buildingsTypes.snapshotJson());
		const char* names[]={"inn","hospital","racetrack","swimmingpool"};
		for (int k=0;k<4;++k)
		{
			auto& spec=catalog["variants"][game.buildingsTypes.getFinishedTypeNum(names[k])];
			spec["properties"]["maxMaterial"][WHEAT]=20;
			auto& semantics=spec["semantics"];
			semantics["occupiesGround"]=k!=1;
			auto& market=semantics["market"];
			market["sharedStock"]=k>=2;
			market["suppliesStock"]=k!=1;
			market["suppliesDirectStock"]=k==1;
			market["suppliesStockExperiment"]="";
			market["suppliesStockMaterials"]={"food"};
			market["suppliesDirectStockMaterials"]={"food"};
			market["pickupPenalty"]=k+2;
		}
		game.buildingsTypes.loadSnapshotJson(catalog.dump()); game.configureBuildingCatalog();
		auto* unified=world.addBuilding(names[0],8,8);
		auto* direct=world.addBuilding(names[1],20,8);
		auto* shared=world.addBuilding(names[2],32,8);
		auto* consumer=world.addBuilding(names[3],44,8);
		REQUIRE(unified); REQUIRE(direct); REQUIRE(shared); REQUIRE(consumer);
		auto* foreign=world.addBuilding(names[0],56,8,0,1); REQUIRE(foreign);
		foreign->materials[WHEAT]=10;
		unified->materials[WHEAT]=10; direct->materials[WHEAT]=10; shared->materials[WHEAT]=10;
		auto& cache=m.gradientRuntime->resourceSeeds;
		std::vector<Uint16> expected(m.size), actual(m.size);
		cache.allocationFailed=true;
		m.seedMaterialGradient(0,WHEAT,0,expected.data(),true,consumer,3);
		cache.allocationFailed=false;
		for(int i=0;i<32;++i)m.seedMaterialGradient(0,WHEAT,0,actual.data(),true,consumer,3);
		REQUIRE(cache.valid);
		requireSameField(m,"custom cached suppliers",actual,expected);
		CHECK(actual[m.coordToIndex(8,8)]==GRADIENT_AT_GOAL-2*GRADIENT_STEP);
		CHECK(actual[m.coordToIndex(20,8)]==GRADIENT_AT_GOAL-3*GRADIENT_STEP);
		CHECK(actual[m.coordToIndex(32,8)]==GRADIENT_FORBIDDEN);
		CHECK(actual[m.coordToIndex(44,8)]==GRADIENT_FORBIDDEN);
		CHECK(actual[m.coordToIndex(56,8)]==GRADIENT_FORBIDDEN);
		// Live stock changes must not wait for template invalidation.
		direct->materials[WHEAT]=0;
		m.seedMaterialGradient(0,WHEAT,0,actual.data(),true,consumer,3);
		CHECK(cache.valid);
		CHECK(actual[m.coordToIndex(20,8)]==GRADIENT_UNREACHABLE);
		direct->materials[WHEAT]=10;
		m.addForbidden(20,8,0); m.markImmobileUnit(21,8,0);
		m.seedMaterialGradient(0,WHEAT,0,actual.data(),true,consumer,3);
		CHECK(actual[m.coordToIndex(20,8)]==GRADIENT_FORBIDDEN);
		CHECK(actual[m.coordToIndex(21,8)]==GRADIENT_FORBIDDEN);
		m.seedMaterialGradient(0,WOOD,0,actual.data(),true,consumer,3);
		CHECK(actual[m.coordToIndex(8,8)]==GRADIENT_FORBIDDEN);
		CHECK(actual[m.coordToIndex(20,9)]==GRADIENT_UNREACHABLE);
		// Mode selection remains independent, including overlay-only Direct mode.
		m.removeForbidden(20,8,0);
		m.seedMaterialGradient(0,WHEAT,0,actual.data(),true,consumer,2);
		CHECK(actual[m.coordToIndex(8,8)]==GRADIENT_FORBIDDEN);
		CHECK(actual[m.coordToIndex(20,8)]==GRADIENT_AT_GOAL-3*GRADIENT_STEP);
	}


	TEST_CASE("supplier seed indexing rejects foreign and invalid building IDs in cached and direct fields")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec=7, .hDec=7, .teams=Team::MAX_COUNT,
			.discovered=true, .clearImmobile=true, .header=true});
		auto& m=world.game.map;
		const int lastTeam=Team::MAX_COUNT-1;
		const std::array<Uint16,6> gids={Building::GIDfrom(0,0),
			Building::GIDfrom(Building::MAX_COUNT-1,0), Building::GIDfrom(0,lastTeam),
			Building::GIDfrom(Building::MAX_COUNT-1,lastTeam), Uint16(NOGBID-1), NOGBID};
		for (size_t i=0;i<gids.size();++i) m.setBuilding(4+int(i),4,1,1,gids[i]);
		std::array<Uint16,Building::MAX_COUNT> seeds{};
		seeds[0]=GRADIENT_AT_GOAL-2*GRADIENT_STEP;
		seeds.back()=GRADIENT_AT_GOAL-4*GRADIENT_STEP;
		std::vector<Uint16> direct(m.size),cached(m.size);
		for (int team:{0,lastTeam})
		{
			CAPTURE(team);
			m.seedMaterialGradientDirect(team,WHEAT,0,direct.data(),seeds.data());
			for (int request=0;request<32;++request)
				m.gradientRuntime->resourceSeeds.trySeed(m,team,WHEAT,0,cached.data(),seeds.data());
			REQUIRE(m.gradientRuntime->resourceSeeds.valid);
			requireSameField(m,"supplier GID bounds",cached,direct);
			const int ownOffset=team==0 ? 0 : 2;
			const int otherOffset=team==0 ? 2 : 0;
			CHECK(cached[m.coordToIndex(4+ownOffset,4)]==seeds[0]);
			CHECK(cached[m.coordToIndex(5+ownOffset,4)]==seeds.back());
			CHECK(cached[m.coordToIndex(4+otherOffset,4)]==GRADIENT_FORBIDDEN);
			CHECK(cached[m.coordToIndex(5+otherOffset,4)]==GRADIENT_FORBIDDEN);
			CHECK(cached[m.coordToIndex(8,4)]==GRADIENT_FORBIDDEN);
			CHECK(cached[m.coordToIndex(9,4)]==GRADIENT_UNREACHABLE);
		}
	}

	TEST_CASE("resource cache tracks cell writes and falls back through mutation bursts")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec=7, .hDec=7, .teams=Team::MAX_COUNT, .clearImmobile=true, .header=true});
		auto &m = world.game.map;
		auto &cache = m.gradientRuntime->resourceSeeds;
		std::vector<Uint16> expected(m.size), actual(m.size);
		auto compare = [&](int phase) {
			for (int resource = 0; resource < MaterialCount; ++resource)
			{
				const int team = phase % Team::MAX_COUNT, swim = phase % SWIM_CLASS_COUNT;
				CAPTURE(team);
				CAPTURE(swim);
				CAPTURE(resource);
				scalarResource(m, team, resource, swim, expected.data(), false);
				m.seedMaterialGradient(team, resource, swim, actual.data(), false);
				requireSameField(m, "tracked resource", actual, expected);
				m.seedMaterialGradientDirect(team, resource, swim, actual.data(), nullptr);
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
			case 6: m.setCellTerrain(index, GRASS); m.incResourceByIndex(x, y, WHEAT, 0); break;
			case 7: m.setResourceByIndex(x, y, WHEAT, 0); m.setNoResource(x, y, 0); break;
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
		m.incResourceByIndex(11, 11, WHEAT, 0);
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

	TEST_CASE("cached natural goals follow immobile occupancy and live overlays")
	{
		glob2test::HeadlessGlobals globals;
		int hiddenResource = -1;
		for (int resource = 0; resource < 8; ++resource)
			if (ResourceRegistry::builtins()->properties(static_cast<ResourceId>(resource)).visibleToHarvest)
			{
				hiddenResource = resource;
				break;
			}
		REQUIRE(hiddenResource >= 0);
		for (const int wDec : {5, 7})
		{
			CAPTURE(wDec);
			glob2test::HeadlessGame world({.wDec=wDec, .hDec=wDec, .teams=2, .clearImmobile=true, .header=true});
			auto &m = world.game.map;
			auto &cache = m.gradientRuntime->resourceSeeds;
			const int x = 10, y = 10;
			const size_t at = m.coordToIndex(x, y);
			const Uint32 mask = Team::teamNumberToMask(0);
			std::vector<Uint16> expected(m.size), actual(m.size);
			m.replaceTile(at, Tile{});
			m.setCellTerrain(at, GRASS);
			m.clearImmobileUnit(x, y);
			m.fogOfWar[at] = mask;
			m.replaceResource(at, Resource{WHEAT, 0, 1, 0});
			auto compare = [&](int checkedResource, Uint16 checkedValue) {
				for (const int swim : {0, 3})
					for (const int resource : {int(WHEAT), int(WOOD), hiddenResource})
					{
						CAPTURE(swim);
						CAPTURE(resource);
						scalarResource(m, 0, resource, swim, expected.data(), false);
						m.seedMaterialGradient(0, resource, swim, actual.data(), false);
						requireSameField(m, "occupied natural goal", actual, expected);
						if (resource == checkedResource) CHECK(actual[at] == checkedValue);
						m.seedMaterialGradientDirect(0, resource, swim, actual.data(), nullptr);
						requireSameField(m, "direct occupied natural goal", actual, expected);
					}
			};
			auto warm = [&] {
				for (int i = 0; i < 32; ++i)
					m.seedMaterialGradient(0, WHEAT, 0, actual.data(), false);
				CHECK(cache.valid == (wDec == 7));
			};
			warm();
			const auto bytes = cache.allocatedBytes();
			compare(WHEAT, GRADIENT_AT_GOAL);
			m.markImmobileUnit(x, y, 0);
			compare(WHEAT, GRADIENT_FORBIDDEN);
			m.clearImmobileUnit(x, y);
			compare(WHEAT, GRADIENT_AT_GOAL);
			m.markImmobileUnit(x, y, 1);
			compare(WHEAT, GRADIENT_FORBIDDEN);
			m.replaceResource(at, Resource{WOOD, 0, 1, 0});
			compare(WOOD, GRADIENT_FORBIDDEN);
			m.clearImmobileUnit(x, y);
			compare(WOOD, GRADIENT_AT_GOAL);
			m.markImmobileUnit(x, y, 0);
			m.replaceResource(at, Resource{});
			compare(WOOD, GRADIENT_FORBIDDEN);
			m.clearImmobileUnit(x, y);
			compare(WOOD, GRADIENT_UNREACHABLE);
			// Coalesced resource/occupancy edits use the final effective goal type.
			m.replaceResource(at, Resource{WHEAT, 0, 1, 0});
			m.markImmobileUnit(x, y, 0);
			m.replaceResource(at, Resource{WOOD, 0, 1, 0});
			m.clearImmobileUnit(x, y);
			m.markImmobileUnit(x, y, 1);
			compare(WOOD, GRADIENT_FORBIDDEN);
			m.clearImmobileUnit(x, y);
			compare(WOOD, GRADIENT_AT_GOAL);
			// A coalesced removal must clear a previously published resource bit,
			// even when the final cell is empty and unoccupied again.
			m.replaceResource(at, Resource{});
			m.markImmobileUnit(x, y, 0);
			m.clearImmobileUnit(x, y);
			compare(WOOD, GRADIENT_UNREACHABLE);
			// Empty-cell occupancy still updates base passability without a goal.
			m.markImmobileUnit(x, y, 0);
			compare(WOOD, GRADIENT_FORBIDDEN);
			m.clearImmobileUnit(x, y);
			compare(WOOD, GRADIENT_UNREACHABLE);
			// Goals still override terrain/buildings, with fog live and paint last.
			m.replaceResource(at, Resource{static_cast<Uint8>(hiddenResource), 0, 1, 0});
			m.setCellTerrain(at, WATER);
			m.setBuilding(x, y, 1, 1, 42);
			m.fogOfWar[at] = 0;
			compare(hiddenResource, GRADIENT_FORBIDDEN);
			m.fogOfWar[at] = mask;
			compare(hiddenResource, GRADIENT_AT_GOAL);
			m.addForbidden(x, y, 0);
			compare(hiddenResource, GRADIENT_FORBIDDEN);
			m.removeForbidden(x, y, 0);
			compare(hiddenResource, GRADIENT_AT_GOAL);
			m.markImmobileUnit(x, y, 0);
			compare(hiddenResource, GRADIENT_FORBIDDEN);
			cache.invalidate();
			warm();
			compare(hiddenResource, GRADIENT_FORBIDDEN);
			m.clearImmobileUnit(x, y);
			compare(hiddenResource, GRADIENT_AT_GOAL);
            // The first appearances of wood and the hidden material each add
            // one lazy goal bitset. After these materials have been observed,
            // occupancy churn, invalidation and rebuild must allocate nothing.
            const auto warmedBytes=cache.allocatedBytes();
            const auto goalBytes=((m.size+63)/64)*sizeof(Uint64);
            CHECK(warmedBytes==bytes+(wDec==7 ? 2*goalBytes : 0));
			if (wDec == 7)
			{
				// A burst exceeds the existing dirty bound; fallback and rebuild
				// must both preserve blocked resource membership.
				for (size_t i = 0; i <= m.size / 64; ++i)
				{
					m.replaceResource(i, Resource{WHEAT, 0, 1, 0});
					m.markImmobileUnit(i & m.wMask, i >> m.wDec, 0);
				}
				REQUIRE_FALSE(cache.valid);
				compare(hiddenResource, GRADIENT_AT_GOAL);
				warm();
				compare(hiddenResource, GRADIENT_AT_GOAL);
				for (size_t i = 0; i <= m.size / 64; ++i)
					m.clearImmobileUnit(i & m.wMask, i >> m.wDec);
				REQUIRE_FALSE(cache.valid);
				compare(hiddenResource, GRADIENT_AT_GOAL);
				warm();
				compare(hiddenResource, GRADIENT_AT_GOAL);
				CHECK(cache.allocatedBytes() == warmedBytes);
			}
		}
	}

	TEST_CASE("concurrent preparation owns buffers and serializes dirty cache refresh")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.wDec=7, .hDec=7, .teams=2, .clearImmobile=true, .header=true});
		auto &m = world.game.map;
		std::vector<Uint16> expected(m.size);
		for (int pass = 0; pass < 16; ++pass)
			m.seedMaterialGradient(0, WHEAT, 0, expected.data(), false);
		REQUIRE(m.gradientRuntime->resourceSeeds.valid);
		m.replaceResource(0, Resource{WHEAT, 0, 1, 0});
		m.setCellTerrain(1, WATER);
		m.addForbidden(0, 0, 1);
		std::vector<std::vector<Uint16>> outputs(16, std::vector<Uint16>(m.size));
		m.configureCompute(4, Map::ComputeInitialize);
		m.computeExecutor().run(outputs.size(), [&](size_t job) {
			m.seedMaterialGradient(job % 2, job % MaterialCount, job % SWIM_CLASS_COUNT,
				outputs[job].data(), false);
		});
		for (size_t job = 0; job < outputs.size(); ++job)
		{
			CAPTURE(job);
			scalarResource(m, job % 2, job % MaterialCount, job % SWIM_CLASS_COUNT, expected.data(), false);
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
				const int swim = pass % SWIM_CLASS_COUNT, resource = pass % MaterialCount;
				scalarResource(m, 0, resource, swim, expected.data(), false);
				m.seedMaterialGradient(0, resource, swim, actual.data(), false);
				requireSameField(m, "custom cached terrain", actual, expected);
				m.seedMaterialGradientDirect(0, resource, swim, actual.data(), nullptr);
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
			m.seedMaterialGradient(0, WHEAT, pass % SWIM_CLASS_COUNT, actual.data(), false);
			requireSameField(m, "unavailable cache", actual, expected);
		}
		CHECK_FALSE(cache.storage);
		// A 2048-square map exceeds the conservative 16-byte/cell gate.
		m.setSize(11, 11, GRASS);
		actual.resize(m.size);
		m.seedMaterialGradient(0, WHEAT, 0, actual.data(), false);
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
		for (int resource=0; resource<=8; ++resource)
		for (unsigned forbidden=0; forbidden<4; ++forbidden)
		for (unsigned immobile=0; immobile<2; ++immobile)
		for (unsigned building=0; building<3; ++building)
		for (unsigned area=0; area<4; ++area)
		for (unsigned fog=0; fog<4; ++fog, ++i)
		{
			REQUIRE(i < m.size);
			auto &c = m.tiles[i];
			m.setCellTerrain(i, static_cast<TerrainType>(terrain));
			c.resource.type = resource == 8 ? NO_RES_TYPE : resource;
            c.resource.amount = resource == 8 ? 0 : 1;
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
				world.game.configureBuildingCatalog();
				m.fogOfWar = phase & 1 ? m.fogOfWarB.data() : m.fogOfWarA.data();
				world.game.teams[0]->allies = phase & 1 ? 3 : 1;
				for (int r=0; r<MaterialCount; ++r)
				{
					market0->materials[r] = phase & 1 ? 0 : 10;
					market1->materials[r] = phase & 1 ? 10 : 0;
				}
				for (int team=0; team<2; ++team)
				for (int swim=0; swim<SWIM_CLASS_COUNT; ++swim)
				{
					CAPTURE(threads);
					CAPTURE(phase);
					CAPTURE(team);
					CAPTURE(swim);
					for (int resource=0; resource<MaterialCount; ++resource)
					for (bool markets : {false, true})
					{
						CAPTURE(resource);
						CAPTURE(markets);
						scalarResource(m, team, resource, swim, expected.data(), markets);
						m.seedMaterialGradient(team, resource, swim, actual.data(), markets);
						requireSameField(m, "resource", actual, expected);
						std::array<Uint16, Building::MAX_COUNT> supplierSeeds{};
						if (markets && m.marketsV2Enabled())
							for (const auto* b : world.game.teams[team]->stockSuppliers)
								if (m.isStockedMarketTile(b->gid, team, resource))
									supplierSeeds[Building::GIDtoID(b->gid)] = (GRADIENT_AT_GOAL - 5 * GRADIENT_STEP);
						m.seedMaterialGradientDirect(team, resource, swim, actual.data(),
							markets && m.marketsV2Enabled() ? supplierSeeds.data() : nullptr);
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
			c.resource.type = i % (8 + 1) == 8 ? NO_RES_TYPE : i % (8 + 1);
            c.resource.amount = c.resource.type==NO_RES_TYPE ? 0 : 1;
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
			for (int resource=0; resource<MaterialCount; ++resource)
			{
				CAPTURE(resource);
				scalarResource(m, team, resource, swim, expected.data(), false);
				m.seedMaterialGradient(team, resource, swim, actual.data(), false);
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
			cell.resource.type = terrainProperties(terrain).farmMaterial;
			REQUIRE(m.resourcePropertiesByIndex(cell.resource.type).clearable);
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
			m.setResourceByIndex(20, 20, phase % MaterialCount, 0);
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
				m.seedMaterialGradient(0, WHEAT, swim, actual.data(), false);
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
			m.setResourceByIndex(20 + resource, 20, resource, 0);
		std::vector<Uint16> warmup(m.size);
		for (int i = 0; i < 16; ++i) m.seedMaterialGradient(0, WHEAT, 0, warmup.data(), false);
		REQUIRE(m.gradientRuntime->resourceSeeds.valid);
		for (unsigned workers : {0, 1, 2})
		{
			CAPTURE(workers);
			// Use an unallocated field for every worker count.
			const int resource = workers;
			REQUIRE(m.materialGradients[0][resource][0] == nullptr);
			std::vector<Uint16> expected(m.size);
			scalarResource(m, 0, resource, 0, expected.data(), false);
			m.propagateGradient(expected.data(), 0);
			m.configureGradientPipeline(workers, 8);
			m.configureCompute(4, Map::ComputeAI);
			std::vector<Uint16 *> requests(8);
			m.computeExecutor().run(requests.size(), [&](size_t j) {
				requests[j] = m.getMaterialGradientSlot(0, resource, 0);
			});
			REQUIRE(requests[0] != nullptr);
			for (size_t j=1; j<requests.size(); ++j)
				REQUIRE(requests[j] == requests[0]);
			requireSameField(m, "lazy resource", std::vector<Uint16>(requests[0], requests[0] + m.size), expected);
		}
	}
}

TEST_SUITE("GradientPreparation")
{
TEST_CASE("compound passable material goals track depletion visibility and overlapping suppliers")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.wDec=7,.hDec=7,.teams=2,.clearImmobile=true,.header=true});
    auto& map=world.game.map;
    using Json=nlohmann::json;
    auto source=Json::parse(map.resourceRegistry().serialize())["resources"][1];
    source["key"]="mixed-visible";
    source["properties"]["blocksGround"]=false;
    source["properties"]["visibleToHarvest"]=true;
    source["properties"]["persistsWhenEmpty"]=true;
    source["yields"]["wood"]={{"capacity",5},{"initial",2},{"growthRate",196608},{"consumption","one"}};
    auto hidden=source;
    hidden["key"]="mixed-always";
    hidden["properties"]["visibleToHarvest"]=false;
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({source,hidden})}}.dump());
    const auto first=resourceIndex(*map.resourceRegistry().find("mixed-visible"));
    const auto second=resourceIndex(*map.resourceRegistry().find("mixed-always"));
    REQUIRE(map.incResourceByIndex(10,10,first,0));
    REQUIRE(map.incResourceByIndex(11,10,second,0));
    const auto a=map.coordToIndex(10,10), b=map.coordToIndex(11,10);
    map.fogOfWar[a]=map.fogOfWar[b]=0;
    std::vector<Uint16> direct(map.size),cached(map.size);
    auto compare=[&](int material) {
        map.seedMaterialGradientDirect(0,material,0,direct.data(),nullptr);
        for (int repeat=0;repeat<32;++repeat) map.seedMaterialGradient(0,material,0,cached.data(),false);
        REQUIRE(map.gradientRuntime->resourceSeeds.valid);
        requireSameField(map,"compound source",cached,direct);
    };
    compare(int(MaterialId::Food));
    CHECK(cached[a]==GRADIENT_UNREACHABLE);
    CHECK(cached[b]==GRADIENT_AT_GOAL);
    map.fogOfWar[a]=Team::teamNumberToMask(0);
    compare(int(MaterialId::Wood));
    CHECK(cached[a]==GRADIENT_AT_GOAL);
    map.setMaterialAmount(a,MaterialId::Wood,0);
    compare(int(MaterialId::Wood));
    CHECK(cached[a]==GRADIENT_UNREACHABLE);
    compare(int(MaterialId::Food));
    CHECK(cached[a]==GRADIENT_AT_GOAL);
    map.setMaterialAmount(a,MaterialId::Food,0);
    compare(int(MaterialId::Food));
    CHECK(cached[a]==GRADIENT_UNREACHABLE);
    REQUIRE(map.growResourceStock(a));
    compare(int(MaterialId::Food));
    CHECK(cached[a]==GRADIENT_AT_GOAL);
    // A passable natural source can overlap a building. Natural goals take
    // precedence over supplier prices in both kernels.
    const auto building=Building::GIDfrom(0,0);
    map.setBuilding(10,10,1,1,building);
    std::array<Uint16,Building::MAX_COUNT> supplierSeeds{};
    supplierSeeds[0]=GRADIENT_AT_GOAL-4*GRADIENT_STEP;
    map.seedMaterialGradientDirect(0,int(MaterialId::Food),0,direct.data(),supplierSeeds.data());
    REQUIRE(map.gradientRuntime->resourceSeeds.trySeed(map,0,int(MaterialId::Food),0,cached.data(),supplierSeeds.data()));
    requireSameField(map,"natural and building source overlap",cached,direct);
    CHECK(cached[a]==GRADIENT_AT_GOAL);
    map.addForbidden(10,10,0);
    REQUIRE(map.gradientRuntime->resourceSeeds.trySeed(map,0,int(MaterialId::Food),0,cached.data(),supplierSeeds.data()));
    CHECK(cached[a]==GRADIENT_FORBIDDEN);
}
}

TEST_SUITE("RuntimeResources")
{
TEST_CASE("unused catalog definitions allocate no material gradients and schedule no extra work")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame base({.wDec=7,.hDec=7,.header=true,.seed=7411});
    glob2test::HeadlessGame expanded({.wDec=7,.hDec=7,.header=true,.seed=7411});
    using Json=nlohmann::json;
    const auto prototype=Json::parse(base.game.map.resourceRegistry().serialize())["resources"][1];
    Json additions=Json::array();
    for (unsigned n=0;n<192;++n)
    {
        auto definition=prototype;
        definition["key"]="unused-"+std::to_string(1000+n);
        const auto material=MaterialKeys[8+n%4];
        definition["properties"]["primaryMaterial"]=material;
        definition["yields"]=Json{{material,{{"capacity",7},{"initial",1},{"growthRate",ResourceRateScale},{"consumption","one"}}}};
        additions.push_back(std::move(definition));
    }
    auto& original=base.game.map;
    auto& candidate=expanded.game.map;
    candidate.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",additions}}.dump());
    for (Map* map:{&original,&candidate})
    {
        REQUIRE(map->incResourceByIndex(8,8,WHEAT,0));
        map->configureGradientPipeline(1,3);
        for (unsigned material=0;material<MaterialCount;++material)
            for (int swim=0;swim<SWIM_CLASS_COUNT;++swim)
            {
                REQUIRE(map->getMaterialGradientSlot(0,material,swim)!=nullptr);
                CHECK((map->materialGradients[0][material][swim]!=nullptr)==(material==materialIndex(MaterialId::Food)));
            }
    }
    expanded.game.syncRandom=base.game.syncRandom;
    for (unsigned tick=0;tick<64;++tick)
    {
        { auto random=base.game.bindRandom(); base.game.syncStep(0); }
        { auto random=expanded.game.bindRandom(); expanded.game.syncStep(0); }
        CHECK(base.game.syncRandom==expanded.game.syncRandom);
        CHECK(original.gradientPipelineStatus().jobs==candidate.gradientPipelineStatus().jobs);
        CHECK(original.gradientPipelineStatus().pending==candidate.gradientPipelineStatus().pending);
    }
    original.finishGradientPipeline();
    candidate.finishGradientPipeline();
    CHECK(original.gradientPipelineStatus().jobs>0);
    CHECK(original.gradientPipelineStatus().published==candidate.gradientPipelineStatus().published);
    CHECK(original.gradientRuntime->resourceSeeds.allocatedBytes()==candidate.gradientRuntime->resourceSeeds.allocatedBytes());
    for (unsigned material=0;material<MaterialCount;++material)
        for (int swim=0;swim<SWIM_CLASS_COUNT;++swim)
        {
            const auto* left=original.materialGradients[0][material][swim];
            const auto* right=candidate.materialGradients[0][material][swim];
            CHECK((left==nullptr)==(right==nullptr));
            if (left && right) CHECK(std::equal(left,left+original.size,right));
        }
}
}

TEST_SUITE("RuntimeResources")
{
TEST_CASE("seed cache allocates only live material bitsets and reuses them after depletion")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.wDec=7,.hDec=7,.clearImmobile=true,.header=true});
    auto& map=fixture.game.map;
    using Json=nlohmann::json;
    auto definition=Json::parse(map.resourceRegistry().serialize())["resources"][1];
    definition["key"]="lazy-stock-cache";
    definition["properties"]["persistsWhenEmpty"]=true;
    definition["yields"]["gold"]={{"capacity",2},{"initial",1},{"consumption","one"}};
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",Json::array({definition})}}.dump());
    auto& cache=map.gradientRuntime->resourceSeeds;
    std::vector<Uint16> actual(map.size),expected(map.size);
    const auto verify=[&](unsigned material) {
        scalarResource(map,0,material,0,expected.data(),false);
        map.seedMaterialGradient(0,material,0,actual.data(),false);
        requireSameField(map,"lazy material availability",actual,expected);
    };
    for (unsigned pass=0;pass<32;++pass) verify(materialIndex(MaterialId::Food));
    REQUIRE(cache.valid);
    const auto emptyBytes=cache.allocatedBytes();
    // Querying every absent material must not instantiate any goal bitset.
    for (unsigned material=0;material<MaterialCount;++material) verify(material);
    CHECK(cache.allocatedBytes()==emptyBytes);
    const auto id=*map.resourceRegistry().find("lazy-stock-cache");
    REQUIRE(map.incResource(8,8,id,0));
    verify(materialIndex(MaterialId::Food));
    verify(materialIndex(MaterialId::Gold));
    const auto stockedBytes=cache.allocatedBytes();
    CHECK(stockedBytes==emptyBytes+2*((map.size+63)/64)*sizeof(Uint64));
    for (unsigned cycle=0;cycle<8;++cycle)
    {
        const auto index=map.coordToIndex(8,8);
        map.setMaterialAmount(index,MaterialId::Food,0);
        map.setMaterialAmount(index,MaterialId::Gold,0);
        verify(materialIndex(MaterialId::Food));
        verify(materialIndex(MaterialId::Gold));
        CHECK(actual[index]!=GRADIENT_AT_GOAL);
        map.setMaterialAmount(index,MaterialId::Food,1);
        map.setMaterialAmount(index,MaterialId::Gold,1);
        verify(materialIndex(MaterialId::Food));
        verify(materialIndex(MaterialId::Gold));
        CHECK(actual[index]==GRADIENT_AT_GOAL);
        CHECK(cache.allocatedBytes()==stockedBytes);
    }
}
}

TEST_CASE("compact clearing traits preserve custom high-ID property combinations" * doctest::test_suite("GradientPreparation"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.teams=2,.clearImmobile=true,.header=true});
    auto& map=world.game.map;
    using Json=nlohmann::json;
    const auto prototype=Json::parse(map.resourceRegistry().serialize())["resources"][1];
    Json definitions=Json::array();
    for(unsigned n=0;n<272;++n) {
        auto definition=prototype;
        definition["key"]="clearing-traits-"+std::to_string(1000+n);
        definition["properties"]["persistsWhenEmpty"]=true;
        definition["properties"]["blocksGround"]=bool(n&1);
        definition["properties"]["clearable"]=bool(n&2);
        definition["properties"]["farmable"]=bool(n&4);
        definitions.push_back(std::move(definition));
    }
    map.installResourceDefinitions(Json{{"schemaVersion",1},{"resources",definitions}}.dump());
    std::vector<Uint16> actual(map.size),expected(map.size);
    for(unsigned n=264;n<272;++n) {
        const auto id=*map.resourceRegistry().find("clearing-traits-"+std::to_string(1000+n));
        REQUIRE(resourceIndex(id)>255);
        const auto index=map.coordToIndex(8+n-264,8);
        map.setResource(8+n-264,8,id,0);
        // Empty persistent stocks remain clearable according to properties.
        map.tiles[index].resource.amount=0;
        for(bool farms:{false,true}) for(unsigned paint=0;paint<8;++paint) {
            CAPTURE(n);CAPTURE(farms);CAPTURE(paint);
            world.game.gameHeader.getExperiments().set(ExperimentId::FarmAreas,farms);
            auto& tile=map.tiles[index];
            tile.clearArea=paint&1 ? 1:0;
            tile.farmArea=paint&2 ? 1:0;
            tile.forbidden=paint&4 ? 1:0;
            tile.building=42;map.immobileUnits[index]=0;
            scalarClear(map,0,0,expected.data());
            map.seedClearAreasGradient(0,0,actual.data());
            CHECK(actual==expected);
            const bool goal=!(paint&4) && (n&2) && ((paint&1) || (farms && (paint&2) && !(n&4)));
            CHECK(actual[index]==(goal ? GRADIENT_AT_GOAL : GRADIENT_FORBIDDEN));
            tile.building=NOGBID;map.immobileUnits[index]=IMMOBILE_UNIT_NONE;
            tile.clearArea=tile.farmArea=tile.forbidden=0;
        }
    }
}
