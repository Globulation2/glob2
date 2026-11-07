// SPDX-License-Identifier: GPL-3.0-or-later
#include "HeadlessGlobals.h"
#include "HeadlessGame.h"
#include "GradientFixture.h"
#include "TerrainLine.h"
#include "TerrainPresentation.h"
#include "BinaryStream.h"
#include "TextStream.h"
#include "PackedArray.h"
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
#include <queue>

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
    // still resolve the same material without changing rules.
    CHECK_EQ(static_cast<unsigned>(TRAIL),4);
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

TEST_CASE("vertex terrain survives tiling and batches snapshot invalidation")
{
    glob2test::HeadlessGlobals globals;
    Map map;
    map.setSize(5,5,WATER);
    const auto old = map.frozenVertexSnapshot();
    const auto generation = map.terrainGeneration();
    {
        auto batch = map.editTerrain();
        map.paintCell(8,8,TRAIL);
        const auto partial=map.frozenVertexSnapshot();
        map.resourceGrowthField();
        map.paintCell(11,8,ICE);
        CHECK_EQ((*partial)[map.coordToIndex(11,8)],WATER);
        CHECK_EQ((*map.frozenVertexSnapshot())[map.coordToIndex(11,8)],ICE);
        CHECK_FALSE(map.growthCache.validFor(map));
        map.resourceGrowthField();
    }
    CHECK_EQ(map.terrainGeneration(),generation+1);
    // The field already includes the final mutation; publishing the batch's
    // general terrain generation must not discard it again.
    CHECK(map.growthCache.validFor(map));
    CHECK_EQ((*old)[map.coordToIndex(8,8)],WATER);
    CHECK_EQ(map.terrainTypeAt(8,8),TRAIL);
    CHECK_EQ(map.terrainTypeAt(11,8),ICE);
    CHECK_EQ(map.terrainTypeAt(7,8),MIXED_TERRAIN);
    CHECK_EQ(map.terrainTypeAt(6,8),WATER);
    map.tile(2,1);
    CHECK_EQ(map.terrainTypeAt(40,8),TRAIL);
    CHECK_EQ(map.terrainTypeAt(43,8),ICE);
}

TEST_CASE("same-size map replacement and terrain edits refresh ecology")
{
    glob2test::HeadlessGlobals globals;
    Map map;
    map.setSize(5,5,WATER);
    CHECK(map.resourceGrowthField().landField().at(8,8)==Fertility::kScale);
    map.setSize(5,5,GRASS);
    CHECK_FALSE(map.growthCache.validFor(map));
    CHECK(map.resourceGrowthField().landField().at(8,8)==0);
    map.paintCell(9,8,WATER);
    CHECK_FALSE(map.growthCache.validFor(map));
    CHECK(map.resourceGrowthField().landField().at(8,8)>0);
    map.tile(2,1);
    CHECK_FALSE(map.growthCache.validFor(map));
    const auto& field=map.resourceGrowthField().landField();
    CHECK(field.at(8,8)==field.at(40,8));
}

TEST_CASE("terrain edits refresh escape costs after captured routes publish at their fixed deadline")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto& map = world.game.map;
    for (int y=6;y<11;++y) for (int x=6;x<11;++x) map.addForbidden(x,y,0);
    auto* escape = glob2test::awaitGradient(world.game,[&] { return map.getForbiddenGradient(0,MovementAccess::Land); });
    const auto old = escape[map.coordToIndex(8,8)];
    {
        auto batch = map.editTerrain();
        for (int x=8;x<12;++x) map.paintCell(x,8,TRAIL);
    }
    CHECK(escape[map.coordToIndex(8,8)] == old); // Guidance remains stale until publication.
    map.updateForbiddenGradient(0,MovementAccess::Land);
    auto* refreshed=glob2test::awaitGradient(world.game,[&] {
        auto* field=map.getForbiddenGradient(0,MovementAccess::Land);
        return field!=escape ? field : nullptr;
    });
    CHECK(refreshed[map.coordToIndex(8,8)] > old);
    map.resetGradientPipeline();
    map.configureGradientPipeline(0,2);
    map.setResourceByIndex(15,15,WHEAT,1);
    map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);
    map.advanceGradientPipeline();
    auto& pipeline = map.gradientRuntime->pipeline;
    map.stagePeriodicGradientPreparation();
    map.game->snapshotStore().invalidateBoundary();
    map.preparePendingGradient(map.game->snapshotStore().captureBoundary(*map.game));
    std::vector<Uint16> captured;
    pipeline.visitPendingSnapshots([&](const auto& pending) {
        captured.assign(pending.data,pending.data+map.cellCount());
    });
    REQUIRE(captured.size()==map.cellCount());
    // Edit passable cells near the material source: the earlier escape fixture's
    // forbidden pocket masks terrain costs and cannot distinguish generations.
    map.paintCell(14,15,ICE);
    map.advanceGradientPipeline();
    CHECK(map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land)==nullptr);
    // Another soft edit before the very next advance cannot move the deadline.
    map.paintCell(13,15,TRAIL);
    std::vector<Uint16> replacement(map.cellCount());
    map.seedMaterialGradient(0,WHEAT,0,replacement.data());
    map.propagateGradient(replacement.data(),0);
    REQUIRE(replacement!=captured);
    map.advanceGradientPipeline();
    REQUIRE(map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land)!=nullptr);
    CHECK(std::equal(captured.begin(),captured.end(),map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land)));
    CHECK_EQ(pipeline.metrics.discarded,0);
    CHECK_EQ(pipeline.metrics.published,1);
    map.updateMaterialGradient(0,WHEAT,MovementAccess::Land);
    const auto* refreshedMaterial=glob2test::awaitGradient(world.game,[&] {
        auto* field=map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);
        return field && std::equal(replacement.begin(),replacement.end(),field) ? field : nullptr;
    });
    CHECK(std::equal(replacement.begin(),replacement.end(),refreshedMaterial));
}

TEST_CASE("retired terrain flags load from old headers without becoming experiments")
{
    glob2test::HeadlessGlobals globals;
    const std::vector<std::string> retired = {
        "ice-terrain", "road-terrain", "obstacle-terrain", "ridge-terrain",
        "barren-terrain", "rough-terrain", "path-terrain", "lava-terrain",
        "fertile-terrain", "deep-water-terrain", "void-terrain"};
    for (const auto& key : retired)
    {
        CHECK_FALSE(knownExperimentKey(key));
        CHECK(ExperimentSet::fromText(key).empty());
    }
    MapHeader header;
    auto* bytes = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(bytes);
    size_t shaPosition = 0;
    header.save(&output, &shaPosition);
    output.flush();
    const std::string original(bytes->getBuffer(), bytes->getPosition());
    const auto withRequirements = [&](const std::vector<std::string>& keys) {
        // Recreate the serialized requirement list written by pre-graduation builds.
        auto* storage = new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream out(storage);
        ExperimentSet::fromKeys(keys, nullptr, keys).save(&out);
        out.flush();
        std::string legacy = original;
        legacy.insert(shaPosition + 20, storage->getBuffer(), storage->getPosition());
        const unsigned version = FILE_FORMAT_VERSION_MAINLINE_TERRAINS - 1;
        // An empty map name puts the minor version at byte 8.
        for (unsigned i = 0; i < 4; ++i) legacy[8+i] = char(version >> (24-8*i));
        return legacy;
    };
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(withRequirements(retired)));
    MapHeader restored;
    REQUIRE(restored.load(&input));
    CHECK(restored.getVersionMinor() == FILE_FORMAT_VERSION_MAINLINE_TERRAINS - 1);
    CHECK(restored.getMapName() == header.getMapName());
    CHECK(restored.getNumberOfTeams() == header.getNumberOfTeams());
    // Re-saving upgrades the header and writes no retired requirement slot.
    auto* upgradedBytes = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream upgraded(upgradedBytes);
    restored.save(&upgraded);
    upgraded.flush();
    CHECK(std::string(upgradedBytes->getBuffer(), upgradedBytes->getPosition()) == original);
    // Truly unknown requirements remain an error, rather than being silently lost.
    GAGCore::BinaryInputStream corrupt(new GAGCore::MemoryStreamBackend(withRequirements({"unknown-terrain"})));
    CHECK_FALSE(restored.load(&corrupt));

    // A saved game's old flags are retired too; its terrain survives.
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    world.game.map.paintCell(8,8,ICE);
    world.game.map.paintCell(12,8,TRAIL);
    world.game.gameHeader.setExperiments(ExperimentSet::fromKeys(retired, nullptr, retired));
    auto* savedBytes = new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream savedOutput(savedBytes);
    world.game.save(&savedOutput, false, "retired terrains");
    savedOutput.flush();
    GAGCore::BinaryInputStream savedInput(new GAGCore::MemoryStreamBackend(
        std::string(savedBytes->getBuffer(), savedBytes->getPosition())));
    GameGUI loaded;
    REQUIRE(loaded.game.load(&savedInput));
    CHECK(loaded.game.gameHeader.getExperiments().empty());
    CHECK(loaded.game.map.terrainTypeAt(8,8) == ICE);
    CHECK(loaded.game.map.terrainTypeAt(12,8) == TRAIL);
    CHECK(loaded.game.map.checkSum(true) == world.game.map.checkSum(true));
}

TEST_CASE("ice exposure includes stationary units and preserves fractional health in saves")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto& map = world.game.map;
    map.paintCell(8,8,ICE);
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
    world.game.map.paintCell(8,8,ICE);
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
    world.game.map.paintCell(8,8,ICE);
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

TEST_CASE("constrained explorer flag eligibility applies route accessibility and hunger distance")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    world.game.map.paintCell(24,24,VOID_HOLE);
    REQUIRE(world.game.map.hasAirRoutingConstraints());
    Unit* explorer=world.addUnit(EXPLORER,8,8);
    Building* flag=world.addBuilding("explorationflag",12,12);
    REQUIRE(explorer); REQUIRE(flag);
    explorer->activity=Unit::ACT_RANDOM;
    explorer->medical=Unit::MED_FREE;
    flag->unitStayRange=0;
    int routeDistance=0,distance=0;
    // This case compares settled route distance with the hunger threshold.
    glob2test::awaitBuildingGradient(world.game,flag,AIR_MOVEMENT_CLASS,BuildingRoute::Combat);
    REQUIRE(world.game.map.buildingAvailable(flag,MovementAccess::Air,8,8,&routeDistance,BuildingRoute::Combat));
    REQUIRE(routeDistance>0);
    explorer->hungriness=1;
    explorer->hungry=explorer->trigHungry+routeDistance-1;
    CHECK_FALSE(flag->considerUnitForExplorerFlag(explorer,&distance,0));
    explorer->hungry=explorer->trigHungry+routeDistance;
    CHECK(flag->considerUnitForExplorerFlag(explorer,&distance,INT_MAX));
    CHECK_EQ(distance,routeDistance*routeDistance);
    world.game.map.addForbidden(12,12,0);
    world.game.map.updateGlobalGradient(flag,MovementAccess::Air,BuildingRoute::Combat);
    CHECK(flag->considerUnitForExplorerFlag(explorer,&distance,0));
    CHECK_EQ(distance,routeDistance*routeDistance);
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
		first.paintCell(3, 3, bog);
		CHECK(first.terrainQueueBuckets() == 256);
		CHECK(first.hasTerrainMovementModifiers());
		CHECK_FALSE(first.isFreeForGroundUnit(3, 3, false, 1));
		CHECK(first.isFreeForGroundUnit(3, 3, true, 1));
		first.paintCell(3, 3, GRASS);
		CHECK(first.terrainQueueBuckets() == 64);
		CHECK_FALSE(first.hasTerrainMovementModifiers());
		first.importTerrainDefinitions(
			R"({"schemaVersion":1,"terrains":[{"key":"test:bog","name":"Water equivalent","base":"water","properties":{},"appearance":"sand"}]})");
		first.paintCell(3, 3, bog);
		second.paintCell(3, 3, WATER);
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
		map.paintCell(8, 8, hazard);
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
		CHECK(map.resourceGrowthRateAt(map.coordToIndex(8, 8), WHEAT) > 0);
		CHECK(map.stepCost(1, 0, map.coordToIndex(8, 8), 0) == 29);
		CHECK(map.stepCost(1, 1, map.coordToIndex(8, 8), 0) == 40);
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
					map.paintCell(x, y, hazard);
		}
		map.paintCell(2, 2, GRASS);
		map.paintCell(12, 12, GRASS);
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
						map.paintCell(x, y, bog);
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
			ComputeExecutor executor; executor.configure(2);
			GradientPipeline pipeline;
			pipeline.configure(executor, 2, 1024,
                               [profiles=map.frozenTerrainMovementSnapshot(swim), buckets=map.terrainQueueBuckets()](auto &job, auto &scratch)
							   {
								   gradient_kernel::propagateTerrainProfiles(
									   job.data.get(), job.swim, gradient_kernel::COST_LIMIT,
									   {32, 32}, scratch, profiles->data(),
									   profiles->movement, buckets);
							   });
			auto owned = std::make_unique<Uint16[]>(1024);
			std::copy(pipelined.begin(), pipelined.end(), owned.get());
			auto *field = owned.release();
			pipeline.advance();
			pipeline.submit(&field, swim,
							[&](auto &job)
							{
								std::copy(pipelined.begin(), pipelined.end(), job.data.get());
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
				// Lookup identity is amphibious access; the team's living units
				// select the weighted preparation costs exercised by this case.
				auto* swimmer = world.addUnit(WORKER, 2, 2);
				REQUIRE(swimmer);
				swimmer->performance[WALK] = 30;
				swimmer->performance[SWIM] = 10;
				world.team->updateRoutingRatio();
				REQUIRE(world.team->routingRatio.selected == 6);
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
							map.paintCell(x, y, water);
				}
				CHECK(map.hasTerrainMovementModifiers() == (speed != 256));
				map.setResourceByIndex(20, 20, WHEAT, 1);
				map.getMaterialGradientSlot(0, WHEAT, MovementAccess::Amphibious);
				std::vector<Uint16> expected(1024);
				map.seedMaterialGradient(0, WHEAT, 6, expected.data());
				map.propagateGradient(expected.data(), 6);

				// Exercise Map's actual dispatch and worker callback: neutral-speed
				// custom water captures a binary plane, while slow water captures
				// compact movement profiles and selects the larger queue.
				map.configureGradientPipeline(workers, 2);
				map.advanceGradientPipeline();
				map.syncStep(0);
				map.stagePeriodicGradientPreparation();
				map.preparePendingGradient(world.game.snapshotStore().captureBoundary(world.game));
				REQUIRE(map.gradientRuntime->pipeline.pendingCount() == 1);
				map.gradientRuntime->pipeline.visitPendingSnapshots(
					[&](const auto &pending)
					{
						CHECK_FALSE(pending.superseded);
						CHECK(pending.swim == 6);
						CHECK(std::equal(expected.begin(), expected.end(), pending.data));
					});

				importBeforeMatch(world.game, definitions(speed == 256 ? 64 : 256));
				CHECK(map.terrainRegistry().find("test:water") == water);
				map.gradientRuntime->pipeline.visitPendingSnapshots([&](const auto &pending)
																	{ CHECK(pending.superseded); });
				std::vector<Uint16> replacement(1024);
				map.seedMaterialGradient(0, WHEAT, 6, replacement.data());
				map.propagateGradient(replacement.data(), 6);
				REQUIRE(replacement != expected);
				map.updateMaterialGradient(0, WHEAT, MovementAccess::Amphibious);
				map.advanceGradientPipeline();
				map.advanceGradientPipeline();
				CHECK(map.gradientRuntime->pipeline.metrics.discarded == 1);
				CHECK(map.gradientRuntime->pipeline.pendingCount() == 0);
				CHECK(map.lookupMaterialGradientSlot(0, WHEAT, MovementAccess::Amphibious) == nullptr);
				// The replacement is queued, not synchronously built by update. Admit
				// it at a completed-world boundary and await its own fixed deadline.
				const auto* published=glob2test::awaitGradient(world.game,[&] { return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious); });
				CHECK(map.gradientRuntime->pipeline.metrics.published==1);
				CHECK(map.publishedCostClass(published) == 6);
				CHECK(map.materialGradients[0][WHEAT][6] == nullptr);
				CHECK(std::equal(replacement.begin(),replacement.end(),published));
			}
	}
	TEST_CASE("unused distinct costs do not expand map movement setup")
	{
		glob2test::HeadlessGlobals globals;
		Map map;
		map.setSize(5, 5, GRASS);
		map.paintCell(8, 8, ICE);
		std::vector<Uint16> expected(1024, 1);
		expected[0] = GRADIENT_AT_GOAL;
		map.propagateGradient(expected.data(), 6);
		const auto baseline = map.frozenTerrainMovementSnapshot(6);
		// The square patch contains grass plus quarter-, half- and full-ice
		// cells, each with a distinct cardinal and diagonal entry cost.
		REQUIRE(baseline->movement.profiles.size() == 4);
		REQUIRE(baseline->movement.steps.size() == 8);
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
		CHECK(snapshot->movement.profiles.size() == baseline->movement.profiles.size());
		CHECK(snapshot->movement.steps == baseline->movement.steps);
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
		world.game.map.paintCell(8, 8, TerrainType(2000));
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
		map.paintCell(8, 8, *map.terrainRegistry().find("test:hazard"));
		map.paintCell(9, 8, *map.terrainRegistry().find("test:bog"));
		auto *worker = world.addUnit(WORKER, 8, 8);
		REQUIRE(worker);
		for (int i = 0; i < 7; ++i)
			worker->applyTerrainHealth();
		map.setResourceByIndex(15, 15, WHEAT, 1);
		map.getMaterialGradientSlot(0, WHEAT, MovementAccess::Amphibious);
		map.configureGradientPipeline(1, 2);
		map.advanceGradientPipeline();
		map.stagePeriodicGradientPreparation();
		map.game->snapshotStore().invalidateBoundary(); map.preparePendingGradient(map.game->snapshotStore().captureBoundary(*map.game));
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
		const auto* sourceField = map.lookupMaterialGradientSlot(0, WHEAT, MovementAccess::Amphibious);
		const auto* restoredField = resumed.game.map.lookupMaterialGradientSlot(0, WHEAT, MovementAccess::Amphibious);
		REQUIRE(sourceField);
		REQUIRE(restoredField);
		CHECK(std::equal(sourceField, sourceField + map.getW() * map.getH(), restoredField));
		CHECK(map.publishedCostClass(sourceField) == resumed.game.map.publishedCostClass(restoredField));
		CHECK(map.materialGradients[0][WHEAT][6] == nullptr);
		CHECK(resumed.game.map.materialGradients[0][WHEAT][6] == nullptr);
		for (int i = 0; i < 100; ++i)
		{
			world.game.step(0);
			resumed.game.step(0);
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
	TEST_CASE("format-186 saves keep built-in IDs and move custom terrain behind the grass and sand looks")
	{
		glob2test::HeadlessGlobals globals;
		// Written by an origin/master build (TERRAIN_COUNT 29) with two runtime
		// definitions painted at IDs 29 and 30 beside chasm, ice, trail, sand and water.
		auto *backend = globalContainer->fileManager->openInflatingInputStreamBackend(
			"test/fixtures/terrain-catalogue/custom-registry-186.game.gz");
		REQUIRE(backend);
		GameGUI loaded;
		{
			GAGCore::BinaryInputStream input(backend);
			REQUIRE(loaded.game.load(&input));
		}
		auto verify = [](const Map &map)
		{
			REQUIRE(map.terrainRegistry().size() == TERRAIN_COUNT + 2);
			const auto bog = map.terrainRegistry().find("fixture:bog");
			const auto mud = map.terrainRegistry().find("fixture:mud");
			REQUIRE(bog);
			REQUIRE(mud);
			CHECK(*bog == TerrainType(TERRAIN_COUNT));
			CHECK(*mud == TerrainType(TERRAIN_COUNT + 1));
			CHECK(map.terrainProperties(*bog).swimmable);
			CHECK(map.terrainProperties(*bog).groundSpeedQ8 == 128);
			CHECK(map.terrainProperties(*mud).groundSpeedQ8 == 192);
			CHECK_FALSE(map.terrainProperties(*mud).buildable);
			CHECK(map.terrainTypeAt(4, 4) == *bog);
			CHECK(map.terrainTypeAt(5, 4) == *bog);
			CHECK(map.terrainTypeAt(6, 6) == *mud);
			CHECK(map.terrainTypeAt(8, 8) == CHASM);
			CHECK(map.terrainTypeAt(10, 10) == ICE);
			CHECK(map.terrainTypeAt(12, 10) == TRAIL);
			CHECK(map.terrainTypeAt(14, 14) == SAND);
			CHECK(map.terrainTypeAt(16, 14) == WATER);
			CHECK(map.terrainTypeAt(0, 0) == GRASS);
		};
		verify(loaded.game.map);
		auto *resaved = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(resaved);
		loaded.game.save(&output, false, "custom-registry-187");
		output.flush();
		GameGUI reloaded;
		{
			GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(
				std::string(resaved->getBuffer(), resaved->getPosition())));
			REQUIRE(reloaded.game.load(&input));
		}
		verify(reloaded.game.map);
		CHECK(reloaded.game.map.terrainRegistry().digest() == loaded.game.map.terrainRegistry().digest());
		CHECK(reloaded.game.map.checkSum(true) == loaded.game.map.checkSum(true));
	}
	TEST_CASE("format-194 saves keep built-in IDs and move custom terrain behind the format-195 built-ins")
	{
		glob2test::HeadlessGlobals globals;
		// Written by an origin/master build (TERRAIN_COUNT 33) with two runtime
		// definitions painted at IDs 33 and 34 beside pale sand, chasm, ice, trail,
		// sand and water.
		auto *backend = globalContainer->fileManager->openInflatingInputStreamBackend(
			"test/fixtures/terrain-catalogue/custom-registry-194.game.gz");
		REQUIRE(backend);
		GameGUI loaded;
		{
			GAGCore::BinaryInputStream input(backend);
			REQUIRE(loaded.game.load(&input));
		}
		CHECK(loaded.game.mapHeader.getVersionMinor() == 194);
		auto verify = [](const Map &map)
		{
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
			CHECK(map.terrainTypeAt(8, 8) == CHASM);
			CHECK(map.terrainTypeAt(10, 10) == ICE);
			CHECK(map.terrainTypeAt(12, 10) == TRAIL);
			CHECK(map.terrainTypeAt(14, 14) == SAND);
			CHECK(map.terrainTypeAt(16, 14) == WATER);
			CHECK(map.terrainTypeAt(18, 18) == PALE_SAND);
			CHECK(map.terrainTypeAt(0, 0) == GRASS);
		};
		verify(loaded.game.map);
		auto *resaved = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(resaved);
		loaded.game.save(&output, false, "custom-registry-195");
		output.flush();
		GameGUI reloaded;
		{
			GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(
				std::string(resaved->getBuffer(), resaved->getPosition())));
			REQUIRE(reloaded.game.load(&input));
		}
		CHECK(reloaded.game.mapHeader.getVersionMinor() == VERSION_MINOR);
		verify(reloaded.game.map);
		CHECK(reloaded.game.map.terrainRegistry().digest() == loaded.game.map.terrainRegistry().digest());
		CHECK(reloaded.game.map.checkSum(true) == loaded.game.map.checkSum(true));
		CHECK(nlohmann::json::parse(reloaded.game.map.terrainRegistry().serialize())["terrains"][0]["id"] == TERRAIN_COUNT);
	}
	TEST_CASE("sandstone and pumice share every rule of their path and rough groups")
	{
		for (auto [type, member] : {std::pair{SANDSTONE, BRICK_PATH}, std::pair{SANDSTONE, BOARDWALK},
									std::pair{PUMICE, MUD}, std::pair{PUMICE, SCREE}})
		{
			CAPTURE(terrainPresentation(type).name);
			CHECK(terrainGroup(type) == terrainGroup(member));
			CHECK(sameTerrainProperties(terrainProperties(type), terrainProperties(member)));
			CHECK(TerrainRegistry::builtins()->propertyIndex(type) ==
				  TerrainRegistry::builtins()->propertyIndex(member));
		}
		CHECK(terrainGroup(SANDSTONE) == TerrainGroup::Paths);
		CHECK(terrainGroup(PUMICE) == TerrainGroup::Rough);
		CHECK(std::string(terrainPresentation(SANDSTONE).name) == "sandstone");
		CHECK(std::string(terrainPresentation(PUMICE).name) == "pumice");
		CHECK(TerrainRegistry::builtins()->find("sandstone") == SANDSTONE);
		CHECK(TerrainRegistry::builtins()->find("pumice") == PUMICE);
	}
	TEST_CASE("a frozen lake shares every rule of ice")
	{
		CHECK(terrainGroup(FROZEN_LAKE) == TerrainGroup::Ice);
		CHECK(terrainPaintable(FROZEN_LAKE));
		CHECK(sameTerrainProperties(terrainProperties(FROZEN_LAKE), terrainProperties(ICE)));
		CHECK(TerrainRegistry::builtins()->propertyIndex(FROZEN_LAKE) ==
			  TerrainRegistry::builtins()->propertyIndex(ICE));
		CHECK(std::string(terrainPresentation(FROZEN_LAKE).name) == "frozen_lake");
		CHECK(TerrainRegistry::builtins()->find("frozen_lake") == FROZEN_LAKE);
	}
	TEST_CASE("wind-swept snow shares every rule of deep snow")
	{
		CHECK(terrainGroup(WIND_SWEPT_SNOW) == terrainGroup(DEEP_SNOW));
		CHECK(terrainPaintable(WIND_SWEPT_SNOW));
		CHECK(sameTerrainProperties(terrainProperties(WIND_SWEPT_SNOW), terrainProperties(DEEP_SNOW)));
		CHECK(TerrainRegistry::builtins()->propertyIndex(WIND_SWEPT_SNOW) ==
			  TerrainRegistry::builtins()->propertyIndex(DEEP_SNOW));
		CHECK(std::string(terrainPresentation(WIND_SWEPT_SNOW).name) == "wind_swept_snow");
		CHECK(TerrainRegistry::builtins()->find("wind_swept_snow") == WIND_SWEPT_SNOW);
	}
	TEST_CASE("weathered sandstone shares every rule of dirt")
	{
		CHECK(terrainGroup(WEATHERED_SANDSTONE) == TerrainGroup::Barren);
		CHECK(terrainPaintable(WEATHERED_SANDSTONE));
		CHECK(sameTerrainProperties(terrainProperties(WEATHERED_SANDSTONE), terrainProperties(DIRT)));
		CHECK(TerrainRegistry::builtins()->propertyIndex(WEATHERED_SANDSTONE) ==
			  TerrainRegistry::builtins()->propertyIndex(DIRT));
		CHECK(std::string(terrainPresentation(WEATHERED_SANDSTONE).name) == "weathered_sandstone");
		CHECK(TerrainRegistry::builtins()->find("weathered_sandstone") == WEATHERED_SANDSTONE);
	}
	TEST_CASE("windy sand dunes share every rule of sand")
	{
		CHECK(terrainGroup(WINDY_SAND_DUNE) == TerrainGroup::Sand);
		CHECK(terrainPaintable(WINDY_SAND_DUNE));
		CHECK(sameTerrainProperties(terrainProperties(WINDY_SAND_DUNE), terrainProperties(SAND)));
		CHECK(TerrainRegistry::builtins()->propertyIndex(WINDY_SAND_DUNE) ==
			  TerrainRegistry::builtins()->propertyIndex(SAND));
		CHECK(std::string(terrainPresentation(WINDY_SAND_DUNE).name) == "windy_sand_dune");
		CHECK(TerrainRegistry::builtins()->find("windy_sand_dune") == WINDY_SAND_DUNE);
	}
	TEST_CASE("hardpan shares every rule of the rough group")
	{
		for (auto member : {MUD, MUDFLAT, SCREE, PUMICE})
		{
			CAPTURE(terrainPresentation(member).name);
			CHECK(terrainGroup(HARDPAN) == terrainGroup(member));
			CHECK(sameTerrainProperties(terrainProperties(HARDPAN), terrainProperties(member)));
			CHECK(TerrainRegistry::builtins()->propertyIndex(HARDPAN) ==
				  TerrainRegistry::builtins()->propertyIndex(member));
		}
		CHECK(terrainGroup(HARDPAN) == TerrainGroup::Rough);
		CHECK(terrainPaintable(HARDPAN));
		CHECK(std::string(terrainPresentation(HARDPAN).name) == "hardpan");
		CHECK(TerrainRegistry::builtins()->find("hardpan") == HARDPAN);
	}
	TEST_CASE("tundra shares every rule of the barren group")
	{
		CHECK(terrainGroup(TUNDRA) == TerrainGroup::Barren);
		CHECK(terrainPaintable(TUNDRA));
		for (TerrainType member : {DIRT, CLAY, SHALE, HEATH, WEATHERED_SANDSTONE})
		{
			CHECK(sameTerrainProperties(terrainProperties(TUNDRA), terrainProperties(member)));
			CHECK(TerrainRegistry::builtins()->propertyIndex(TUNDRA) ==
				  TerrainRegistry::builtins()->propertyIndex(member));
		}
		CHECK(std::string(terrainPresentation(TUNDRA).name) == "tundra");
		CHECK(TerrainRegistry::builtins()->find("tundra") == TUNDRA);
	}
	TEST_CASE("arctic tundra shares every rule of tundra and the barren group")
	{
		CHECK(terrainGroup(ARCTIC_TUNDRA) == TerrainGroup::Barren);
		CHECK(terrainPaintable(ARCTIC_TUNDRA));
		for (TerrainType member : {DIRT, CLAY, SHALE, HEATH, WEATHERED_SANDSTONE, TUNDRA})
		{
			CAPTURE(member);
			CHECK(sameTerrainProperties(terrainProperties(ARCTIC_TUNDRA), terrainProperties(member)));
			CHECK(TerrainRegistry::builtins()->propertyIndex(ARCTIC_TUNDRA) ==
				  TerrainRegistry::builtins()->propertyIndex(member));
		}
		CHECK(std::string(terrainPresentation(ARCTIC_TUNDRA).name) == "arctic_tundra");
		CHECK(TerrainRegistry::builtins()->find("arctic_tundra") == ARCTIC_TUNDRA);
	}
	TEST_CASE("volcanic ash shares every rule of dirt")
	{
		CHECK(terrainGroup(VOLCANIC_ASH) == TerrainGroup::Barren);
		CHECK(terrainPaintable(VOLCANIC_ASH));
		CHECK(sameTerrainProperties(terrainProperties(VOLCANIC_ASH), terrainProperties(DIRT)));
		CHECK(TerrainRegistry::builtins()->propertyIndex(VOLCANIC_ASH) ==
			  TerrainRegistry::builtins()->propertyIndex(DIRT));
		CHECK(std::string(terrainPresentation(VOLCANIC_ASH).name) == "volcanic_ash");
		CHECK(TerrainRegistry::builtins()->find("volcanic_ash") == VOLCANIC_ASH);
	}
	TEST_CASE("glacier ice shares every rule of ice")
	{
		CHECK(terrainGroup(GLACIER_ICE) == TerrainGroup::Ice);
		CHECK(terrainPaintable(GLACIER_ICE));
		CHECK(sameTerrainProperties(terrainProperties(GLACIER_ICE), terrainProperties(ICE)));
		CHECK(TerrainRegistry::builtins()->propertyIndex(GLACIER_ICE) == TerrainRegistry::builtins()->propertyIndex(ICE));
		CHECK(std::string(terrainPresentation(GLACIER_ICE).name) == "glacier_ice");
		CHECK(TerrainRegistry::builtins()->find("glacier_ice") == GLACIER_ICE);
	}
	TEST_CASE("overgrown road shares every rule of the other paths")
	{
		for (auto member : {TRAIL, BRICK_PATH, BOARDWALK, SANDSTONE})
		{
			CAPTURE(member);
			CHECK(terrainGroup(OVERGROWN_ROAD) == terrainGroup(member));
			CHECK(sameTerrainProperties(terrainProperties(OVERGROWN_ROAD), terrainProperties(member)));
			CHECK(TerrainRegistry::builtins()->propertyIndex(OVERGROWN_ROAD) ==
				  TerrainRegistry::builtins()->propertyIndex(member));
		}
		CHECK(terrainGroup(OVERGROWN_ROAD) == TerrainGroup::Paths);
		CHECK(terrainPaintable(OVERGROWN_ROAD));
		CHECK(std::string(terrainPresentation(OVERGROWN_ROAD).name) == "overgrown_road");
		CHECK(TerrainRegistry::builtins()->find("overgrown_road") == OVERGROWN_ROAD);
	}
	TEST_CASE("rock paths share every rule of the other paths")
	{
		const std::pair<TerrainType, const char *> rockPaths[] = {
			{SLATE_ASHLAR, "slate_ashlar"}, {BASALT_CAUSEWAY, "basalt_causeway"}, {SCORIA_BLOCKS, "scoria_blocks"}};
		for (auto [path, name] : rockPaths)
		{
			CAPTURE(name);
			for (auto member : {TRAIL, BRICK_PATH, BOARDWALK, SANDSTONE, OVERGROWN_ROAD})
			{
				CAPTURE(member);
				CHECK(terrainGroup(path) == terrainGroup(member));
				CHECK(sameTerrainProperties(terrainProperties(path), terrainProperties(member)));
				CHECK(TerrainRegistry::builtins()->propertyIndex(path) ==
					  TerrainRegistry::builtins()->propertyIndex(member));
			}
			CHECK(terrainGroup(path) == TerrainGroup::Paths);
			CHECK(terrainPaintable(path));
			CHECK(std::string(terrainPresentation(path).name) == name);
			CHECK(TerrainRegistry::builtins()->find(name) == path);
		}
	}
	TEST_CASE("ice paths share every rule of the other paths")
	{
		const std::pair<TerrainType, const char *> icePaths[] = {{RIME_ASHLAR, "rime_ashlar"},
																 {MOONSTONE_LOZENGE, "moonstone_lozenge"},
																 {FROST_PINWHEEL, "frost_pinwheel"}};
		for (auto [path, name] : icePaths)
		{
			CAPTURE(name);
			for (auto member : {TRAIL, BRICK_PATH, BOARDWALK, SANDSTONE, OVERGROWN_ROAD, SLATE_ASHLAR})
			{
				CAPTURE(member);
				CHECK(terrainGroup(path) == terrainGroup(member));
				CHECK(sameTerrainProperties(terrainProperties(path), terrainProperties(member)));
				CHECK(TerrainRegistry::builtins()->propertyIndex(path) ==
					  TerrainRegistry::builtins()->propertyIndex(member));
			}
			CHECK(terrainGroup(path) == TerrainGroup::Paths);
			CHECK(terrainPaintable(path));
			CHECK(std::string(terrainPresentation(path).name) == name);
			CHECK(TerrainRegistry::builtins()->find(name) == path);
		}
	}
	TEST_CASE("laterite shares every rule of dirt")
	{
		CHECK(terrainGroup(LATERITE) == TerrainGroup::Barren);
		CHECK(terrainPaintable(LATERITE));
		CHECK(sameTerrainProperties(terrainProperties(LATERITE), terrainProperties(DIRT)));
		CHECK(TerrainRegistry::builtins()->propertyIndex(LATERITE) == TerrainRegistry::builtins()->propertyIndex(DIRT));
		CHECK(std::string(terrainPresentation(LATERITE).name) == "laterite");
		CHECK(TerrainRegistry::builtins()->find("laterite") == LATERITE);
	}
	TEST_CASE("cliffs share every rule of hedge and thicket")
	{
		CHECK(terrainGroup(CLIFFS) == TerrainGroup::Obstacles);
		CHECK(terrainPaintable(CLIFFS));
		CHECK(sameTerrainProperties(terrainProperties(CLIFFS), terrainProperties(HEDGE)));
		CHECK(TerrainRegistry::builtins()->propertyIndex(CLIFFS) == TerrainRegistry::builtins()->propertyIndex(THICKET));
		CHECK_FALSE(terrainProperties(CLIFFS).walkable);
		CHECK(terrainProperties(CLIFFS).projectileBlocks);
		CHECK(std::string(terrainPresentation(CLIFFS).name) == "cliffs");
		CHECK(TerrainRegistry::builtins()->find("cliffs") == CLIFFS);
	}
	TEST_CASE("molten lava pool shares every rule of lava")
	{
		CHECK(terrainGroup(MOLTEN_LAVA_POOL) == TerrainGroup::Lava);
		CHECK(terrainPaintable(MOLTEN_LAVA_POOL));
		CHECK(sameTerrainProperties(terrainProperties(MOLTEN_LAVA_POOL), terrainProperties(LAVA)));
		CHECK(TerrainRegistry::builtins()->propertyIndex(MOLTEN_LAVA_POOL) ==
			  TerrainRegistry::builtins()->propertyIndex(LAVA));
		CHECK(std::string(terrainPresentation(MOLTEN_LAVA_POOL).name) == "molten_lava_pool");
		CHECK(TerrainRegistry::builtins()->find("molten_lava_pool") == MOLTEN_LAVA_POOL);
	}
	TEST_CASE("glacier shares every rule of cliffs")
	{
		CHECK(terrainGroup(GLACIER) == TerrainGroup::Obstacles);
		CHECK(terrainPaintable(GLACIER));
		CHECK(sameTerrainProperties(terrainProperties(GLACIER), terrainProperties(CLIFFS)));
		CHECK(TerrainRegistry::builtins()->propertyIndex(GLACIER) == TerrainRegistry::builtins()->propertyIndex(CLIFFS));
		CHECK_FALSE(terrainProperties(GLACIER).walkable);
		CHECK(terrainProperties(GLACIER).projectileBlocks);
		CHECK(std::string(terrainPresentation(GLACIER).name) == "glacier");
		CHECK(TerrainRegistry::builtins()->find("glacier") == GLACIER);
	}
	TEST_CASE("the new Barriers masses play like cliffs and the Outcrops looks like rock spires")
	{
		for (const auto type : {ICEFALL, SANDSTONE_BENCHES, OBSIDIAN_RIDGE, GRANITE_TORS, TRAVERTINE_TERRACES})
		{
			CAPTURE(int(type));
			CHECK(terrainGroup(type) == TerrainGroup::Obstacles);
			CHECK(terrainPaintable(type));
			CHECK(sameTerrainProperties(terrainProperties(type), terrainProperties(CLIFFS)));
			CHECK(terrainProperties(type).projectileBlocks);
		}
		for (const auto type : {PENITENTES, TERMITE_MOUNDS, HOODOOS, PETRIFIED_STUMPS})
		{
			CAPTURE(int(type));
			CHECK(terrainGroup(type) == TerrainGroup::Ridges);
			CHECK(terrainPaintable(type));
			CHECK(sameTerrainProperties(terrainProperties(type), terrainProperties(RIDGE_ROCK)));
			CHECK_FALSE(terrainProperties(type).walkable);
			CHECK_FALSE(terrainProperties(type).projectileBlocks);
		}
		CHECK(TerrainRegistry::builtins()->find("icefall") == ICEFALL);
		CHECK(TerrainRegistry::builtins()->find("petrified_stumps") == PETRIFIED_STUMPS);
	}
	TEST_CASE("grass and sand looks share every rule of grass and sand")
	{
		for (auto [look, classic] : {std::pair{LUSH_GRASS, GRASS}, std::pair{DRY_GRASS, GRASS},
									 std::pair{DUNE_SAND, SAND}, std::pair{PALE_SAND, SAND}})
		{
			CAPTURE(terrainPresentation(look).name);
			CHECK(terrainGroup(look) == terrainGroup(classic));
			CHECK(sameTerrainProperties(terrainProperties(look), terrainProperties(classic)));
			CHECK(TerrainRegistry::builtins()->propertyIndex(look) ==
				  TerrainRegistry::builtins()->propertyIndex(classic));
		}
		// The editor lays a sand beach where any grass look meets water.
		glob2test::HeadlessGlobals globals;
		Map map;
		map.setSize(5, 5, WATER);
		map.paintVertices({{2, 2}}, DRY_GRASS);
		CHECK(map.vertexTerrainAt(2, 2) == DRY_GRASS);
		CHECK(map.vertexTerrainAt(1, 1) == SAND);
		CHECK(map.vertexTerrainAt(3, 2) == SAND);
		CHECK(map.vertexTerrainAt(0, 0) == WATER);
	}
	TEST_CASE("painted catalogue terrain survives save and reload with its checksum")
	{
		glob2test::HeadlessGlobals globals;
		glob2test::HeadlessGame world({.loadDefaultRace = true, .header = true});
		auto &map = world.game.map;
		std::vector<TerrainType> painted;
		for (unsigned i = BOULDERS; i < TERRAIN_COUNT; ++i)
			if (terrainPaintable(TerrainType(i)))
				painted.push_back(TerrainType(i));
		// Every catalogue type from BOULDERS on is paintable.
		REQUIRE(painted.size() == TERRAIN_COUNT - BOULDERS);
		CHECK(std::find(painted.begin(), painted.end(), TUNDRA) != painted.end());
		for (std::size_t n = 0; n < painted.size(); ++n)
		{
			map.paintCell(int(n % 8) * 2 + 2, int(n / 8) * 3 + 2, painted[n]);
			CHECK(map.terrainTypeAt(int(n % 8) * 2 + 2, int(n / 8) * 3 + 2) == painted[n]);
		}
		map.paintCell(20, 20, ICE);
		map.paintCell(22, 20, TRAIL);
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
			CHECK(loaded.game.map.terrainTypeAt(22, 20) == TRAIL);
			CHECK(loaded.game.map.checkSum(true) == map.checkSum(true));
		}
	}
	TEST_CASE("each catalogue type shares its group profile")
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
			map.paintCell(2, 2, type);
			CHECK(sameTerrainProperties(map.terrainProperties(type),
										terrainGroupDefinition(terrainGroup(type)).properties));
		}
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
		CHECK(terrainProperties(BRICK_PATH).groundSpeedQ8 == 512);
		CHECK(terrainProperties(LAVA).airHealthQ8 == -64);
		CHECK_FALSE(terrainProperties(LAVA).walkable);
		CHECK(terrainProvidesFertility(terrainProperties(LOAM)));
		for (const auto type : {LOAM, MOSS, SPRING_MEADOW})
			CHECK(terrainProperties(type).fertilityQ8 == 48);
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
		// Built-ins grow over time. The requested cohort count includes them;
		// starting custom IDs at an old literal overfilled the capacity fixture.
		for (unsigned count : {unsigned(TERRAIN_COUNT), 259u, 1024u, unsigned(TerrainRegistry::Capacity)})
		{
			GameGUI world;
			GAGCore::BinaryInputStream input(glob2OpenMapOrSaveInputStreamBackend(
				*globalContainer->fileManager, "maps/FourSquares1.map"));
			REQUIRE(world.game.load(&input));
			auto &map = world.game.map;
			map.game = nullptr;
			if (count > TERRAIN_COUNT)
			{
				Json definitions = Json::array();
				for (unsigned i = TERRAIN_COUNT; i < count; ++i)
				{
					const auto *preset = TerrainPresentations[(i - TERRAIN_COUNT) % 5].name;
					definitions.push_back({{"key", "bench:t" + std::to_string(i)},
										   {"name", "Equivalent terrain"},
										   {"base", preset},
										   {"appearance", preset},
										   {"properties", Json::object()}});
				}
				map.importTerrainDefinitions(
					Json{{"schemaVersion", 1}, {"terrains", definitions}}.dump());
				REQUIRE(map.terrainRegistry().size() == count);
				std::array<std::vector<TerrainType>, 5> equivalents;
				for (unsigned i = TERRAIN_COUNT; i < map.terrainRegistry().size(); ++i)
					equivalents[map.terrainRegistry().appearance(TerrainType(i))].push_back(
						TerrainType(i));
				auto batch = map.editTerrain();
				for (int y = 0; y < map.getH(); ++y)
					for (int x = 0; x < map.getW(); ++x)
					{
						auto original = map.vertexTerrainAt(x, y);
						if (original <= TRAIL)
						{
							const auto &choices = equivalents[original];
							// At full catalog capacity, random aliases at every vertex
							// create too many transient corner combinations while painting.
							// Keep the complete catalog, but use spatially coherent aliases
							// spanning its range so the fixture remains a valid map.
							const bool maximum = count == TerrainRegistry::Capacity;
							const unsigned sx = maximum ? unsigned(x / 16) : unsigned(x);
							const unsigned sy = maximum ? unsigned(y / 16) : unsigned(y);
							map.setVertexTerrain(x, y,
											   choices[(sx * 73856093u ^ sy * 19349663u) % choices.size()]);
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


namespace
{
// Paints every vertex of the rectangle [x0,x1]x[y0,y1].
void paintVertices(Map& map, int x0, int y0, int x1, int y1, TerrainType type)
{
    auto edit=map.editTerrain();
    for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) map.setVertexTerrain(x,y,type);
}
}

TEST_SUITE("TerrainHazardRouting")
{
TEST_CASE("water-only movement minima track fast water and retain optimal obstacle routes")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.wDec=5,.hDec=5,.teams=1,.discovered=true,.clearImmobile=true,.header=true});
    auto& map=world.game.map;
    map.game=nullptr;
    map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"test:fast-water","name":"Fast water","base":"water","appearance":"water","properties":{"groundSpeedQ8":512}}]})");
    map.setGame(&world.game);
    const auto fast=map.terrainRegistry().find("test:fast-water");
    REQUIRE(fast.has_value());
    map.fillTerrain(WATER);
    CHECK(map.minStepCost(WATER_ONLY_CLASS)==10);
    map.paintCell(3,3,*fast);
    CHECK(map.minStepCost(WATER_ONLY_CLASS)==5);
    map.paintCell(3,3,WATER); // Removing the last fast cell must remove its histogram contribution.
    map.paintCell(25,25,TRAIL);
    CHECK(map.hasTerrainMovementModifiers());
    CHECK_FALSE(map.terrainPropertiesAt(25,25).swimmable); // The pure trail is walking-only.
    CHECK(map.terrainPropertiesAt(24,24).swimmable);
    CHECK(map.terrainPropertiesAt(24,24).groundSpeedQ8==320); // (3*256+512)/4.
    CHECK(map.minStepCost(WATER_ONLY_CLASS)==8); // Swimming shores inherit a quarter of trail speed.
    map.paintCell(25,25,WATER);
    CHECK(map.minStepCost(WATER_ONLY_CLASS)==10); // Removing the last fast shore restores the minimum.

    map.fillTerrain(*fast);
    // This fixed layout makes an overestimated water-only heuristic choose
    // a more expensive first move (94 instead of 77). Full grass cells make
    // actual swimming obstacles under corner voting; a lone grass vertex does not.
    for (const auto [x,y]:{std::pair{16,17}, {13,12}, {14,18}, {16,15}, {16,13}, {10,16}, {14,16}, {14,12}, {15,18}})
        paintVertices(map,x,y,x+1,y+1,GRASS);
    REQUIRE(map.minStepCost(WATER_ONLY_CLASS)==5);
    constexpr int startX=8,startY=16,targetX=20,targetY=16;
    // Independent reverse Dijkstra. Price each destination from its four raw
    // corner identities: pure fast water has speed512, while a three-water/one-
    // grass shore averages448 and costs6 cardinal/8 diagonal instead of5/7.
    // Neither cell merging, compiled engine costs nor its heuristic is used.
    std::vector<bool> swimming(map.size);
    std::vector<int> cardinal(map.size);
    for (size_t i=0;i<map.size;++i)
    {
        int speedSum=0,waterCorners=0;
        for (const auto corner:map.cellCorners(i))
        {
            REQUIRE((corner==*fast || corner==GRASS));
            speedSum+=corner==*fast ? 512 : 256;
            waterCorners+=corner==*fast;
        }
        swimming[i]=waterCorners>=3;
        const int speed=speedSum/4;
        cardinal[i]=(10*256+speed/2)/speed;
        CHECK(map.terrainPropertiesAt(i).groundSpeedQ8==speed);
        CHECK(map.terrainPropertiesAt(i).swimmable==swimming[i]);
    }
    const auto edgeCost=[&](size_t destination,bool diagonal) {
        return diagonal ? cardinal[destination]*14/10 : cardinal[destination];
    };
    std::vector<int> distance(map.size,INT_MAX);
    using Entry=std::pair<int,size_t>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> pending;
    const auto target=map.coordToIndex(targetX,targetY);
    distance[target]=0;
    pending.emplace(0,target);
    while (!pending.empty())
    {
        const auto [cost,index]=pending.top(); pending.pop();
        if (cost!=distance[index]) continue;
        const int x=int(index&map.wMask),y=int(index>>map.wDec);
        for (int dx=-1;dx<=1;++dx) for (int dy=-1;dy<=1;++dy)
        {
            if (!dx && !dy) continue;
            const auto next=map.coordToIndex(x+dx,y+dy);
            if (!swimming[next]) continue;
            // Reverse search: next is the predecessor, index the destination.
            const int candidate=cost+edgeCost(index,dx && dy);
            if (candidate<distance[next]) { distance[next]=candidate; pending.emplace(candidate,next); }
        }
    }
    REQUIRE(distance[map.coordToIndex(startX,startY)]==77);
    int x=startX,y=startY,travelCost=0;
    for (int step=0;step<64 && (x!=targetX || y!=targetY);++step)
    {
        int dx=0,dy=0;
        REQUIRE(map.pathfindPointToPoint(x,y,targetX,targetY,&dx,&dy,MovementAccess::Water,world.game.teams[0]->me,100));
        REQUIRE((dx!=0 || dy!=0));
        const auto next=map.coordToIndex(x+dx,y+dy);
        const int edge=edgeCost(next,dx && dy);
        CHECK(edge+distance[next]==distance[map.coordToIndex(x,y)]);
        travelCost+=edge;
        x=(x+dx)&map.wMask; y=(y+dy)&map.hMask;
    }
    CHECK(x==targetX); CHECK(y==targetY); CHECK(travelCost==77);
    map.fillTerrain(WATER);
    CHECK(map.minStepCost(WATER_ONLY_CLASS)==10);
}

TEST_CASE("ice costs twenty recovery ticks per HP and remains traversable")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;
    // One ice corner averages to 87.5% speed and -2/256 HP per tick.
    map.setVertexTerrain(10,9,ICE);
    CHECK(map.terrainPropertiesAt(9,8).groundSpeedQ8==224);
    CHECK(map.terrainPropertiesAt(9,8).groundHealthQ8==-2);
    // Round travel 10/0.875 to 11, then include 20 recovery ticks per HP:
    // round(11*(256+20*2)/256)=13; diagonal cost is floor(13*1.4)=18.
    CHECK(map.stepCost(1,0,map.coordToIndex(9,8),0)==13);
    CHECK(map.stepCost(1,1,map.coordToIndex(9,8),0)==18);
    CHECK(map.stepCost(1,0,map.coordToIndex(9,7),0)==10);
    int dx=0,dy=0;
    REQUIRE(map.pathfindPointToPoint(8,8,11,8,&dx,&dy,MovementAccess::Land,world.game.teams[0]->me,100));
    CHECK(dx==1); CHECK(dy==0); // The diluted icy row costs 36; the safe detour 38.
    // Two ice corners per cell make the direct strip expensive enough to avoid.
    paintVertices(map,9,9,11,9,ICE);
    CHECK(map.stepCost(1,0,map.coordToIndex(9,8),0)==17);
    REQUIRE(map.pathfindPointToPoint(8,8,11,8,&dx,&dy,MovementAccess::Land,world.game.teams[0]->me,100));
    CHECK(dy==-1); // The detour costs 42, including its one-corner icy landing; straight costs 47.
    // A corridor offers no safe alternative but must remain routable. Grass
    // vertices along row 8 make the cells of rows 7 and 8 walkable.
    map.fillTerrain(WATER);
    paintVertices(map,7,8,13,8,GRASS);
    map.setVertexTerrain(10,8,ICE);
    REQUIRE(map.pathfindPointToPoint(8,8,11,8,&dx,&dy,MovementAccess::Land,world.game.teams[0]->me,100));
    CHECK(dx==1); CHECK(dy==0);
    dx=dy=1;
    CHECK_FALSE(map.pathfindPointToPoint(8,8,11,8,&dx,&dy,MovementAccess::Land,world.game.teams[0]->me,2));
    CHECK(dx==0); CHECK(dy==0);
    // A safe loop exists, but it costs more than the icy route.
    paintVertices(map,7,9,7,14,GRASS);
    paintVertices(map,13,9,13,14,GRASS);
    paintVertices(map,7,14,13,14,GRASS);
    REQUIRE(map.pathfindPointToPoint(8,8,11,8,&dx,&dy,MovementAccess::Land,world.game.teams[0]->me,100));
    CHECK(dx==1); CHECK(dy==0);
}

TEST_CASE("idle ground units avoid entering ice and escape a multi-cell patch")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;
    auto* unit=world.addUnit(WORKER,8,8);
    REQUIRE(unit);
    // Ice vertices around the unit's cell touch every neighbouring cell, but
    // none of the four corners of its own.
    paintVertices(map,7,7,10,10,ICE);
    paintVertices(map,8,8,9,9,GRASS);
    CHECK(map.terrainPropertiesAt(8,8).groundHealthQ8==0);
    for(int n=0;n<32;++n) { map.pathfindRandom(unit); CHECK(unit->dx==0); CHECK(unit->dy==0); }
    paintVertices(map,8,8,9,9,ICE);
    REQUIRE(map.pathfindTerrainSafety(unit));
    CHECK((unit->dx!=0 || unit->dy!=0));
    CHECK(map.terrainPropertiesAt(8+unit->dx,8+unit->dy).groundHealthQ8<0);
    for(int n=0;n<8 && map.terrainPropertiesAt(unit->posX,unit->posY).groundHealthQ8<0;++n)
        unit->handleActionRandomGround();
    CHECK(map.terrainPropertiesAt(unit->posX,unit->posY).groundHealthQ8==0);
    map.fillTerrain(ICE);
    CHECK_FALSE(map.pathfindTerrainSafety(unit));
    CHECK(unit->dx==0); CHECK(unit->dy==0);
}

TEST_CASE("custom air damage routes flyers and idle flyers escape it")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;
    map.game=nullptr;
    map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"test:air-hazard","name":"Air hazard","base":"grass","properties":{"airHealthQ8":-256},"appearance":"ice"}]})");
    map.setGame(&world.game);
    const auto hazard=map.terrainRegistry().find("test:air-hazard");
    REQUIRE(hazard.has_value());
    map.setVertexTerrain(10,9,*hazard);
    CHECK(map.hasAirTerrainConstraints());
    int dx=0,dy=0;
    REQUIRE(map.pathfindAirPointToPoint(8,8,11,8,&dx,&dy));
    CHECK(dy!=0);
    auto* unit=world.addUnit(EXPLORER,8,8);
    REQUIRE(unit);
    paintVertices(map,7,7,10,10,*hazard);
    paintVertices(map,8,8,9,9,GRASS);
    for(int n=0;n<32;++n) { unit->handleActionRandomFly(); CHECK(unit->posX==8); CHECK(unit->posY==8); }
    paintVertices(map,8,8,9,9,*hazard);
    for(int n=0;n<8 && map.terrainPropertiesAt(unit->posX,unit->posY).airHealthQ8<0;++n)
        unit->handleActionRandomFly();
    CHECK(map.terrainPropertiesAt(unit->posX,unit->posY).airHealthQ8==0);
}
}

TEST_SUITE("TerrainHazardRouting")
{
TEST_CASE("extreme authored hazards saturate and shared fields agree with point costs")
{
    glob2test::HeadlessGlobals globals;
    Map map;
    map.setSize(5,5,GRASS);
    map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"test:extreme","name":"Extreme","base":"grass","properties":{"groundHealthQ8":-32768,"airHealthQ8":-32768},"appearance":"ice"}]})");
    const auto extreme=*map.terrainRegistry().find("test:extreme");
    map.paintCell(8,8,extreme);
    CHECK(map.stepCost(1,0,map.coordToIndex(8,8),0)==181);
    CHECK(map.stepCost(1,1,map.coordToIndex(8,8),0)==253);
    CHECK(map.terrainRegistry().airRouteCost(extreme)==181);
    CHECK(map.terrainQueueBuckets()==256);
    std::vector<Uint16> field(map.getW()*map.getH(),GRADIENT_UNREACHABLE);
    field[map.coordToIndex(8,8)]=GRADIENT_AT_GOAL;
    map.propagateGradient(field.data(),0);
    CHECK(field[map.coordToIndex(7,8)]==GRADIENT_AT_GOAL-181);
    // The hazard's transition cells cost as much, so the diagonal is cheapest.
    CHECK(field[map.coordToIndex(7,7)]==GRADIENT_AT_GOAL-253);
    const auto snapshot=map.frozenTerrainMovementSnapshot(0);
    map.paintCell(8,8,GRASS);
    CHECK(map.frozenTerrainMovementSnapshot(0)!=snapshot);
    CHECK(map.terrainQueueBuckets()==64);
    CHECK_FALSE(map.hasTerrainMovementModifiers());
}
TEST_CASE("old hazard route caches rebuild while current saves retain routing state")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;
    map.paintCell(9,8,ICE);
    // Wheat clear of the ice's transition cells, where nothing grows.
    map.setResourceByIndex(12,8,WHEAT,1);
    REQUIRE(map.getResource(12,8).type==WHEAT);
    const auto* field=glob2test::awaitGradient(world.game,[&] { return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land); });
    REQUIRE(field);
    std::vector<Uint16> expected(field,field+map.getW()*map.getH());
    map.configureGradientPipeline(0,2);
    auto* bytes=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(bytes);
    map.saveRuntimeState(&output);
    output.flush();
    const std::string saved(bytes->getBuffer(),bytes->getPosition());
    {
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(std::string(saved)));
        map.loadRuntimeState(&input,VERSION_MINOR);
        REQUIRE(map.materialGradients[0][WHEAT][0]);
        CHECK(std::equal(expected.begin(),expected.end(),map.materialGradients[0][WHEAT][0]));
    }
    {
        // Write the actual format141 grammar for this restricted world. The
        // current writer has eight swim classes and newer pipeline sections;
        // labelling its bytes as141 would test a malformed stream instead.
        REQUIRE(world.team->liveBuildings.entries().empty());
        REQUIRE(map.gradientRuntime->pipeline.pendingCount()==0);
        auto* historicalBytes=new GAGCore::MemoryStreamBackend;
        GAGCore::BinaryOutputStream historical(historicalBytes);
        const auto gradient=[&](const Uint16* field) {
            historical.writeUint8(field!=nullptr,"present");
            if(field) GAGCore::PackedArray::write<Uint16>(&historical,map.size,
                [&](size_t i){return field[i];});
        };
        historical.writeEnterSection("mapRuntime");
        historical.writeUint8(map.fogOfWar==map.fogOfWarA.data(),"fogIsA");
        historical.writeUint32(map.topologyGeneration,"topologyGeneration");
        historical.writeEnterSection("cells");
        GAGCore::PackedArray::write<Uint8>(&historical,map.size,
            [&](size_t i){return map.occupancyCells[i].immobileUnit;});
        GAGCore::PackedArray::write<Uint32>(&historical,map.size,
            [&](size_t i){return map.fogOfWarA[i];});
        GAGCore::PackedArray::write<Uint32>(&historical,map.size,
            [&](size_t i){return map.fogOfWarB[i];});
        historical.writeLeaveSection();
        historical.writeEnterSection("teams");
        for(int team=0;team<world.game.teamsCount();++team) {
            historical.writeEnterSection(team);
            historical.writeEnterSection("claims");
            GAGCore::PackedArray::write<Uint16>(&historical,map.size,
                [&](size_t i){return map.clearingAreaClaims[team][i];});
            historical.writeLeaveSection();
            historical.writeEnterSection("swimClasses");
            for(int swim=0;swim<LEGACY_SWIM_CLASS_COUNT;++swim) {
                historical.writeEnterSection(swim);
                historical.writeEnterSection("resources");
                for(int material=0;material<MaterialSlotCount;++material) {
                    historical.writeEnterSection(material);
                    gradient(map.materialGradients[team][material][swim]);
                    historical.writeUint8(0,"updated");
                    historical.writeEnterSection("markets");
                    gradient(map.marketMaterialGradients[team][material][swim]);
                    historical.writeUint8(0 /* legacy visitation byte */,"dirty");
                    historical.writeUint8(0 /* legacy visitation byte */,"updated");
                    historical.writeLeaveSection(2);
                }
                historical.writeLeaveSection();
                historical.writeEnterSection("forbidden");
                gradient(map.forbiddenGradient[team][swim]);
                historical.writeLeaveSection();
                historical.writeEnterSection("guard");
                gradient(map.guardAreasGradient[team][swim]);
                historical.writeUint8(0 /* legacy visitation byte */,"updated");
                historical.writeLeaveSection();
                historical.writeEnterSection("clear");
                gradient(map.clearAreasGradient[team][swim]);
                historical.writeUint8(0 /* legacy visitation byte */,"updated");
                historical.writeLeaveSection(2);
            }
            historical.writeLeaveSection();
            historical.writeEnterSection("buildings");
            historical.writeLeaveSection(2);
        }
        historical.writeLeaveSection();
        historical.writeEnterSection("gradientPipeline");
        historical.writeUint8(map.gradientRuntime->pipeline.delayTicks(),"delay");
        historical.writeUint8(0,"count");
        historical.writeLeaveSection();
        map.saveMaterialRoutingCache(&historical);
        historical.writeLeaveSection();
        historical.flush();
        GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(historicalBytes->takeContents()));
        map.loadRuntimeState(&input,141);
        CHECK(input.isEndOfStream());
        CHECK(map.materialGradients[0][WHEAT][0]==nullptr);
        CHECK(map.gradientRuntime->pipeline.delayTicks()==2);
        const auto* rebuilt=glob2test::awaitGradient(world.game,[&] { return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land); });
        REQUIRE(rebuilt);
        CHECK(std::equal(expected.begin(),expected.end(),rebuilt));
    }
}
}

TEST_SUITE("TerrainHazardRouting")
{
TEST_CASE("workers and warriors reuse escape fields and moving units do not invalidate them")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.terrain=WATER,.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;
    // An ice island whose only cell without an ice corner is (14,8).
    paintVertices(map,7,7,16,10,ICE);
    paintVertices(map,14,8,15,9,GRASS);
    auto* worker=world.addUnit(WORKER,8,8);
    auto* warrior=world.addUnit(WARRIOR,10,8);
    REQUIRE(worker); REQUIRE(warrior);
    REQUIRE(accessClass(worker->movementAccess())==accessClass(warrior->movementAccess()));
    auto& cache=map.gradientRuntime->safety;
    REQUIRE(map.pathfindTerrainSafety(worker));
    CHECK(cache.builds==1);
    REQUIRE(map.pathfindTerrainSafety(warrior));
    CHECK(cache.builds==1); CHECK(cache.fields.size()==1);
    // Units around the worker hold it in place without invalidating the field.
    const int around[8][2]={{7,7},{8,7},{9,7},{7,8},{9,8},{7,9},{8,9},{9,9}};
    for(const auto& cell:around) map.setGroundUnit(cell[0],cell[1],warrior->gid);
    CHECK_FALSE(map.pathfindTerrainSafety(worker));
    CHECK(cache.builds==1);
    for(const auto& cell:around) map.setGroundUnit(cell[0],cell[1],NOGUID);
    REQUIRE(map.pathfindTerrainSafety(worker));
    CHECK(cache.builds==1);
    const int dx=worker->dx,dy=worker->dy;
    cache.fields.clear(); // eviction/load must reproduce the same next step
    REQUIRE(map.pathfindTerrainSafety(worker));
    CHECK(worker->dx==dx); CHECK(worker->dy==dy);
    CHECK(cache.builds==2);

    map.setResourceByIndex(14,8,WOOD,1); // block the only safe cell
    CHECK_FALSE(map.pathfindTerrainSafety(worker));
    const auto negativeBuild=cache.builds;
    for(int n=0;n<20;++n) CHECK_FALSE(map.pathfindTerrainSafety(worker));
    CHECK(cache.builds==negativeBuild);
    map.setNoResource(14,8,1);
    REQUIRE(map.pathfindTerrainSafety(worker));
    CHECK(cache.builds==negativeBuild+1);
    map.setBuilding(14,8,1,1,0);
    CHECK_FALSE(map.pathfindTerrainSafety(worker));
    map.setBuilding(14,8,1,1,NOGUID);
    REQUIRE(map.pathfindTerrainSafety(worker));
    paintVertices(map,14,8,15,9,WATER);
    CHECK_FALSE(map.pathfindTerrainSafety(worker));
    paintVertices(map,14,8,15,9,GRASS);
    REQUIRE(map.pathfindTerrainSafety(worker));
}
TEST_CASE("escape fields separate team permissions swimming and forbidden-area escape")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.terrain=WATER,.teams=2,.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;
    // An ice island whose only cell without an ice corner is (14,8).
    paintVertices(map,7,7,16,10,ICE);
    paintVertices(map,14,8,15,9,GRASS);
    auto* first=world.addUnit(WORKER,8,8,0);
    auto* other=world.addUnit(WORKER,10,8,1);
    REQUIRE(first); REQUIRE(other);
    map.addForbidden(14,8,0);
    CHECK_FALSE(map.pathfindTerrainSafety(first));
    REQUIRE(map.pathfindTerrainSafety(other));
    CHECK(map.gradientRuntime->safety.fields.size()==2);
    map.removeForbidden(14,8,0);
    REQUIRE(map.pathfindTerrainSafety(first));
    const int permittedDx=first->dx,permittedDy=first->dy;
    // A unit already on forbidden ice can leave through further forbidden cells.
    // Paint only the hazardous approach, leaving its safe goal unchanged; the
    // best direction must match the unpainted route, including cheaper mixed edges.
    for(int y=6;y<=10;++y) for(int x=6;x<=13;++x) map.addForbidden(x,y,0);
    REQUIRE(map.pathfindTerrainSafety(first));
    CHECK(first->dx==permittedDx); CHECK(first->dy==permittedDy);
    const int nextX=first->posX+first->dx,nextY=first->posY+first->dy;
    CHECK((first->dx!=0 || first->dy!=0));
    CHECK(map.terrainPropertiesAt(nextX,nextY).groundHealthQ8<0);
    CHECK_FALSE(map.isFreeForGroundUnit(nextX,nextY,false,world.game.teams[0]->me));
    CHECK(map.isFreeForGroundUnitNoForbidden(nextX,nextY,false));
    // Swimming exposes the surrounding safe water as additional escape goals.
    first->setPerformance(SWIM,first->performance[WALK]);
    REQUIRE(map.pathfindTerrainSafety(first));
    CHECK(first->dy!=0);
}
TEST_CASE("flyers share safety across teams and ignore forbidden paint and ground blockers")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.teams=2,.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;
    map.game=nullptr;
    map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"test:air-escape","name":"Air hazard","base":"grass","properties":{"airHealthQ8":-16},"appearance":"ice"}]})");
    map.setGame(&world.game);
    const auto hazard=*map.terrainRegistry().find("test:air-escape");
    for(int y=6;y<=12;++y) for(int x=6;x<=12;++x) map.paintCell(x,y,hazard);
    auto* first=world.addUnit(EXPLORER,8,8,0);
    auto* other=world.addUnit(EXPLORER,9,8,1);
    REQUIRE(first); REQUIRE(other);
    REQUIRE(map.pathfindTerrainSafety(first));
    REQUIRE(map.pathfindTerrainSafety(other));
    CHECK(map.gradientRuntime->safety.builds==1);
    map.setResourceByIndex(15,15,WOOD,1);
    map.setBuilding(15,16,1,1,0);
    REQUIRE(map.pathfindTerrainSafety(first));
    CHECK(map.gradientRuntime->safety.builds==1);
    map.addForbidden(8,8,0);
    REQUIRE(map.pathfindTerrainSafety(first));
    CHECK(map.gradientRuntime->safety.builds==1);
    map.paintCell(8,9,GRASS);
    REQUIRE(map.pathfindTerrainSafety(first));
    CHECK(map.gradientRuntime->safety.builds==2);
    CHECK(first->dx==0); CHECK(first->dy==1);
}
}

TEST_CASE("escape fields respect runtime resource blocking and ignore stock-only edits" *
          doctest::test_suite("TerrainHazardRouting"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;
    map.game=nullptr;
    map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"test:both-hazard","name":"Hazard","base":"grass","properties":{"groundHealthQ8":-16,"airHealthQ8":-16},"appearance":"ice"}]})");
    map.installResourceDefinitions(R"({"schemaVersion":1,"resources":[{"key":"test:canopy","properties":{"blocksGround":false,"blocksAir":true,"persistsWhenEmpty":true},"yields":{"food":{"capacity":9,"initial":3,"consumption":"one"}},"presentation":{"name":"Canopy","sprite":"data/gfx/ressource","minimap":[10,20,30],"levels":[{"stock":0,"variants":[{"frame":1}]}]}}]})");
    map.setGame(&world.game);
    const auto hazard=*map.terrainRegistry().find("test:both-hazard");
    for(int y=0;y<map.getH();++y) for(int x=0;x<map.getW();++x) map.paintCell(x,y,hazard);
    map.paintCell(13,8,GRASS);
    auto* ground=world.addUnit(WORKER,12,8);
    auto* air=world.addUnit(EXPLORER,12,8);
    REQUIRE(ground); REQUIRE(air);
    REQUIRE(map.pathfindTerrainSafety(ground));
    REQUIRE(map.pathfindTerrainSafety(air));
    auto& cache=map.gradientRuntime->safety;
    CHECK(cache.builds==2);
    map.setResource(13,8,*map.resourceRegistry().find("test:canopy"),1);
    REQUIRE(map.pathfindTerrainSafety(ground));
    CHECK(cache.builds==2); // A walk-through deposit does not invalidate ground routes.
    CHECK_FALSE(map.pathfindTerrainSafety(air));
    CHECK(cache.builds==3);
    map.setResourceAmount(map.coordToIndex(13,8),0);
    CHECK_FALSE(map.pathfindTerrainSafety(air));
    CHECK(cache.builds==3); // Empty persistent deposits retain their blocking properties.
    map.setNoResource(13,8,1);
    REQUIRE(map.pathfindTerrainSafety(air));
    CHECK(cache.builds==4);
}

TEST_CASE("escape fields charge destination terrain and choose a cheaper indirect exit" *
          doctest::test_suite("TerrainHazardRouting"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.terrain=WATER,.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;
    map.game=nullptr;
    map.importTerrainDefinitions(R"({"schemaVersion":1,"terrains":[{"key":"test:mild","name":"Mild hazard","base":"grass","properties":{"groundHealthQ8":-1},"appearance":"ice"},{"key":"test:slow-safe","name":"Slow safety","base":"grass","properties":{"groundSpeedQ8":64},"appearance":"grass"}]})");
    map.setGame(&world.game);
    // Author raw corners so the adjacent eastern goal is entirely slow-safe,
    // while north requires one mixed hazardous step before reaching safety.
    // The northeastern/southeastern shoulders retain ice so neither diagonal
    // becomes an unintended cheap safe goal under arithmetic averaging.
    paintVertices(map,8,8,8,9,ICE);
    paintVertices(map,9,8,10,9,*map.terrainRegistry().find("test:slow-safe"));
    paintVertices(map,8,7,9,7,*map.terrainRegistry().find("test:mild"));
    paintVertices(map,8,6,9,6,GRASS);
    map.setVertexTerrain(10,7,ICE);
    paintVertices(map,9,10,10,10,ICE);
    CHECK(map.terrainPropertiesAt(8,7).groundSpeedQ8==176);
    CHECK(map.terrainPropertiesAt(8,7).groundHealthQ8==-2);
    CHECK(map.terrainPropertiesAt(8,6).groundHealthQ8==0); // (-1-1+0+0)/4 truncates toward zero.
    CHECK(map.stepCost(1,0,map.coordToIndex(9,8),0)==40);
    auto* unit=world.addUnit(WORKER,8,8);
    REQUIRE(unit);
    REQUIRE(map.pathfindTerrainSafety(unit));
    CHECK(unit->dx==0); CHECK(unit->dy==-1);
    const auto& costs=map.gradientRuntime->safety.fields.begin()->second.costs;
    // Enter the mixed northern cell (round(15*(256+20*2)/256)=17),
    // then the safe northern cell (10); entering the eastern slow goal costs 40.
    CHECK(costs[map.coordToIndex(8,8)]==27);
    CHECK(costs[map.coordToIndex(8,7)]==10);
}

TEST_CASE("escape cache evicts the least recently used profile and resets generations" *
          doctest::test_suite("TerrainHazardRouting"))
{
    TerrainSafetyCache cache;
    // Simulate a two-field budget without allocating map-sized cell vectors.
    const auto cells=TerrainSafetyCache::MaximumBytes/(2*sizeof(Uint32));
    cache.acquire(1,cells);
    cache.acquire(2,cells);
    cache.acquire(1,cells);
    cache.acquire(3,cells);
    CHECK(cache.fields.contains(1));
    CHECK_FALSE(cache.fields.contains(2));
    CHECK(cache.fields.contains(3));
    cache.invalidate(true,false);
    CHECK(cache.groundGeneration==2); CHECK(cache.airGeneration==1);
    cache.groundGeneration=std::numeric_limits<Uint64>::max();
    cache.invalidate(true,true);
    CHECK(cache.fields.empty());
    CHECK(cache.groundGeneration==1); CHECK(cache.airGeneration==1);
}

TEST_CASE("hazard escape shares the team ratio and remains independent of cache resets" * doctest::test_suite("TerrainProperties"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=world.game.map;
    glob2test::importTerrainBeforeMatch(world.game,R"({"schemaVersion":1,"terrains":[{"key":"mode:hazard","name":"Hazard","base":"grass","properties":{"groundHealthQ8":-13},"appearance":"ice"}]})");
    map.fillTerrain(*map.terrainRegistry().find("mode:hazard"));
    map.paintCell(8,6,GRASS);map.paintCell(9,8,WATER);
    auto* unit=world.addUnit(WORKER,8,8);REQUIRE(unit);
    unit->performance[FLY]=0;unit->performance[WALK]=30;unit->performance[SWIM]=10;
    world.team->updateRoutingRatio();REQUIRE(world.team->routingRatio.selected==6);
    REQUIRE(map.pathfindTerrainSafety(unit));CHECK(unit->dx==0);CHECK(unit->dy==-1);
    auto& cache=map.gradientRuntime->safety;
    REQUIRE(cache.fields.size()==1);CHECK(cache.builds==1);
    unit->performance[WALK]=10;unit->performance[SWIM]=30;
    REQUIRE(map.pathfindTerrainSafety(unit));CHECK(unit->dx==0);CHECK(unit->dy==-1);
    CHECK(cache.fields.size()==1);CHECK(cache.builds==1);
    world.game.stepCounter=512;world.team->updateRoutingRatio();REQUIRE(world.team->routingRatio.selected==1);
    REQUIRE(map.pathfindTerrainSafety(unit));CHECK(unit->dx==1);CHECK(unit->dy==0);
    CHECK(cache.fields.size()==1);CHECK(cache.builds==2);
    cache.reset();
    REQUIRE(map.pathfindTerrainSafety(unit));CHECK(unit->dx==1);CHECK(unit->dy==0);
    CHECK(cache.fields.size()==1);CHECK(cache.builds==1);
}
