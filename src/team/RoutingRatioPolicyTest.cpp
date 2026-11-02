// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include "Engine.h"
#include "ai/observation/AIWorldView.h"
#include "map/gradient/RoutingRatioStudy.h"
#include "field/GradientPropagation.h"
#include <BinaryStream.h>
#include <StreamBackend.h>

namespace {
Unit* swimmer(glob2test::HeadlessGame& world,int walk,int swim) {
    auto* unit=world.addUnit(WORKER);
    unit->performance[FLY]=0; unit->performance[WALK]=walk; unit->performance[SWIM]=swim;
    return unit;
}
ExperimentSet policy(ExperimentId id) { ExperimentSet result;result.set(id);return result; }
std::vector<Uint32> continuationState(Game& game) {
    std::vector<Uint32> state,buildings,units;
    game.checkSum(&state,&buildings,&units,true);
    state.erase(state.begin()); // Loading records the new save-format version.
    state.insert(state.end(),buildings.begin(),buildings.end());
    state.insert(state.end(),units.begin(),units.end());
    return state;
}
}
TEST_SUITE("RoutingRatioPolicy") {
TEST_CASE("policies are exclusive and survive experiment serialization") {
    auto set=policy(ExperimentId::GradientRatioMode);
    CHECK_THROWS_AS(set.set(ExperimentId::GradientRatioEqual),std::invalid_argument);
    CHECK(ExperimentSet::fromText(set.toText())==set);
}
TEST_CASE("baseline preserves every physical class") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world;
    swimmer(world,30,10); world.team->updateRoutingRatio();
    CHECK_FALSE(world.team->routingRatio.known);
    for(int c=0;c<MOVEMENT_CLASS_COUNT;++c)CHECK(world.game.map.routingClass(0,c)==c);
}
TEST_CASE("mode excludes dead flying and access-only units and preserves ties") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true,.experiments=policy(ExperimentId::GradientRatioMode)});
    auto* slow=swimmer(world,30,10); swimmer(world,30,10);auto* even=swimmer(world,20,20);
    auto* dead=swimmer(world,10,30);dead->isDead=true;
    auto* air=swimmer(world,10,30);air->performance[FLY]=20;
    swimmer(world,0,20);swimmer(world,20,0);
    world.team->updateRoutingRatio();CHECK(world.team->routingRatio.selected==slow->swimClass());
    for(int c=1;c<LEGACY_SWIM_CLASS_COUNT;++c)CHECK(world.game.map.routingClass(0,c)==slow->swimClass());
    for(int c:{0,WATER_ONLY_CLASS,AIR_MOVEMENT_CLASS})CHECK(world.game.map.routingClass(0,c)==c);
    swimmer(world,20,20);world.game.stepCounter=511;world.team->updateRoutingRatio();
    CHECK(world.team->routingRatio.tick==0);
    world.game.stepCounter=512;world.team->updateRoutingRatio();
    CHECK(world.team->routingRatio.selected==slow->swimClass());
    slow->isDead=true; world.game.stepCounter=1024;world.team->updateRoutingRatio();
    CHECK(world.team->routingRatio.selected==even->swimClass());
    // A movement upgrade changes the next boundary's population directly.
    even->performance[WALK]=30;even->performance[SWIM]=10;
    world.game.stepCounter=1536;world.team->updateRoutingRatio();
    CHECK(world.team->routingRatio.selected==6);
}
TEST_CASE("speed modes quantize independently and median retains an even-population tie") {
    glob2test::HeadlessGlobals globals;
    for(auto id:{ExperimentId::GradientSpeedModes,ExperimentId::GradientRatioMedian,ExperimentId::GradientRatioEqual}) {
        glob2test::HeadlessGame world({.loadDefaultRace=true,.header=true,.experiments=policy(id)});
        world.team->updateRoutingRatio();CHECK(world.team->routingRatio.selected==Map::SWIM_CLASS_EVEN);
        swimmer(world,10,30);swimmer(world,30,10);
        world.game.stepCounter=512;world.team->updateRoutingRatio();
        CHECK(world.team->routingRatio.selected==Map::SWIM_CLASS_EVEN);
        if(id==ExperimentId::GradientSpeedModes) {
            CHECK(world.team->routingRatio.walk==10);CHECK(world.team->routingRatio.swim==10);
        }
    }
}
TEST_CASE("material and area requests share selected identities with snapshot AI readers") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true,.header=true,.experiments=policy(ExperimentId::GradientRatioMode)});
    swimmer(world,30,10);world.team->updateRoutingRatio();
    auto& map=world.game.map;REQUIRE(map.incResourceByIndex(24,24,WHEAT,0));
    std::optional<GradientFieldKey> first,second;
    map.getMaterialGradientSlot(0,WHEAT,1,false,nullptr,&first);
    map.getMaterialGradientSlot(0,WHEAT,3,false,nullptr,&second);
    REQUIRE(first);REQUIRE(second);CHECK(first==second);CHECK(first->swim==6);
    map.gradientMovementDemand(*first);map.gradientMovementDemand(*second);
    const auto* material=glob2test::awaitGradient(world.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,1);});
    CHECK(map.getMaterialGradientSlot(0,WHEAT,3)==material);
    const auto* area=glob2test::awaitGradient(world.game,[&]{return map.getForbiddenGradient(0,1);});
    CHECK(map.getForbiddenGradient(0,3)==area);
    auto* inn=world.addBuilding("inn",4,4);
    const auto* building=glob2test::awaitBuildingGradient(world.game,inn,1,BuildingRoute::Footprint);
    REQUIRE(building);CHECK(map.buildingGradient(inn,3,BuildingRoute::Footprint)==building);
    CHECK(inn->globalGradient[inn->routeSlot(1,BuildingRoute::Footprint)]==nullptr);
    const auto* consumer=glob2test::awaitGradient(world.game,[&]{return map.cachedMaterialGradientSlot(inn,WHEAT,1,2);});
    CHECK(map.cachedMaterialGradientSlot(inn,WHEAT,3,2)==consumer);
    AIEngine::AIWorldView view(SimulationSnapshot::capture(world.game,SimulationSnapshot::captureCatalog(world.game)));
    CHECK(view.routingClass(0,1)==6);CHECK(view.routingClass(0,3)==6);CHECK(view.routingClass(0,0)==0);
}
TEST_CASE("allied building visitors reuse the service owner ratio") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.teams=2,.discovered=true,.loadDefaultRace=true,.header=true,.experiments=policy(ExperimentId::GradientRatioMode)});
    auto* unit=swimmer(world,30,10);
    world.game.teams[0]->updateRoutingRatio();
    // An empty service owner selects equal cost; the visitor selects class 6.
    // Visitors still reuse the owner's equal-cost field.
    world.game.teams[1]->updateRoutingRatio();
    auto* inn=world.addBuilding("inn",4,4,0,1);
    RoutingRatioReader visitor(unit);
    const auto* field=glob2test::awaitBuildingGradient(world.game,inn,3,BuildingRoute::Footprint);
    REQUIRE(field);
    CHECK(inn->globalGradient[inn->routeSlot(3,BuildingRoute::Footprint)]==field);
    CHECK(inn->globalGradient[inn->routeSlot(6,BuildingRoute::Footprint)]==nullptr);
}
TEST_CASE("counted and ordinary CPU kernels produce identical fields") {
    struct Restore {
        bool previous=GradientKernelWork::enabled.load();
        ~Restore(){GradientKernelWork::enabled.store(previous);}
    } restore;
    for(int movement:{0,1,3,6}) {
        std::vector<Uint16> counted(1024,GRADIENT_UNREACHABLE),ordinary=counted;
        counted[0]=ordinary[0]=GRADIENT_AT_GOAL;
        GradientWorkspace a,b;GradientKernelWork observed,disabled;
        const auto water=[](std::size_t i){return (i%32)>16;};
        GradientKernelWork::enabled.store(true);
        {
            GradientKernelWork::Scope scope(observed);
            gradient_kernel::propagateFieldCPU(counted.data(),movement,gradient_kernel::COST_LIMIT,{32,32},a,water);
        }
        GradientKernelWork::enabled.store(false);
        {
            GradientKernelWork::Scope scope(disabled);
            gradient_kernel::propagateFieldCPU(ordinary.data(),movement,gradient_kernel::COST_LIMIT,{32,32},b,water);
        }
        CHECK(counted==ordinary);CHECK(observed.expanded==1024);
        CHECK(observed.queued>=observed.expanded);CHECK(disabled.expanded==0);
    }
}
TEST_CASE("private routing diagnostics preserve checksums and live field enrollment") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.experiments=policy(ExperimentId::GradientRatioEqual)});
    auto* unit=swimmer(world,30,10);world.team->updateRoutingRatio();
    REQUIRE(world.game.map.incResourceByIndex(24,24,WHEAT,0));
    const auto before=world.game.checkSum();
    const auto fields=world.game.map.publishedResourceFields().size();
    world.game.map.configureRoutingRatioStudy((glob2test::artifactDir()/"ratio-observer").string(),true);
    {
        RoutingRatioReader reader(unit);
        for(unsigned n=0;n<4096;++n)world.game.map.observeRoutingRatioQuery(0,0,unit->movementClass(),unit->posX,unit->posY,false,WHEAT);
    }
    world.game.map.sampleRoutingRatioStudy();
    CHECK(world.game.checkSum()==before);
    CHECK(world.game.map.publishedResourceFields().size()==fields);
}
TEST_CASE("observers preserve every tick for baseline and all sharing policies") {
    struct Restore {
        bool previous=GradientKernelWork::enabled.load();
        ~Restore(){GradientKernelWork::enabled.store(previous);}
    } restore;
    glob2test::HeadlessGlobals globals;
    for(int id:{-1,int(ExperimentId::GradientRatioMode),int(ExperimentId::GradientSpeedModes),int(ExperimentId::GradientRatioMedian),int(ExperimentId::GradientRatioEqual)}) {
        ExperimentSet experiments;if(id>=0)experiments=policy(ExperimentId(id));
        glob2test::HeadlessGame source({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.experiments=experiments});
        source.addUnit(WORKER,20,20,0,1);source.addUnit(WORKER,21,20,0,2);source.addUnit(WORKER,22,20,0,3);
        source.addBuilding("inn",4,4);REQUIRE(source.game.map.incResourceByIndex(24,24,WHEAT,0));
        source.team->updateRoutingRatio();source.game.map.configureCompute(1);
        auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream out(bytes);
        source.gui.save(&out,"observer equivalence");out.flush();
        GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes->getBuffer(),bytes->getPosition()));in.seekFromStart(0);
        GameGUI restored;REQUIRE(restored.load(&in));restored.game.map.configureCompute(4);
        const auto prefix=(glob2test::artifactDir()/("observer-policy-"+std::to_string(id))).string();
        source.game.map.configureRoutingRatioStudy(prefix,true);
        source.game.map.configureGradientWorkStats(prefix+".work",true,true);
        for(unsigned tick=0;tick<530;++tick) {
            source.game.step(0);restored.game.step(0);
            CHECK(continuationState(source.game)==continuationState(restored.game));
        }
        source.game.map.sampleRoutingRatioStudy();
    }
}
TEST_CASE("saved policy and speed tie history continue across the next selection boundary") {
    glob2test::HeadlessGlobals globals;
    GameHeader header;header.setNumberOfPlayers(1);header.setRandomSeed(123456);
    header.getBasePlayer(0)=BasePlayer(0,"Test",0,BasePlayer::P_LOCAL);
    header.getExperiments().set(ExperimentId::GradientSpeedModes);
    auto map=Engine::loadMapHeader("maps/balanced.map");
    GameGUI source;REQUIRE(source.loadFromHeaders(map,header,true,true));source.game.map.configureCompute(1);
    for(unsigned tick=0;tick<510;++tick)source.game.step(0);
    auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream out(bytes);
    source.save(&out,"routing ratio continuation");out.flush();
    GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(bytes->getBuffer(),bytes->getPosition()));in.seekFromStart(0);
    GameGUI restored;REQUIRE(restored.load(&in));restored.game.map.configureCompute(4);
    for(unsigned tick=0;tick<20;++tick) {
        source.game.step(0);restored.game.step(0);
        CHECK(source.game.teams[0]->routingRatio.selected==restored.game.teams[0]->routingRatio.selected);
        CHECK(source.game.teams[0]->routingRatio.tick==restored.game.teams[0]->routingRatio.tick);
        CHECK(continuationState(source.game)==continuationState(restored.game));
    }
}
}
