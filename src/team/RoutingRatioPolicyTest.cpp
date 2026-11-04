// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include "Engine.h"
#include "Version.h"
#include "ai/observation/AIWorldView.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <sstream>

namespace {
Unit* swimmer(glob2test::HeadlessGame& world,int walk,int swim) {
    auto* unit=world.addUnit(WORKER);
    unit->performance[FLY]=0;unit->performance[WALK]=walk;unit->performance[SWIM]=swim;
    return unit;
}
void boundary(Game& game) {
    ++game.stepCounter;game.map.advanceGradientPipeline();
    game.map.stagePeriodicGradientPreparation();glob2test::prepareGradientBoundary(game);
}
std::vector<Uint32> continuationState(Game& game) {
    std::vector<Uint32> state,buildings,units;game.checkSum(&state,&buildings,&units,true);
    state.erase(state.begin());state.insert(state.end(),buildings.begin(),buildings.end());
    state.insert(state.end(),units.begin(),units.end());return state;
}
}
TEST_SUITE("RoutingRatioPolicy") {
TEST_CASE("most-common costs are enabled by default and access has no ratio") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto* unit=swimmer(world,30,10);world.team->updateRoutingRatio();
    CHECK(world.team->routingRatio.known);CHECK(world.team->routingRatio.selected==6);
    CHECK(unit->movementAccess()==MovementAccess::Amphibious);
    CHECK(unit->performance[WALK]==30);CHECK(unit->performance[SWIM]==10);
    CHECK(world.game.map.preparationCostClass(0,int(MovementAccess::Amphibious))==6);
    CHECK(world.game.map.preparationCostClass(0,int(MovementAccess::Land))==0);
    CHECK(world.game.map.preparationCostClass(0,int(MovementAccess::Water))==WATER_ONLY_CLASS);
    CHECK(world.game.map.preparationCostClass(0,int(MovementAccess::Air))==AIR_MOVEMENT_CLASS);
}
TEST_CASE("mode excludes ineligible units and resolves ties at deterministic boundaries") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    auto* slow=swimmer(world,30,10);swimmer(world,30,10);auto* even=swimmer(world,20,20);
    auto* dead=swimmer(world,10,30);dead->isDead=true;
    auto* air=swimmer(world,10,30);air->performance[FLY]=20;
    swimmer(world,0,20);swimmer(world,20,0);
    world.team->updateRoutingRatio();CHECK(world.team->routingRatio.selected==6);
    swimmer(world,20,20);world.game.stepCounter=511;world.team->updateRoutingRatio();
    CHECK(world.team->routingRatio.tick==0);
    world.game.stepCounter=512;world.team->updateRoutingRatio();CHECK(world.team->routingRatio.selected==6);
    slow->isDead=true;world.game.stepCounter=1024;world.team->updateRoutingRatio();CHECK(world.team->routingRatio.selected==3);
    even->performance[WALK]=30;even->performance[SWIM]=10;
    world.game.stepCounter=1536;world.team->updateRoutingRatio();CHECK(world.team->routingRatio.selected==6);
}
TEST_CASE("empty teams use equal cost and an unresolved tie chooses the lower cost class") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true});
    world.team->updateRoutingRatio();CHECK(world.team->routingRatio.selected==3);
    swimmer(world,10,30);swimmer(world,30,10);
    world.game.stepCounter=512;world.team->updateRoutingRatio();CHECK(world.team->routingRatio.selected==1);
}
TEST_CASE("a ratio change keeps published material and area fields usable until replacement") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true,.header=true});
    auto* unit=swimmer(world,30,10);world.team->updateRoutingRatio();
    auto& map=world.game.map;REQUIRE(map.incResourceByIndex(24,24,WHEAT,0));
    const auto* material=glob2test::awaitGradient(world.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious);});
    const auto* forbidden=glob2test::awaitGradient(world.game,[&]{return map.getForbiddenGradient(0,MovementAccess::Amphibious);});
    REQUIRE(map.publishedCostClass(material)==6);REQUIRE(map.publishedCostClass(forbidden)==6);
    unit->performance[WALK]=10;unit->performance[SWIM]=30;
    world.game.stepCounter=512;world.team->updateRoutingRatio();REQUIRE(world.team->routingRatio.selected==1);
    CHECK(map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious)==material);
    CHECK(map.getForbiddenGradient(0,MovementAccess::Amphibious)==forbidden);
    CHECK(map.publishedCostClass(material)==6);
    map.updateMaterialGradient(0,WHEAT,MovementAccess::Amphibious);
    CHECK(map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious)==material);
    for(unsigned n=0;n<64 && map.publishedCostClass(map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious))!=1;++n)boundary(world.game);
    CHECK(map.publishedCostClass(map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious))==1);
    CHECK(map.getForbiddenGradient(0,MovementAccess::Amphibious)==forbidden);
}
TEST_CASE("in-flight preparation retains captured costs across another team policy change") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true,.header=true});
    swimmer(world,30,10);world.team->updateRoutingRatio();auto& map=world.game.map;
    REQUIRE(map.incResourceByIndex(24,24,WHEAT,0));
    CHECK(map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious)==nullptr);
    boundary(world.game);world.team->routingRatio.selected=1;
    const auto* field=glob2test::awaitGradient(world.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious);});
    CHECK(map.publishedCostClass(field)==6);
}
TEST_CASE("allied building visitors use the owner's stable field and preparation policy") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.teams=2,.discovered=true,.loadDefaultRace=true,.header=true});
    swimmer(world,30,10);world.game.teams[0]->updateRoutingRatio();world.game.teams[1]->updateRoutingRatio();
    auto* inn=world.addBuilding("inn",4,4,0,1);auto& map=world.game.map;
    const auto* field=glob2test::awaitBuildingGradient(world.game,inn,3,BuildingRoute::Footprint);
    CHECK(map.publishedCostClass(field)==3);
    CHECK(inn->globalGradient[inn->routeSlot(6,BuildingRoute::Footprint)]==nullptr);
    world.game.teams[1]->routingRatio.selected=6;
    CHECK(map.lookupBuildingField(inn,MovementAccess::Amphibious,BuildingRoute::Footprint)==field);
    CHECK(map.publishedCostClass(field)==3);
    int distance=0;CHECK(map.buildingAvailable(inn,MovementAccess::Amphibious,8,8,&distance,BuildingRoute::Footprint));
}
TEST_CASE("snapshot AI reads use stable access and retain team preparation costs") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true,.header=true});
    swimmer(world,30,10);world.team->updateRoutingRatio();
    REQUIRE(world.game.map.incResourceByIndex(24,24,WHEAT,0));
    glob2test::awaitGradient(world.game,[&]{return world.game.map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious);});
    AIEngine::AIWorldView view(SimulationSnapshot::capture(world.game,SimulationSnapshot::captureCatalog(world.game)));
    CHECK(view.preparationCostClass(0,int(MovementAccess::Amphibious))==6);
    CHECK_FALSE(view.resourceGradient(0,WHEAT,MovementAccess::Amphibious).empty());
}
TEST_CASE("saved mode and field costs continue through the next boundary with one and four participants") {
    glob2test::HeadlessGlobals globals;
    GameHeader header;header.setNumberOfPlayers(1);header.setRandomSeed(123456);
    header.getBasePlayer(0)=BasePlayer(0,"Test",0,BasePlayer::P_LOCAL);
    auto map=Engine::loadMapHeader("maps/balanced.map");
    GameGUI source;REQUIRE(source.loadFromHeaders(map,header,true,true));source.game.map.configureCompute(1);
    Unit* upgraded=nullptr;
    for(auto* unit:source.game.teams[0]->liveUnits.entries())if(unit->typeNum==WORKER){upgraded=unit;break;}
    REQUIRE(upgraded);upgraded->resetAtLevel(3);
    REQUIRE(upgraded->performance[SWIM]>0);

    for(unsigned tick=0;tick<510;++tick)source.game.step(0);
    auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream out(bytes);
    source.save(&out,"shared routing continuation");out.flush();
    glob2test::writeFile(glob2test::artifactDir()/"modal-routing-510.game",std::string(bytes->getBuffer(),bytes->getPosition()));
    std::ostringstream trace;trace<<"# tick, modal cost, normalized state checksums; verified against restored four-worker execution\n";
    GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes->getBuffer(),bytes->getPosition()));in.seekFromStart(0);
    GameGUI restored;REQUIRE(restored.load(&in));restored.game.map.configureCompute(4);
    for(unsigned tick=0;tick<20;++tick) {
        source.game.step(0);restored.game.step(0);
        CHECK(source.game.teams[0]->routingRatio.selected==restored.game.teams[0]->routingRatio.selected);
        const auto state=continuationState(source.game);
        CHECK(state==continuationState(restored.game));
        trace<<source.game.stepCounter<<' '<<source.game.teams[0]->routingRatio.selected;
        for(auto value:state)trace<<' '<<value;
        trace<<'\n';
    }
    glob2test::writeFile(glob2test::artifactDir()/"modal-routing-continuation.trace",trace.str());
}
TEST_CASE("save preserves distinct serving and in-flight costs across a policy switch") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame source({.discovered=true,.loadDefaultRace=true,.header=true});
    glob2test::HeadlessGame restored({.discovered=true,.loadDefaultRace=true,.header=true});
    auto& map=source.game.map;auto& copy=restored.game.map;
    for(auto* world:{&source,&restored}) {
        REQUIRE(world->game.map.incResourceByIndex(24,24,WHEAT,0));
        world->team->routingRatio.known=true;world->team->routingRatio.selected=6;
    }
    const auto* serving=glob2test::awaitGradient(source.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious);});
    REQUIRE(map.publishedCostClass(serving)==6);
    source.team->routingRatio.selected=1;
    map.updateMaterialGradient(0,WHEAT,MovementAccess::Amphibious);
    for(unsigned n=0;n<64 && !map.gradientPipelineStatus().pending;++n)boundary(source.game);
    REQUIRE(map.gradientPipelineStatus().pending>0);
    source.team->routingRatio.selected=6;
    auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream out(bytes);
    map.saveRuntimeState(&out);out.flush();
    glob2test::writeFile(glob2test::artifactDir()/"serving6-pending1.runtime.bin",std::string(bytes->getBuffer(),bytes->getPosition()));
    restored.game.stepCounter=source.game.stepCounter;
    GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes->getBuffer(),bytes->getPosition()));in.seekFromStart(0);
    copy.loadRuntimeState(&in,VERSION_MINOR);copy.configureCompute(4);
    REQUIRE(copy.publishedCostClass(copy.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious))==6);
    for(unsigned n=0;n<16;++n) {
        boundary(source.game);boundary(restored.game);
        const auto* a=map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious);
        const auto* b=copy.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious);
        REQUIRE(a);REQUIRE(b);CHECK(map.publishedCostClass(a)==copy.publishedCostClass(b));
        CHECK(std::equal(a,a+map.cellCount(),b));
    }
    CHECK(map.publishedCostClass(map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious))==1);
}
TEST_CASE("legacy production caches coalesce without losing their published ratio costs") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame source({.discovered=true,.loadDefaultRace=true,.header=true});
    glob2test::HeadlessGame restored({.discovered=true,.loadDefaultRace=true,.header=true});
    auto& map=source.game.map;auto& copy=restored.game.map;
    REQUIRE(map.incResourceByIndex(24,24,WHEAT,0));REQUIRE(copy.incResourceByIndex(24,24,WHEAT,0));
    source.team->routingRatio.known=true;source.team->routingRatio.selected=6;
    glob2test::awaitGradient(source.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious);});
    const GradientFieldKey legacyKey{GradientFieldFamily::Materials,0,6,WHEAT};
    const GradientFieldKey sharedKey{GradientFieldFamily::Materials,0,3,WHEAT};
    *map.gradientFieldDestination(legacyKey)=std::exchange(*map.gradientFieldDestination(sharedKey),nullptr);
    // Format 200 has identical runtime layout except for the new cost footers.
    struct LegacyWriter:GAGCore::BinaryOutputStream {
        using BinaryOutputStream::BinaryOutputStream;
        void writeUint8(Uint8 value,std::string_view name) override {
            if(name!="preparationCost")BinaryOutputStream::writeUint8(value,name);
        }
    };
    auto* bytes=new GAGCore::MemoryStreamBackend;LegacyWriter out(bytes);map.saveRuntimeState(&out);out.flush();
    restored.game.stepCounter=source.game.stepCounter;
    GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes->getBuffer(),bytes->getPosition()));in.seekFromStart(0);
    copy.loadRuntimeState(&in,200);
    const auto* field=copy.getMaterialGradientSlot(0,WHEAT,MovementAccess::Amphibious);
    REQUIRE(field);CHECK(copy.publishedCostClass(field)==6);CHECK(*copy.gradientFieldDestination(legacyKey)==nullptr);
    CHECK(std::equal(field,field+map.cellCount(),*map.gradientFieldDestination(legacyKey)));
}

}
