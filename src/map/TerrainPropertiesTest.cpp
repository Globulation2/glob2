// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "TerrainLine.h"
#include "TerrainPresentation.h"
#include "TerrainExperiments.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "Version.h"
#include "gradient/GradientRuntime.h"
#include "MapInternal.h"
#include <memory>
#include <climits>

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
    map.tiles[map.coordToIndex(9,8)].terrain=256;
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
