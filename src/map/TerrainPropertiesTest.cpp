// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "TerrainLine.h"
#include "TerrainPresentation.h"
#include "BinaryStream.h"
#include "TextStream.h"
#include "FileManager.h"
#include "Utilities.h"
#include "StreamBackend.h"
#include "Version.h"
#include "gradient/GradientRuntime.h"
#include "MapInternal.h"
#include "gradient/BuildingGradientSearch.h"
#include "field/RuntimeTerrainGradient.h"
#include <memory>
#include <climits>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iterator>

TEST_SUITE("TerrainProperties")
{
TEST_CASE("exclusive movement modes and bounded compile-time definitions")
{
    for (const auto& properties : TERRAIN_PROPERTIES) CHECK(validTerrainProperties(properties));
    TerrainProperties invalid;
    invalid.walkable = invalid.swimmable = true;
    CHECK_FALSE(validTerrainProperties(invalid));
    invalid.swimmable = false;
    CHECK(validTerrainProperties(invalid));
    invalid.groundSpeedQ8 = 0;
    CHECK_FALSE(validTerrainProperties(invalid));
    CHECK_EQ(terrainProperties(ICE).groundSpeedQ8,128);
    CHECK_EQ(terrainProperties(TRAIL).groundSpeedQ8,512);
}

TEST_CASE("Trail retains legacy identities and terrain behavior")
{
    // Trail replaces Road's name and artwork, so old maps and scripts must
    // still resolve the same material and experiment without changing rules.
    CHECK_EQ(static_cast<unsigned>(TRAIL),4);
    CHECK_EQ(static_cast<unsigned>(ExperimentId::TrailTerrain),3);
    CHECK_EQ(parseExperimentKey("road-terrain"),ExperimentId::TrailTerrain);
    CHECK_EQ(terrainExperiment(TRAIL),ExperimentId::TrailTerrain);
    CHECK_EQ(std::string(terrainPresentation(TRAIL).name),"road");
    CHECK_EQ(std::string(terrainPresentation(TRAIL).label),"[road]");
    const auto& properties = terrainProperties(TRAIL);
    CHECK(properties.walkable);
    CHECK(properties.buildable);
    CHECK_FALSE(properties.swimmable);
    CHECK_FALSE(properties.resourcesGrow);
    CHECK_EQ(properties.allowedResources,0);
    CHECK_EQ(properties.groundSpeedQ8,512);
    CHECK_EQ(properties.groundHealthQ8,0);
    CHECK(properties.flyable);
    CHECK_EQ(properties.airSpeedQ8,256);
    CHECK_EQ(properties.airHealthQ8,0);
}

TEST_CASE("canonical terrain survives presentation regeneration and batches snapshot invalidation")
{
    glob2test::HeadlessGlobals globals;
    Map map;
    map.setSize(5,5,WATER);
    const auto old = map.frozenTerrainSnapshot();
    const auto generation = map.terrainGeneration();
    {
        auto batch = map.editTerrain();
        map.setCellTerrain(8,8,TRAIL);
        const auto partial=map.frozenTerrainSnapshot();
        map.resourceGrowthField();
        map.setCellTerrain(9,8,ICE);
        CHECK_EQ((*partial)[map.coordToIndex(9,8)],WATER);
        CHECK_EQ((*map.frozenTerrainSnapshot())[map.coordToIndex(9,8)],ICE);
        CHECK_FALSE(map.growthCache.validFor(map));
        map.resourceGrowthField();
    }
    CHECK_EQ(map.terrainGeneration(),generation+1);
    // The field already includes the final mutation; publishing the batch's
    // general terrain generation must not discard it again.
    CHECK(map.growthCache.validFor(map));
    CHECK_EQ((*old)[map.coordToIndex(8,8)],WATER);
    CHECK_EQ(map.terrainTypeAt(8,8),TRAIL);
    CHECK_EQ(map.terrainTypeAt(9,8),ICE);
    map.rebuildTerrain();
    CHECK_EQ(map.terrainTypeAt(8,8),TRAIL);
    CHECK_EQ(map.terrainTypeAt(9,8),ICE);
    CHECK_EQ(map.terrainTypeAt(7,8),WATER);
    CHECK(map.requiredTerrainExperiments().has(ExperimentId::IceTerrain));
    CHECK(map.requiredTerrainExperiments().has(ExperimentId::TrailTerrain));
    map.tile(2,1);
    CHECK_EQ(map.terrainTypeAt(40,8),TRAIL);
    CHECK_EQ(map.terrainTypeAt(41,8),ICE);
}

TEST_CASE("same-size map replacement and legacy import refresh ecology")
{
    glob2test::HeadlessGlobals globals;
    Map map;
    map.setSize(5,5,WATER);
    CHECK(map.resourceGrowthField().landField().at(8,8)==Fertility::kScale);
    map.setSize(5,5,GRASS);
    CHECK_FALSE(map.growthCache.validFor(map));
    CHECK(map.resourceGrowthField().landField().at(8,8)==0);
    auto tile=map.getTile(9,8);tile.terrain=256;map.replaceTile(9,8,tile);
    map.importLegacyTerrain();
    CHECK_FALSE(map.growthCache.validFor(map));
    CHECK(map.resourceGrowthField().landField().at(8,8)>0);
    map.tile(2,1);
    CHECK_FALSE(map.growthCache.validFor(map));
    const auto& field=map.resourceGrowthField().landField();
    CHECK(field.at(8,8)==field.at(40,8));
}

TEST_CASE("terrain edits refresh escape costs and supersede queued route snapshots")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto& map = world.game.map;
    for (int y=6;y<11;++y) for (int x=6;x<11;++x) map.addForbidden(x,y,0);
    auto* escape = map.getForbiddenGradient(0,0);
    const auto old = escape[map.coordToIndex(8,8)];
    {
        auto batch = map.editTerrain();
        for (int x=8;x<12;++x) map.setCellTerrain(x,8,TRAIL);
    }
    CHECK(escape[map.coordToIndex(8,8)] > old);
    map.setResource(15,15,WHEAT,1);
    map.getResourceGradient(0,WHEAT,0);
    map.configureGradientPipeline(0,2);
    map.advanceGradientPipeline();
    auto& pipeline = map.gradientRuntime->pipeline;
    pipeline.submit(&map.resourcesGradient[0][WHEAT][0],0,[&](auto& job) {
        map.seedResourcesGradient(0,WHEAT,0,job.data.get());
        job.modifiedCosts = true;
        job.terrain = map.frozenTerrainSnapshot();
    });
    map.setCellTerrain(9,8,ICE);
    map.advanceGradientPipeline();
    map.advanceGradientPipeline();
    CHECK_EQ(pipeline.metrics.discarded,1);
    CHECK_EQ(pipeline.metrics.published,0);
}

TEST_CASE("required terrain experiments survive tiling and distinguish map headers")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.header=true});
    auto& map = world.game.map;
    map.setCellTerrain(8,8,ICE);
    map.tile(2,1);
    CHECK(world.game.mapHeader.requiredTerrainExperiments.has(ExperimentId::IceTerrain));
    MapHeader plain, required;
    required.requiredTerrainExperiments.set(ExperimentId::TrailTerrain);
    CHECK(plain != required);
    CHECK_FALSE(plain == required);
    CHECK(plain.checkSum() != required.checkSum());
    auto* bytes = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(bytes);
    required.save(&output);
    output.flush();
    std::string corrupt(bytes->getBuffer(),bytes->getPosition());
    // The pointer/length MemoryStreamBackend constructor leaves its cursor at
    // the end after copying. The owning-string constructor starts at byte zero.
    GAGCore::BinaryInputStream validInput(new GAGCore::MemoryStreamBackend(std::string(corrupt)));
    MapHeader decoded;
    REQUIRE(decoded.load(&validInput));
    CHECK(decoded == required);
    auto key = corrupt.find("road-terrain");
    REQUIRE(key != std::string::npos);
    corrupt.replace(key,4,"xxxx");
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(std::move(corrupt)));
    MapHeader loaded;
    CHECK_FALSE(loaded.load(&input));
    CHECK(loaded.requiredTerrainExperiments.empty());
}

TEST_CASE("ice exposure includes stationary units and preserves fractional health in saves")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto& map = world.game.map;
    map.setCellTerrain(8,8,ICE);
    Unit* unit = world.addUnit(WORKER,8,8);
    REQUIRE(unit);
    unit->hp = unit->performance[HP];
    const int health = unit->hp;
    for (int i=0;i<17;++i) unit->applyTerrainHealth();
    CHECK_EQ(unit->hp,health);
    CHECK_EQ(unit->terrainHealthRemainder,-136);

    auto* bytes = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(bytes);
    world.game.save(&output,false,"terrain-continuation");
    auto* copy = new GAGCore::MemoryStreamBackend(*bytes);
    copy->seekFromStart(0);
    GAGCore::BinaryInputStream input(copy);
    GameGUI resumed;
    // Loading into a previously queried same-size map must discard its ready
    // ecology cache, even though dimensions alone would still match.
    resumed.game.map.setSize(map.getShiftW(),map.getShiftH(),WATER);
    CHECK(resumed.game.map.resourceGrowthField().landField().at(8,8)==Fertility::kScale);
    REQUIRE(resumed.game.load(&input));
    Fertility::GrowthCache freshEcology;
    freshEcology.rebuild(resumed.game.map);
    CHECK(resumed.game.map.resourceGrowthField().landField().values()==freshEcology.landField().values());
    CHECK(resumed.game.map.resourceGrowthField().aquaticField()==freshEcology.aquaticField());
    Unit* loaded = resumed.game.teams[0]->myUnits[Unit::GIDtoID(unit->gid)];
    REQUIRE(loaded);
    CHECK_EQ(loaded->terrainHealthRemainder,unit->terrainHealthRemainder);
    CHECK_EQ(resumed.game.map.terrainTypeAt(8,8),ICE);
    CHECK(resumed.game.gameHeader.hasExperiment(ExperimentId::IceTerrain));
    for (int i=0;i<15;++i) { unit->applyTerrainHealth(); loaded->applyTerrainHealth(); }
    CHECK_EQ(unit->hp,health-1);
    CHECK_EQ(loaded->hp,unit->hp);
    CHECK_EQ(unit->terrainHealthRemainder,0);
    unit->insideTimeout=-1;
    for (int i=0;i<32;++i) unit->applyTerrainHealth();
    CHECK_EQ(unit->hp,health-1);
}

TEST_CASE("explorers ignore ice damage and no-permadeath applies to ground hazards")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    world.game.map.setCellTerrain(8,8,ICE);
    Unit* explorer=world.addUnit(EXPLORER,8,8);
    REQUIRE(explorer);
    const int hp=explorer->hp;
    for(int i=0;i<64;++i) explorer->applyTerrainHealth();
    CHECK_EQ(explorer->hp,hp);
    Unit* worker=world.addUnit(WORKER,8,8);
    REQUIRE(worker);
    worker->hp=1;
    world.game.gameHeader.setPermadeathDisabled(true);
    for(int i=0;i<96;++i) worker->applyTerrainHealth();
    CHECK_FALSE(worker->isDead);
    CHECK_GE(worker->hp,1);
}

TEST_CASE("health modifiers clip healing and expose units that have found an exit")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    Unit* unit=world.addUnit(WORKER,8,8);
    REQUIRE(unit);
    const int maximum=unit->performance[HP];
    unit->hp=maximum;
    for (int i=0;i<31;++i) unit->applyTerrainHealthRate(8);
    CHECK_EQ(unit->terrainHealthRemainder,0);
    unit->hp=maximum-2;
    for (int i=0;i<32;++i) unit->applyTerrainHealthRate(8);
    CHECK_EQ(unit->hp,maximum-1);
    for (int i=0;i<32;++i) unit->applyTerrainHealthRate(12);
    CHECK_EQ(unit->hp,maximum);
    CHECK_EQ(unit->terrainHealthRemainder,0);
    world.game.map.setCellTerrain(8,8,ICE);
    unit->displacement=Unit::DIS_EXITING_BUILDING;
    unit->attachedBuilding=nullptr;
    unit->insideTimeout=0;
    for (int i=0;i<32;++i) unit->applyTerrainHealth();
    CHECK_EQ(unit->hp,maximum-1);
}

TEST_CASE("terrain health profiles choose independent air and ground rates")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    Unit* worker=world.addUnit(WORKER,8,8);
    Unit* explorer=world.addUnit(EXPLORER,8,8);
    REQUIRE(worker); REQUIRE(explorer);
    auto terrain=terrainProperties(GRASS);
    terrain.groundHealthQ8=-16;
    terrain.airHealthQ8=32;
    worker->hp=worker->performance[HP]-4;
    explorer->hp=explorer->performance[HP]-4;
    const int ground=worker->hp,air=explorer->hp;
    for(int tick=0;tick<16;++tick) { worker->applyTerrainHealth(terrain); explorer->applyTerrainHealth(terrain); }
    CHECK_EQ(worker->hp,ground-1);
    CHECK_EQ(explorer->hp,air+2);
    terrain.groundHealthQ8=32;
    terrain.airHealthQ8=-16;
    for(int tick=0;tick<16;++tick) { worker->applyTerrainHealth(terrain); explorer->applyTerrainHealth(terrain); }
    CHECK_EQ(worker->hp,ground+1);
    CHECK_EQ(explorer->hp,air+1);
}

TEST_CASE("explorer flag eligibility applies route accessibility and hunger distance")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    Unit* explorer=world.addUnit(EXPLORER,8,8);
    Building* flag=world.addBuilding("explorationflag",12,12);
    REQUIRE(explorer); REQUIRE(flag);
    explorer->activity=Unit::ACT_RANDOM;
    explorer->medical=Unit::MED_FREE;
    explorer->hungry=explorer->trigHungry+10*explorer->race->hungriness;
    int distance=0;
    CHECK_FALSE(flag->considerUnitForExplorerFlag(explorer,&distance,INT_MAX));
    CHECK_FALSE(flag->considerUnitForExplorerFlag(explorer,&distance,11));
    CHECK(flag->considerUnitForExplorerFlag(explorer,&distance,10));
    CHECK_EQ(distance,100);
}

TEST_CASE("projectile supercover tests intermediate cells corners and wrapped coordinates")
{
    const auto obstacle=[](std::int64_t x,std::int64_t y){return (x&15)==2 && (y&15)==1;};
    CHECK_FALSE(terrainSegmentClear(16,48,144,48,obstacle));
    CHECK(terrainSegmentClear(16,80,144,80,obstacle));
    const auto corner=[](std::int64_t x,std::int64_t y){return x==1&&y==0;};
    CHECK_FALSE(terrainSegmentClear(16,16,80,80,corner));
    const auto seam=[](std::int64_t x,std::int64_t y){return (x&15)==15&&(y&15)==1;};
    CHECK_FALSE(terrainSegmentClear(16,48,-48,48,seam));
    CHECK(terrainSegmentClear(16,48,16,48,seam));
    CHECK_FALSE(terrainSegmentClear(32,16,32,112,[](auto x,auto y){return x==0&&y==2;}));
    CHECK_FALSE(terrainSegmentClear(16,32,112,32,[](auto x,auto y){return x==2&&y==0;}));
}
}

TEST_SUITE("TerrainRuntime")
{
	namespace
	{
	const char *runtimeDefinitions = R"({"schemaVersion":1,"terrains":[
 {"key":"test:bog","name":"Bog","base":"water","properties":{"groundSpeedQ8":64},"appearance":"sand"},
 {"key":"test:hazard","name":"Hazard","base":"grass","properties":{"groundSpeedQ8":192,"buildable":false,"flyable":false,"projectileBlocks":true,"groundHealthQ8":-16,"growthQ8":512,"fertilitySource":true,"fertilityQ8":512},"appearance":"ice"},
 {"key":"test:unused","name":"Unused","base":"grass","properties":{"groundSpeedQ8":1024,"airSpeedQ8":1024},"appearance":"grass"}
]})";
	void importBeforeMatch(Game &game, std::string_view definitions = runtimeDefinitions)
	{
		// A detached map is the pre-match authoring API; active games cannot replace definitions.
		game.map.game = nullptr;
		game.map.importTerrainDefinitions(definitions);
		game.map.setGame(&game);
	}
	} // namespace
	TEST_CASE("maps isolate registries and reject failed imports atomically")
	{
		glob2test::HeadlessGlobals globals;
		Map first, second;
		first.setSize(5, 5, GRASS);
		second.setSize(5, 5, GRASS);
		first.importTerrainDefinitions(runtimeDefinitions);
		CHECK(second.terrainRegistry().size() == TERRAIN_COUNT);
		const auto previous = first.frozenTerrainRegistry();
		const auto generation = first.terrainGeneration();
		CHECK_THROWS(
			first.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"bad"}]})"));
		CHECK(first.frozenTerrainRegistry() == previous);
		CHECK(first.terrainGeneration() == generation);
		CHECK(first.terrainQueueBuckets() == 64);
		CHECK(first.minStepCost(6) == second.minStepCost(6));
		CHECK_FALSE(first.hasTerrainHealthEffects());
		CHECK_FALSE(first.hasAirTerrainConstraints());
		const auto bog = *first.terrainRegistry().find("test:bog");
		first.setCellTerrain(3, 3, bog);
		CHECK(first.terrainQueueBuckets() == 256);
		CHECK(first.hasTerrainMovementModifiers());
		CHECK_FALSE(first.isFreeForGroundUnit(3, 3, false, 1));
		CHECK(first.isFreeForGroundUnit(3, 3, true, 1));
		first.setCellTerrain(3, 3, GRASS);
		CHECK(first.terrainQueueBuckets() == 64);
		CHECK_FALSE(first.hasTerrainMovementModifiers());
		first.importTerrainDefinitions(
			R"({"schemaVersion":1,"terrains":[{"key":"test:bog","name":"Water equivalent","base":"water","properties":{},"appearance":"sand"}]})");
		first.setCellTerrain(3, 3, bog);
		second.setCellTerrain(3, 3, WATER);
		CHECK_FALSE(first.hasTerrainMovementModifiers());
		CHECK((*first.frozenWaterSnapshot())[first.coordToIndex(3, 3)] == 1);
		std::vector<Uint16> expected(1024, 1), actual;
		expected[first.coordToIndex(3, 3)] = GRADIENT_AT_GOAL;
		actual = expected;
		first.propagateGradient(actual.data(), 6);
		second.propagateGradient(expected.data(), 6);
		CHECK(actual == expected);
		CHECK(first.resourceGrowthField().landField().values() ==
			  second.resourceGrowthField().landField().values());
		CHECK(first.resourceGrowthField().aquaticField() ==
			  second.resourceGrowthField().aquaticField());
	}
	TEST_CASE(
		"custom capabilities drive map queries health ecology and immutable match definitions")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		importBeforeMatch(world.game);
		auto &map = world.game.map;
		CHECK_THROWS(map.importTerrainDefinitions(runtimeDefinitions));
		const auto hazard = *map.terrainRegistry().find("test:hazard");
		map.setCellTerrain(8, 8, hazard);
		CHECK_FALSE(map.isFreeForBuilding(8, 8));
		CHECK(map.hasTerrainHealthEffects());
		CHECK(map.hasAirTerrainConstraints());
		CHECK(map.hasProjectileBlockingTerrain());
		CHECK_FALSE(map.projectilePathClear(7 * 32 + 16, 8 * 32 + 16, 9 * 32 + 16, 8 * 32 + 16));
		CHECK(map.terrainPropertiesAt(8, 8).resourcesGrow);
		CHECK(map.terrainPropertiesAt(8, 8).growthQ8 == 512);
		CHECK(map.terrainPropertiesAt(8, 8).fertilityQ8 == 512);
		map.resourceGrowthField();
		CHECK(map.growthCache.validFor(map));
		CHECK(map.growthCache.landField().at(8, 8) > 0);
		CHECK(map.growthCache.rate(map.coordToIndex(8, 8), WHEAT) > 0);
		CHECK(map.stepCost(1, 0, map.coordToIndex(8, 8), 0) == 13);
		CHECK(map.stepCost(1, 1, map.coordToIndex(8, 8), 0) == 18);
		auto *worker = world.addUnit(WORKER, 8, 8);
		REQUIRE(worker);
		const int hp = worker->hp;
		for (int i = 0; i < 16; ++i)
			worker->applyTerrainHealth();
		CHECK(worker->hp == hp - 1);
		{
			auto batch = map.editTerrain();
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x)
					map.setCellTerrain(x, y, hazard);
		}
		map.setCellTerrain(2, 2, GRASS);
		map.setCellTerrain(12, 12, GRASS);
		int dx = 0, dy = 0;
		CHECK_FALSE(map.pathfindAirPointToPoint(2, 2, 12, 12, &dx, &dy));
	}
	TEST_CASE("eager resumed and worker gradients agree on custom slow swimming")
	{
		glob2test::HeadlessGlobals globals;
		Map map;
		map.setSize(5, 5, GRASS);
		map.importTerrainDefinitions(runtimeDefinitions);
		const auto bog = *map.terrainRegistry().find("test:bog");
		{
			auto batch = map.editTerrain();
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x)
					if ((x * 3 + y * 7) % 5 == 0)
						map.setCellTerrain(x, y, bog);
		}
		for (int swim = 0; swim < 7; ++swim)
		{
			std::vector<Uint16> eager(1024, 1), lazy, pipelined;
			eager[0] = GRADIENT_AT_GOAL;
			lazy = eager;
			pipelined = eager;
			map.propagateGradient(eager.data(), swim);
			BuildingGradientSearch search;
			search.begin(map, lazy.data(), swim);
			for (unsigned cell : {97, 501, 1023})
			{
				search.resolve(cell);
				CHECK(lazy[cell] == eager[cell]);
			}
			search.finish();
			CHECK(lazy == eager);
			GradientPipeline pipeline;
			pipeline.configure(1, 2, 1024,
							   [](auto &job, auto &scratch)
							   {
								   gradient_kernel::propagateTerrainField(
									   job.data.get(), job.swim, gradient_kernel::COST_LIMIT,
									   {32, 32}, scratch,
									   [&](size_t i) { return (*job.terrain)[i]; },
									   job.modifiedCosts, *job.registry, job.terrainBuckets);
							   });
			auto owned = std::make_unique<Uint16[]>(1024);
			std::copy(pipelined.begin(), pipelined.end(), owned.get());
			auto *field = owned.release();
			pipeline.advance();
			pipeline.submit(&field, swim,
							[&](auto &job)
							{
								std::copy(pipelined.begin(), pipelined.end(), job.data.get());
								job.registry = map.frozenTerrainRegistry();
								job.terrain = map.frozenTerrainSnapshot();
								job.terrainBuckets = map.terrainQueueBuckets();
								job.modifiedCosts = true;
							});
			pipeline.advance();
			pipeline.advance();
			CHECK(std::equal(eager.begin(), eager.end(), field));
			delete[] field;
		}
	}
	TEST_CASE("production pipeline captures custom movement and discards fields after reimport")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned workers : {0, 1})
			for (unsigned speed : {256, 64})
			{
				CAPTURE(workers);
				CAPTURE(speed);
				glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
				auto &map = world.game.map;
				world.game.gameHeader.setResourceGrowthDisabled(true);
				auto definitions = [](unsigned factor)
				{
					return nlohmann::json{
						{"schemaVersion", 1},
						{"terrains",
						 nlohmann::json::array({{{"key", "test:water"},
												 {"name", "Custom water"},
												 {"base", "water"},
												 {"appearance", "sand"},
												 {"properties", {{"groundSpeedQ8", factor}}}}})}}
						.dump();
				};
				importBeforeMatch(world.game, definitions(speed));
				const auto water = *map.terrainRegistry().find("test:water");
				{
					auto batch = map.editTerrain();
					for (int y = 0; y < 32; ++y)
						for (int x = 8; x < 13; ++x)
							map.setCellTerrain(x, y, water);
				}
				CHECK(map.hasTerrainMovementModifiers() == (speed != 256));
				map.setResource(20, 20, WHEAT, 1);
				map.getResourceGradient(0, WHEAT, 6);
				std::vector<Uint16> expected(1024);
				map.seedResourcesGradient(0, WHEAT, 6, expected.data());
				map.propagateGradient(expected.data(), 6);

				// Exercise Map's actual dispatch and worker callback: neutral-speed
				// custom water captures a binary plane, while slow water captures
				// compact movement profiles and selects the larger queue.
				map.configureGradientPipeline(workers, 2);
				map.advanceGradientPipeline();
				map.syncStep(0);
				REQUIRE(map.gradientRuntime->pipeline.pendingCount() == 1);
				map.gradientRuntime->pipeline.visitPendingSnapshots(
					[&](const auto &pending)
					{
						CHECK_FALSE(pending.superseded);
						CHECK(std::equal(expected.begin(), expected.end(), pending.data));
					});

				importBeforeMatch(world.game, definitions(speed == 256 ? 64 : 256));
				CHECK(map.terrainRegistry().find("test:water") == water);
				map.gradientRuntime->pipeline.visitPendingSnapshots([&](const auto &pending)
																	{ CHECK(pending.superseded); });
				std::vector<Uint16> replacement(1024);
				map.seedResourcesGradient(0, WHEAT, 6, replacement.data());
				map.propagateGradient(replacement.data(), 6);
				REQUIRE(replacement != expected);
				map.updateResourcesGradient(0, WHEAT, 6);
				map.advanceGradientPipeline();
				map.advanceGradientPipeline();
				CHECK(map.gradientRuntime->pipeline.metrics.discarded == 1);
				CHECK(map.gradientRuntime->pipeline.pendingCount() == 0);
				CHECK(std::equal(replacement.begin(), replacement.end(),
								 map.resourcesGradient[0][WHEAT][6]));
			}
	}
	TEST_CASE("unused distinct costs do not expand map movement setup")
	{
		glob2test::HeadlessGlobals globals;
		Map map;
		map.setSize(5, 5, GRASS);
		map.setCellTerrain(8, 8, ICE);
		std::vector<Uint16> expected(1024, 1);
		expected[0] = GRADIENT_AT_GOAL;
		map.propagateGradient(expected.data(), 6);
		nlohmann::json definitions = nlohmann::json::array();
		for (unsigned speed = 64; speed <= 1024; ++speed)
			definitions.push_back({{"key", "unused:s" + std::to_string(speed)},
								   {"name", "Unused"},
								   {"base", "water"},
								   {"appearance", "water"},
								   {"properties", {{"groundSpeedQ8", speed}}}});
		map.importTerrainDefinitions(
			nlohmann::json{{"schemaVersion", 1}, {"terrains", definitions}}.dump());
		REQUIRE(map.terrainRegistry().movement(6).profiles.size() > 50);
		const auto snapshot = map.frozenTerrainMovementSnapshot(6);
		CHECK(snapshot->movement.profiles.size() == 2);
		CHECK(snapshot->movement.steps.size() == 4);
		CHECK(map.terrainQueueBuckets() == 64);
		std::vector<Uint16> actual(1024, 1);
		actual[0] = GRADIENT_AT_GOAL;
		map.propagateGradient(actual.data(), 6);
		CHECK(actual == expected);
		BuildingGradientSearch search;
		actual.assign(1024, 1);
		actual[0] = GRADIENT_AT_GOAL;
		search.begin(map, actual.data(), 6);
		search.finish();
		CHECK(actual == expected);
	}
	TEST_CASE("large embedded registry crosses the stream string limit")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		using Json = nlohmann::json;
		Json definitions = Json::array();
		for (unsigned i = 0; i < 2048; ++i)
			definitions.push_back({{"key", "large:t" + std::to_string(i)},
								   {"name", "Large registry"},
								   {"base", "grass"},
								   {"appearance", "sand"},
								   {"properties", Json::object()}});
		world.game.map.game = nullptr;
		world.game.map.importTerrainDefinitions(
			Json{{"schemaVersion", 1}, {"terrains", definitions}}.dump());
		world.game.map.setGame(&world.game);
		REQUIRE(world.game.map.terrainRegistry().serialize().size() > 1024 * 1024);
		world.game.map.setCellTerrain(8, 8, TerrainType(2000));
		auto *bytes = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(bytes);
		world.game.save(&output, false, "large-registry");
		output.flush();
		GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(
			std::string(bytes->getBuffer(), bytes->getPosition())));
		GameGUI restored;
		REQUIRE(restored.game.load(&input));
		CHECK(restored.game.map.terrainRegistry().digest() ==
			  world.game.map.terrainRegistry().digest());
		CHECK(restored.game.map.terrainTypeAt(8, 8) == TerrainType(2000));
		auto *textBytes = new GAGCore::MemoryStreamBackend;
		GAGCore::TextOutputStream textOutput(textBytes);
		world.game.save(&textOutput, false, "large-registry-text");
		textOutput.flush();
		GAGCore::TextInputStream textInput(new GAGCore::MemoryStreamBackend(
			std::string(textBytes->getBuffer(), textBytes->getPosition())));
		GameGUI restoredText;
		REQUIRE(restoredText.game.load(&textInput));
		CHECK(restoredText.game.map.terrainRegistry().digest() ==
			  world.game.map.terrainRegistry().digest());
	}
	TEST_CASE("embedded registry restores custom state and pending gradients without source files")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		importBeforeMatch(world.game);
		auto &map = world.game.map;
		map.setCellTerrain(8, 8, *map.terrainRegistry().find("test:hazard"));
		map.setCellTerrain(9, 8, *map.terrainRegistry().find("test:bog"));
		auto *worker = world.addUnit(WORKER, 8, 8);
		REQUIRE(worker);
		for (int i = 0; i < 7; ++i)
			worker->applyTerrainHealth();
		map.setResource(15, 15, WHEAT, 1);
		map.getResourceGradient(0, WHEAT, 6);
		map.configureGradientPipeline(1, 2);
		map.advanceGradientPipeline();
		map.gradientRuntime->pipeline.submit(&map.resourcesGradient[0][WHEAT][6], 6,
											 [&](auto &job)
											 {
												 map.seedResourcesGradient(0, WHEAT, 6,
																		   job.data.get());
												 job.modifiedCosts = true;
												 job.registry = map.frozenTerrainRegistry();
												 job.terrain = map.frozenTerrainSnapshot();
												 job.terrainBuckets = map.terrainQueueBuckets();
											 });
		auto *bytes = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(bytes);
		world.game.save(&output, false, "custom-continuation");
		output.flush();
		GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(
			std::string(bytes->getBuffer(), bytes->getPosition())));
		GameGUI resumed;
		REQUIRE(resumed.game.load(&input));
		CHECK(resumed.game.map.terrainRegistry().digest() == map.terrainRegistry().digest());
		CHECK(resumed.game.map.terrainQueueBuckets() == 256);
		CHECK(resumed.game.map.gradientRuntime->pipeline.pendingCount() == 1);
		CHECK(resumed.game.map.terrainTypeAt(8, 8) == map.terrainTypeAt(8, 8));
		auto *loaded = resumed.game.teams[0]->myUnits[Unit::GIDtoID(worker->gid)];
		REQUIRE(loaded);
		CHECK(loaded->terrainHealthRemainder == worker->terrainHealthRemainder);
		for (int i = 0; i < 2; ++i)
		{
			map.advanceGradientPipeline();
			resumed.game.map.advanceGradientPipeline();
		}
		CHECK(std::equal(map.resourcesGradient[0][WHEAT][6],
						 map.resourcesGradient[0][WHEAT][6] + 1024,
						 resumed.game.map.resourcesGradient[0][WHEAT][6]));
		for (int i = 0; i < 100; ++i)
		{
			world.game.syncStep(0);
			resumed.game.syncStep(0);
			CHECK(world.game.checkSum() == resumed.game.checkSum());
		}
	}
	TEST_CASE("format-137 saves with custom terrain load with remapped IDs and resave at the current format")
	{
		glob2test::HeadlessGlobals globals;
		// Written by an origin/master build (TERRAIN_COUNT 7) with two runtime
		// definitions painted at IDs 7 and 8 beside classic ice, trail, sand and water.
		auto open = []
		{
			return globalContainer->fileManager->openInflatingInputStreamBackend(
				"test/fixtures/terrain-catalogue/custom-registry-137.game.gz");
		};
		auto verify = [](const Game &game)
		{
			const auto &map = game.map;
			REQUIRE(map.terrainRegistry().size() == TERRAIN_COUNT + 2);
			const auto bog = map.terrainRegistry().find("fixture:bog");
			const auto mud = map.terrainRegistry().find("fixture:mud");
			REQUIRE(bog);
			REQUIRE(mud);
			CHECK(*bog == TerrainType(TERRAIN_COUNT));
			CHECK(*mud == TerrainType(TERRAIN_COUNT + 1));
			CHECK(map.terrainRegistry().appearance(*bog) == WATER);
			CHECK(map.terrainRegistry().appearance(*mud) == SAND);
			CHECK(map.terrainProperties(*bog).swimmable);
			CHECK(map.terrainProperties(*bog).groundSpeedQ8 == 128);
			CHECK(map.terrainProperties(*mud).groundSpeedQ8 == 192);
			CHECK_FALSE(map.terrainProperties(*mud).buildable);
			CHECK(map.terrainTypeAt(4, 4) == *bog);
			CHECK(map.terrainTypeAt(5, 4) == *bog);
			CHECK(map.terrainTypeAt(6, 6) == *mud);
			CHECK(map.terrainTypeAt(10, 10) == ICE);
			CHECK(map.terrainTypeAt(12, 10) == TRAIL);
			CHECK(map.terrainTypeAt(14, 14) == SAND);
			CHECK(map.terrainTypeAt(16, 14) == WATER);
			CHECK(map.terrainTypeAt(0, 0) == GRASS);
			// Saved frames kept their appearance ranges, so the load-time frame check held.
			CHECK(map.getTerrain(4, 4) >= 256);
			CHECK(map.getTerrain(4, 4) < 272);
			CHECK(map.getTerrain(6, 6) >= 128);
			CHECK(map.getTerrain(6, 6) < 144);
		};
		GameGUI loaded;
		{
			auto *backend = open();
			REQUIRE(backend);
			GAGCore::BinaryInputStream input(backend);
			REQUIRE(loaded.game.load(&input));
		}
		verify(loaded.game);
		const auto digest = loaded.game.map.terrainRegistry().digest();
		CHECK_FALSE(digest.empty());

		auto *resaved = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(resaved);
		loaded.game.save(&output, false, "custom-registry-138");
		output.flush();
		GameGUI reloaded;
		{
			GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(
				std::string(resaved->getBuffer(), resaved->getPosition())));
			REQUIRE(reloaded.game.load(&input));
		}
		verify(reloaded.game);
		CHECK(reloaded.game.map.terrainRegistry().digest() == digest);
		CHECK(reloaded.game.map.checkSum(true) == loaded.game.map.checkSum(true));
		// Current files number their custom definitions from the current built-ins.
		CHECK(nlohmann::json::parse(reloaded.game.map.terrainRegistry().serialize())["terrains"][0]["id"] == TERRAIN_COUNT);
	}
	TEST_CASE("painted catalogue terrain survives save and reload with its frames and checksum")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		auto &map = world.game.map;
		std::vector<TerrainType> painted;
		for (unsigned i = TERRAIN_COUNT_BEFORE_CATALOGUE; i < TERRAIN_COUNT; ++i)
			if (terrainPaintable(TerrainType(i)))
				painted.push_back(TerrainType(i));
		REQUIRE(painted.size() == 24);
		for (std::size_t n = 0; n < painted.size(); ++n)
		{
			map.setCellTerrain(int(n % 8) * 2 + 2, int(n / 8) * 3 + 2, painted[n]);
			// Saved frames come from the type's own contract, never a neighbour's.
			const auto &frames = terrainCompatibility(painted[n]);
			CHECK(map.getTerrain(int(n % 8) * 2 + 2, int(n / 8) * 3 + 2) >= frames.firstFrame);
			CHECK(map.getTerrain(int(n % 8) * 2 + 2, int(n / 8) * 3 + 2) < frames.firstFrame + frames.variants);
		}
		map.setCellTerrain(20, 20, ICE);
		map.setCellTerrain(21, 20, TRAIL);
		const auto required = map.requiredTerrainExperiments();
		CHECK(required.size() == 11); // nine catalogue groups plus ice and trail
		for (auto text : {false, true})
		{
			CAPTURE(text);
			auto *bytes = new GAGCore::MemoryStreamBackend;
			std::unique_ptr<GAGCore::OutputStream> output(text
				? static_cast<GAGCore::OutputStream *>(new GAGCore::TextOutputStream(bytes))
				: static_cast<GAGCore::OutputStream *>(new GAGCore::BinaryOutputStream(bytes)));
			world.game.save(output.get(), false, "catalogue");
			output->flush();
			auto *storage = new GAGCore::MemoryStreamBackend(std::string(bytes->getBuffer(), bytes->getPosition()));
			std::unique_ptr<GAGCore::InputStream> input(text
				? static_cast<GAGCore::InputStream *>(new GAGCore::TextInputStream(storage))
				: static_cast<GAGCore::InputStream *>(new GAGCore::BinaryInputStream(storage)));
			GameGUI loaded;
			REQUIRE(loaded.game.load(input.get()));
			for (std::size_t n = 0; n < painted.size(); ++n)
				CHECK(loaded.game.map.terrainTypeAt(int(n % 8) * 2 + 2, int(n / 8) * 3 + 2) == painted[n]);
			CHECK(loaded.game.map.terrainTypeAt(20, 20) == ICE);
			CHECK(loaded.game.map.terrainTypeAt(21, 20) == TRAIL);
			CHECK(loaded.game.map.checkSum(true) == map.checkSum(true));
			CHECK(loaded.game.map.requiredTerrainExperiments() == required);
			CHECK(loaded.game.mapHeader.requiredTerrainExperiments == required);
		}
	}
	TEST_CASE("each catalogue type requires exactly its group's experiment and shares the group profile")
	{
		glob2test::HeadlessGlobals globals;
		for (unsigned i = 0; i < TERRAIN_COUNT; ++i)
		{
			const auto type = TerrainType(i);
			CAPTURE(TerrainPresentations[i].name);
			if (!terrainPaintable(type))
				continue;
			Map map;
			map.setSize(5, 5, GRASS);
			map.setCellTerrain(2, 2, type);
			const auto required = map.requiredTerrainExperiments();
			const auto expected = terrainExperiment(type);
			CHECK(required.size() == (expected ? 1u : 0u));
			if (expected)
				CHECK(required.has(*expected));
			// Trail keeps its historical switch; every other member takes the group's.
			if (type == TRAIL)
				CHECK(expected == ExperimentId::TrailTerrain);
			else
				CHECK(expected == terrainGroupExperiment(terrainGroup(type)));
			CHECK(sameTerrainProperties(map.terrainProperties(type),
										terrainGroupDefinition(terrainGroup(type)).properties));
		}
		// Runtime definitions: a gated group's exact profile declares the group's
		// experiment, a profile of its own declares nothing.
		{
			Map custom;
			custom.setSize(5, 5, GRASS);
			custom.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[
				{"key":"mod:pit","name":"Pit","base":"void_hole","properties":{},"appearance":"void_hole"},
				{"key":"mod:bog","name":"Bog","base":"water","properties":{"groundSpeedQ8":128},"appearance":"water"}]})");
			const auto pit = *custom.terrainRegistry().find("mod:pit");
			const auto bog = *custom.terrainRegistry().find("mod:bog");
			custom.setCellTerrain(1, 1, pit);
			CHECK(custom.requiredTerrainExperiments().has(ExperimentId::VoidTerrain));
			CHECK(custom.requiredTerrainExperiments().size() == 1);
			custom.setCellTerrain(1, 1, GRASS);
			custom.setCellTerrain(2, 2, bog);
			CHECK(custom.requiredTerrainExperiments().empty());
		}
		// Classic ground never acquires a requirement.
		Map plain;
		plain.setSize(5, 5, GRASS);
		plain.setCellTerrain(1, 1, SAND);
		plain.setUMTerrain(3, 3, WATER);
		plain.regenerateMap(0, 0, 5, 5);
		CHECK(plain.requiredTerrainExperiments().empty());
		// Group mechanics the catalogue promises.
		CHECK_FALSE(terrainProperties(BOULDERS).walkable);
		CHECK(terrainProperties(BOULDERS).projectileBlocks);
		CHECK(terrainProperties(BOULDERS).flyable);
		CHECK_FALSE(terrainProperties(RIDGE_ROCK).projectileBlocks);
		CHECK(terrainProperties(DIRT).buildable);
		CHECK(terrainProperties(DIRT).inhibitionQ8 == 256);
		CHECK(terrainProperties(DIRT).shoreSupportQ8 == 0);
		CHECK_FALSE(terrainProperties(DIRT).shoreline);
		CHECK(terrainProperties(MUD).groundSpeedQ8 == 160);
		CHECK_FALSE(terrainProperties(MUD).buildable);
		CHECK(terrainProperties(DIRT_TRACK).groundSpeedQ8 == 512);
		CHECK(terrainProperties(LAVA).airHealthQ8 == -64);
		CHECK_FALSE(terrainProperties(LAVA).walkable);
		CHECK(terrainProvidesFertility(terrainProperties(LOAM)));
		CHECK(terrainProperties(LOAM).fertilityQ8 == 768);
		CHECK(terrainProperties(LOAM).buildable);
		CHECK(terrainProperties(DEEP_WATER).swimmable);
		CHECK(terrainProperties(DEEP_WATER).groundSpeedQ8 == 192);
		CHECK(terrainProperties(DEEP_WATER).allowedResources == 0);
		CHECK_FALSE(terrainProperties(VOID_HOLE).flyable);
		CHECK(terrainProperties(VOID_HOLE).projectileBlocks);
	}
	TEST_CASE("write equivalent custom maps for paired performance runs [benchmark][artifacts]")
	{
		glob2test::HeadlessGlobals globals;
		using Json = nlohmann::json;
		for (unsigned count : {7, 259, 1024, 16384})
		{
			GameGUI world;
			GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(
				*globalContainer->fileManager, "maps/FourSquares1.map"));
			REQUIRE(world.game.load(&input));
			auto &map = world.game.map;
			map.game = nullptr;
			if (count > 7)
			{
				Json definitions = Json::array();
				for (unsigned i = 7; i < count; ++i)
				{
					const auto *preset = TerrainPresentations[(i - 7) % 5].name;
					definitions.push_back({{"key", "bench:t" + std::to_string(i)},
										   {"name", "Equivalent terrain"},
										   {"base", preset},
										   {"appearance", preset},
										   {"properties", Json::object()}});
				}
				map.importTerrainDefinitions(
					Json{{"schemaVersion", 1}, {"terrains", definitions}}.dump());
				std::array<std::vector<TerrainType>, 5> equivalents;
				for (unsigned i = 7; i < map.terrainRegistry().size(); ++i)
					equivalents[map.terrainRegistry().appearance(TerrainType(i))].push_back(
						TerrainType(i));
				auto batch = map.editTerrain();
				for (int y = 0; y < map.getH(); ++y)
					for (int x = 0; x < map.getW(); ++x)
					{
						auto original = map.terrainTypeAt(x, y);
						if (original < GRASS_SAND_SHORE)
						{
							const auto &choices = equivalents[original];
							map.setCellTerrain(x, y,
											   choices[terrainVisualHash(x, y) % choices.size()]);
						}
					}
			}
			map.setGame(&world.game);
			const auto path =
				glob2test::artifactDir() / ("equivalent-" + std::to_string(count) + ".map");
			REQUIRE(globalContainer->fileManager->writeAtomically(
				path.string(),
				[&](GAGCore::OutputStream &out) { world.game.save(&out, true, "FourSquares1"); }));
			MESSAGE(path.string());
		}
	}
}
