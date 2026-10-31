// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include <nlohmann/json.hpp>
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <algorithm>
#include <iostream>
#include "GlobalContainer.h"
#include "Game.h"
#include "Map.h"
#include "MapInternal.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "UnitCatalog.h"
#include "Player.h"
#include "Order.h"
#include "Brush.h"
#include "BuildingGradientSearch.h"
#include "gradient/GradientRuntime.h"
#include "gradient/SnapshotGradient.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <memory>
#include <vector>
#include "gradient/ResourceGradientProbe.h"
#include <fstream>

namespace
{
struct Fixture {
    Game game;
    explicit Fixture(int lastTeam):game(nullptr) {
        game.map.setSize(6,6,GRASS); game.map.setGame(&game);
        for (int i=0;i<=lastTeam;++i) { game.addTeam(); game.teams[i]->race.loadDefault(); }
        game.gameHeader.setResourceGrowthDisabled(true);
        game.map.configureGradientPipeline(0,8);
    }
};
void player(Fixture& f) {
    f.game.players[0]=new Player(); f.game.players[0]->setTeam(f.game.teams[0]);
    f.game.gameHeader.setNumberOfPlayers(1);
}
void edit(Fixture& f,int x,int y,bool add,bool mixed=false) {
    auto order=std::make_shared<OrderAlterForbidden>(); order->sender=0; order->teamNumber=0;
    order->type=add?BrushTool::MODE_ADD:BrushTool::MODE_DEL;
    order->centerX=x; order->centerY=y; order->minX=order->minY=0;
    order->maxX=mixed?2:1; order->maxY=1; order->mask.resize(mixed?2:1,true);
    f.game.executeOrder(order,0);
}
// Complete optional maintenance for the old tick, then publish/capture the new
// completed-world boundary. No helper finishes a live field synchronously.
void boundary(Fixture& f,bool maintenance=false) {
    auto& map=f.game.map;
    if (maintenance) map.syncStep(f.game.stepCounter);
    ++f.game.stepCounter;
    map.advanceGradientPipeline();
    map.stagePeriodicGradientPreparation(); glob2test::prepareGradientBoundary(f.game);
}
template<class Predicate> void await(Fixture& f,Predicate ready,bool maintenance=false,unsigned limit=4096) {
    for (unsigned i=0;i<limit && !ready();++i) boundary(f,maintenance);
    REQUIRE(ready());
}
void prepareMovementTerrain(Map& map,int swim) {
    if (swim==WATER_ONLY_CLASS)
        for (int y=0;y<map.getH();++y) for (int x=0;x<map.getW();++x)
            if (map.getBuilding(x,y)==NOGBID) map.paintCell(x,y,WATER);
}
std::vector<Uint16> referenceArea(Fixture& f,gradient_preparation::Kind kind,int team,int swim) {
    auto snapshot=SimulationSnapshot::capture(f.game,SimulationSnapshot::captureCatalog(f.game),SimulationSnapshot::Simulation);
    gradient_preparation::Request request; request.kind=kind; request.team=team; request.swim=swim;
    request.allies=f.game.teams[team]->allies; request.terrainBuckets=f.game.map.terrainQueueBuckets();
    request.crowding=f.game.gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing);
    std::vector<Uint16> field(f.game.map.cellCount());
    gradient_preparation::CrowdingScratch seed; GradientWorkspace propagation;
    gradient_preparation::seed(request,snapshot,field.data(),seed);
    gradient_preparation::propagateCPU(request,snapshot,field.data(),propagation);
    return field;
}
std::vector<Uint16> referenceBuilding(Fixture& f,Building* building,int swim) {
    auto snapshot=SimulationSnapshot::capture(f.game,SimulationSnapshot::captureCatalog(f.game),SimulationSnapshot::Simulation);
    const auto seed=gradient_preparation::captureBuildingSeed(*building,swim,BuildingRoute::Automatic);
    const auto inputs=BuildingGradientSearch::Inputs::of(f.game.map,swim);
    std::vector<Uint16> field(f.game.map.cellCount()); BuildingGradientSearch search;
    gradient_preparation::buildBuilding(seed,snapshot,inputs,field.data(),search,gradient_kernel::COST_LIMIT);
    return field;
}
void enableWaterOnly(Fixture& f) {
    auto definitions=nlohmann::json::parse(f.game.unitCatalog().serialize());
    auto swimmer=definitions["units"][WORKER]; swimmer["key"]="fixture:water-only";
    swimmer["behaviors"]["walk"]=false; swimmer["behaviors"]["fly"]=false; swimmer["behaviors"]["swim"]=true;
    for (auto& level:swimmer["levels"]) level["performance"][SWIM]=20;
    definitions["units"].push_back(swimmer);
    f.game.gameHeader.setUnitCatalog(UnitCatalog::deserialize(definitions.dump()));
    f.game.configureBuildingCatalog();
    REQUIRE(f.game.hasWaterOnlyUnits());
}
}

TEST_SUITE("MapGradientInvalidation")
{
TEST_CASE("forbidden painting delays complete routes across passable resources") {
    glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings=true});
    for (int swim=0;swim<SWIM_CLASS_COUNT;++swim) {
        CAPTURE(swim);
        Fixture f(0); player(f); auto& map=f.game.map;
        auto* building=f.game.addBuilding(8,8,globals->buildingsTypes.getTypeNum("inn",0,false),0);
        REQUIRE(building); prepareMovementTerrain(map,swim);
        const auto cottonId=*map.resourceRegistry().find("cotton");
        auto definition=nlohmann::json::parse(map.resourceRegistry().serialize())["resources"][resourceIndex(cottonId)];
        definition["key"]="test-passable-fabric"; definition["properties"]["blocksGround"]=false;
        definition["properties"]["clearable"]=true;
        map.installResourceDefinitions(nlohmann::json{{"schemaVersion",1},{"resources",nlohmann::json::array({definition})}}.dump());
        map.setResource(20,20,*map.resourceRegistry().find("test-passable-fabric"),1);
        REQUIRE_FALSE(map.resourceBlocksGround(map.coordToIndex(20,20)));
        REQUIRE(glob2test::awaitBuildingGradient(f.game,building,swim)!=nullptr);
        const auto* published=map.buildingGradient(building,swim);
        const std::vector<Uint16> old(published,published+map.cellCount());
        edit(f,20,20,true);
        REQUIRE(map.requestBuildingRefresh(building,building->routeSlot(swim,BuildingRoute::Automatic)));
        REQUIRE(std::equal(old.begin(),old.end(),map.buildingGradient(building,swim)));
        // A warm refresh cannot publish during the first K-1 advances,
        // including when the serial executor has already finished it.
        for (unsigned advance=1;advance<map.gradientPipelineStatus().delay;++advance) {
            boundary(f);
            REQUIRE(std::equal(old.begin(),old.end(),map.buildingGradient(building,swim)));
        }
        await(f,[&]{return map.buildingGradient(building,swim)[map.coordToIndex(20,20)]==GRADIENT_FORBIDDEN;});
        const auto expected=referenceBuilding(f,building,swim);
        CHECK(std::equal(expected.begin(),expected.end(),map.buildingGradient(building,swim)));
    }
}

TEST_CASE("forbidden edits preserve published generations until delayed refresh") {
    glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings=true});
    for (int swim=0;swim<SWIM_CLASS_COUNT;++swim)
    for (int resource:{WHEAT,WOOD,NO_RES_TYPE}) for (bool selected:{false,true})
    for (bool mixed:{false,true}) for (bool add:{false,true}) {
        CAPTURE(swim); CAPTURE(resource); CAPTURE(selected); CAPTURE(mixed); CAPTURE(add);
        Fixture f(1); player(f); auto& map=f.game.map;
        std::vector<Building*> buildings;
        for (int team=0;team<2;++team) for (const char* kind:{"inn","warflag","explorationflag","clearingflag"}) {
            auto* b=f.game.addBuilding(team?42:8,8+int(buildings.size()%4)*10,globals->buildingsTypes.getTypeNum(kind,0,false),team);
            REQUIRE(b); b->unitStayRange=32; b->setClearsMaterial(WHEAT,true); b->setClearsMaterial(WOOD,true);
            buildings.push_back(b);
        }
        prepareMovementTerrain(map,swim);
        if (resource!=NO_RES_TYPE) map.setResourceByIndex(20,20,resource,1);
        map.setResourceByIndex(25,24,WHEAT,1); map.addGuardArea(26,26,0);
        if (selected) map.addClearArea(20,20,0);
        if (!add) { map.addForbidden(20,20,0); if (mixed) map.addForbidden(21,20,0); }
        for (auto* b:buildings) map.buildingGradient(b,swim);
        map.getForbiddenGradient(0,swim); map.getGuardAreasGradient(0,swim); map.getClearAreasGradient(0,swim);
        await(f,[&] {
            return std::all_of(buildings.begin(),buildings.end(),[&](auto* b){return map.buildingGradient(b,swim)!=nullptr;})
                && map.lookupForbiddenGradient(0,swim) && map.lookupGuardAreasGradient(0,swim) && map.lookupClearAreasGradient(0,swim);
        });
        std::vector<std::vector<Uint16>> before;
        for (auto* b:buildings) {
            const auto* field=map.buildingGradient(b,swim); before.emplace_back(field,field+map.cellCount());
            const auto slot=b->routeSlot(swim,BuildingRoute::Automatic);
            CHECK((!b->globalGradientSearch[slot] || b->globalGradientSearch[slot]->complete()));
        }
        const auto* forbidden=map.lookupForbiddenGradient(0,swim);
        const std::vector<Uint16> oldForbidden(forbidden,forbidden+map.cellCount());
        edit(f,20,20,add,mixed);
        for (unsigned i=0;i<buildings.size();++i)
            CHECK(std::equal(before[i].begin(),before[i].end(),map.buildingGradient(buildings[i],swim)));
        CHECK(std::equal(oldForbidden.begin(),oldForbidden.end(),map.lookupForbiddenGradient(0,swim)));
        // Repeating the exact brush is a no-op even while a refresh is pending.
        const auto generation=map.topologyGeneration; edit(f,20,20,add,mixed);
        CHECK(map.topologyGeneration==generation);
        for (auto* b:buildings) REQUIRE(map.requestBuildingRefresh(b,b->routeSlot(swim,BuildingRoute::Automatic)));
        map.updateTeamAreaGradients(0);
        std::vector<std::vector<Uint16>> expected;
        for (auto* b:buildings) expected.push_back(referenceBuilding(f,b,swim));
        const auto forbiddenExpected=referenceArea(f,gradient_preparation::Kind::Forbidden,0,swim);
        const auto guardExpected=referenceArea(f,gradient_preparation::Kind::Guard,0,swim);
        const auto clearExpected=referenceArea(f,gradient_preparation::Kind::Clear,0,swim);
        await(f,[&] {
            for (unsigned i=0;i<buildings.size();++i)
                if (!std::equal(expected[i].begin(),expected[i].end(),map.buildingGradient(buildings[i],swim))) return false;
            return std::equal(forbiddenExpected.begin(),forbiddenExpected.end(),map.lookupForbiddenGradient(0,swim))
                && std::equal(guardExpected.begin(),guardExpected.end(),map.lookupGuardAreasGradient(0,swim))
                && std::equal(clearExpected.begin(),clearExpected.end(),map.lookupClearAreasGradient(0,swim));
        });
        // Team 1's restrictions did not change; its independently refreshed
        // fields must retain identical complete contents.
        for (unsigned i=4;i<buildings.size();++i) CHECK(expected[i]==before[i]);
    }
}

TEST_CASE("explicit research demanded escape refresh publishes complete fields after every seed transition") {
    glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings=true});
    for (int mutation=0;mutation<12;++mutation) for (int swim=0;swim<SWIM_CLASS_COUNT;++swim) {
        CAPTURE(swim); CAPTURE(mutation);
        Fixture f(1); player(f); auto& map=f.game.map;
        if (swim==WATER_ONLY_CLASS) { enableWaterOnly(f); prepareMovementTerrain(map,swim); }
        for (int y=16;y<25;++y) for (int x=16;x<25;++x) map.addForbidden(x,y,0);
        map.setResourceByIndex(20,20,WHEAT,1); map.setResourceAmount(map.coordToIndex(20,20),3);
        if (mutation==4) map.markImmobileUnit(21,20,0);
        if (mutation==6) map.setBuilding(21,20,1,1,42);
        if (mutation==8) map.paintCell(21,20,WATER);
        map.getForbiddenGradient(0,swim);
        await(f,[&]{return map.lookupForbiddenGradient(0,swim)!=nullptr;});
        const auto* field=map.lookupForbiddenGradient(0,swim);
        const std::vector<Uint16> old(field,field+map.cellCount());
        switch (mutation) {
        case 0:map.setResourceAmount(map.coordToIndex(20,20),1); map.decResource(20,20); break;
        case 1:map.incResourceByIndex(21,20,WHEAT,0); break;
        case 2:map.decResource(20,20); break;
        case 3:map.markImmobileUnit(21,20,0); break;
        case 4:map.clearImmobileUnit(21,20); break;
        case 5:map.setBuilding(21,20,1,1,42); break;
        case 6:map.setBuilding(21,20,1,1,NOGBID); break;
        case 7:map.paintCell(21,20,WATER); break;
        case 8:map.paintCell(21,20,GRASS); break;
        // Direct point edits exercise target metadata; explicit brush recovery
        // is covered separately by the forbidden-edit continuity test.
        case 9:map.addForbidden(25,20,0); break;
        case 10:map.removeForbidden(20,20,0); break;
        case 11:{auto r=map.getResource(20,20); r.type=WOOD; map.replaceResource(20,20,r); break;}
        }
        const auto expected=referenceArea(f,gradient_preparation::Kind::Forbidden,0,swim);
        CHECK(std::equal(old.begin(),old.end(),map.lookupForbiddenGradient(0,swim)));
        const auto jobs=map.gradientPipelineStatus().jobs;
        if((mutation==7 && swim!=WATER_ONLY_CLASS) || mutation==8) {
            // A real terrain paint explicitly invalidates warm route fields
            // (WATER over the water-only fixture is a no-op). That
            // recovery is retained; it is not arbitrary periodic maintenance.
            await(f,[&]{return map.gradientPipelineStatus().jobs==jobs+1 && map.gradientPipelineStatus().pending==0;});
            CHECK(std::equal(expected.begin(),expected.end(),map.lookupForbiddenGradient(0,swim)));
            for(unsigned idle=0;idle<16;++idle)boundary(f,true);
            CHECK(map.gradientPipelineStatus().jobs==jobs+1);
            continue;
        }
        // Other point edits alone do not revive the removed periodic round. This
        // untrained fixture declares research behavior before demanding work.
        map.gradientRuntime->exploration=std::make_unique<ResourceGradientExploration>();
        map.gradientRuntime->exploration->intervals={8};
        for(unsigned idle=0;idle<AdaptiveGradientState::MinimumInterval;++idle)boundary(f,true);
        CHECK(map.gradientPipelineStatus().jobs==jobs);
        int dx=0,dy=0;map.queryForbiddenStep(nullptr,0,swim,20,20,&dx,&dy);
        boundary(f,true);
        CHECK(map.gradientPipelineStatus().jobs==jobs+1);
        CHECK(std::equal(old.begin(),old.end(),map.lookupForbiddenGradient(0,swim)));
        for (unsigned i=0;i<map.gradientPipelineStatus().delay;++i) boundary(f);
        CHECK(std::equal(expected.begin(),expected.end(),map.lookupForbiddenGradient(0,swim)));
    }
}

TEST_CASE("escape guidance recovers a depleted exit and does not rebuild on repeated blocked reads") {
    glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings=true});
    Fixture f(1); player(f); auto& map=f.game.map;
    // This is a reachability/recovery fixture, not a trained-model acceptance
    // test. Only explicit research demand supplies the warm policy here.
    map.gradientRuntime->exploration=std::make_unique<ResourceGradientExploration>();
    map.gradientRuntime->exploration->intervals={8};
    map.addForbidden(20,20,0);
    for (int y=19;y<=21;++y) for (int x=19;x<=21;++x)
        if (x!=20 || y!=20) map.setResourceByIndex(x,y,WHEAT,1);
    map.getForbiddenGradient(0,0); await(f,[&]{return map.lookupForbiddenGradient(0,0)!=nullptr;});
    const auto jobs=map.gradientPipelineStatus().jobs;
    for (unsigned i=0;i<512;++i) {
        int dx=0,dy=0;
        CHECK(map.queryForbiddenStep(nullptr,0,0,20,20,&dx,&dy)==StepResult::Blocked);
    }
    CHECK(map.gradientPipelineStatus().jobs==jobs);
    map.replaceResource(21,20,Resource{}); map.setResourcesGrow(21,20,false);
    int dx=0,dy=0;
    CHECK(map.queryForbiddenStep(nullptr,0,0,20,20,&dx,&dy)==StepResult::Blocked);
    const unsigned bound=AdaptiveGradientState::MinimumInterval+map.gradientPipelineStatus().delay+2;
    await(f,[&]{return map.queryForbiddenStep(nullptr,0,0,20,20,&dx,&dy)==StepResult::Step;},true,bound);
    CHECK(dx==1); CHECK(dy==0);
}

TEST_CASE("idle escape fields cross tick wrap without maintenance and explicit research demanded fields publish together") {
    glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings=true});
    Fixture f(3);player(f);auto& map=f.game.map;
    map.gradientRuntime->exploration=std::make_unique<ResourceGradientExploration>();map.gradientRuntime->exploration->intervals={8};
    f.game.stepCounter=Uint32((1ULL<<32)-16);
    std::vector<GradientFieldKey> keys;
    for(int team=0;team<4;++team)for(int swim=0;swim<LEGACY_SWIM_CLASS_COUNT;++swim) {
        keys.push_back({GradientFieldFamily::Forbidden,Uint8(team),Uint8(swim)});
        map.getForbiddenGradient(team,swim);
    }
    await(f,[&]{
        for(auto key:keys)if(!map.lookupForbiddenGradient(key.team,key.swim))return false;
        return true;
    });
    const auto jobs=map.gradientPipelineStatus().jobs;
    std::vector<const Uint16*> fields;
    for(auto key:keys)fields.push_back(map.lookupForbiddenGradient(key.team,key.swim));
    for(unsigned elapsed=0;elapsed<AdaptiveGradientState::QueryHistoryTicks+16;++elapsed) {
        boundary(f,true);
        CHECK(map.gradientPipelineStatus().jobs==jobs);
        for(unsigned i=0;i<keys.size();++i)CHECK(map.lookupForbiddenGradient(keys[i].team,keys[i].swim)==fields[i]);
    }
    for(auto key:keys)map.gradientMovementDemand(key);
    boundary(f,true);
    CHECK(map.gradientPipelineStatus().jobs==jobs+keys.size());
    for(unsigned elapsed=1;elapsed<8;++elapsed) {
        boundary(f,true);
        for(unsigned i=0;i<keys.size();++i)CHECK(map.lookupForbiddenGradient(keys[i].team,keys[i].swim)==fields[i]);
    }
    boundary(f,true);
    for(unsigned i=0;i<keys.size();++i)CHECK(map.lookupForbiddenGradient(keys[i].team,keys[i].swim)!=fields[i]);
}

}

TEST_SUITE("ZoneGradientDemand") {
TEST_CASE("empty painted areas produce no demand or cold jobs and first paint becomes eligible") {
    glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings=true});
    Fixture f(0);auto& map=f.game.map;
    EntityRandom random;random.seed(123,456);const auto before=random;
    int dx=7,dy=9;
    for(auto kind:{Map::AreaKind::Guard,Map::AreaKind::Clear}) {
        CHECK_FALSE(map.pathfindArea(random,kind,0,0,10,10,&dx,&dy));
        CHECK(map.queryAreaStep(random,kind,0,0,10,10,&dx,&dy)==StepResult::Blocked);
    }
    CHECK(random==before);CHECK(dx==7);CHECK(dy==9);
    CHECK(map.gradientRuntime->adaptive.fields.empty());
    CHECK(map.gradientPipelineStatus().jobs==0);
    map.setAreaMask(map.coordToIndex(12,12),&Tile::guardArea,1);
    CHECK(map.queryAreaStep(random,Map::AreaKind::Guard,0,0,10,10,&dx,&dy)==StepResult::Pending);
    CHECK_FALSE(map.gradientRuntime->adaptive.fields.empty());
    await(f,[&]{return map.lookupGuardAreasGradient(0,0)!=nullptr;});
    const GradientFieldKey key{GradientFieldFamily::Guard,0,0};
    const auto& field=map.gradientRuntime->adaptive.fields.at(key);
    const auto queries=field.queries;const auto outcomes=field.total;
    const auto savedRandom=random;
    map.setAreaMask(map.coordToIndex(12,12),&Tile::guardArea,0);
    const auto fields=map.gradientRuntime->adaptive.fields.size();
    CHECK(map.queryAreaStep(random,Map::AreaKind::Guard,0,0,10,10,&dx,&dy)==StepResult::Blocked);
    CHECK(map.gradientRuntime->adaptive.fields.size()==fields);
    CHECK(field.queries==queries);CHECK(field.total==outcomes);CHECK(random==savedRandom);
    map.setAreaMask(map.coordToIndex(12,12),&Tile::guardArea,1);
    CHECK(map.pathfindArea(random,Map::AreaKind::Guard,0,0,10,10,&dx,&dy));
}
}

TEST_SUITE("ZoneGradientFarmDemand") {
TEST_CASE("farm-only paint enables clearing requests only under the farming rules") {
    glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings=true});
    Fixture f(0);auto& map=f.game.map;
    map.setAreaMask(0,&Tile::farmArea,1);
    CHECK_FALSE(map.hasClearingArea(0));
    EntityRandom random;int dx=0,dy=0;
    CHECK(map.queryAreaStep(random,Map::AreaKind::Clear,0,0,10,10,&dx,&dy)==StepResult::Blocked);
    CHECK(map.gradientRuntime->adaptive.fields.empty());
    f.game.gameHeader.getExperiments().set(ExperimentId::FarmAreas);
    CHECK(map.hasClearingArea(0));
    CHECK(map.queryAreaStep(random,Map::AreaKind::Clear,0,0,10,10,&dx,&dy)==StepResult::Pending);
    map.setAreaMask(0,&Tile::farmArea,0);
    CHECK_FALSE(map.hasClearingArea(0));
    map.setAreaMask(0,&Tile::clearArea,1);
    CHECK(map.hasClearingArea(0));
}
}
