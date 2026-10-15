// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientExecutionAudit.h"
#include "AirGradientTypes.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <array>
#include <memory>
#include <sstream>

namespace {
using air_guidance::Key;
using air_guidance::Family;
void advanceAir(Game& game)
{
    game.map.advanceGradientPipeline();
    ++game.stepCounter;
    game.map.stagePeriodicGradientPreparation();
    game.map.preparePendingGradient();
}
air_guidance::Published awaitAir(Game& game,const Key& key)
{
    for (unsigned boundary=0;boundary<256;++boundary) {
        game.map.requestAirField(key);
        const auto field=game.map.lookupAirField(key);
        if (field.readiness==FieldReadiness::Ready) return field;
        advanceAir(game);
    }
    FAIL("Air field did not publish within 256 boundaries");
    return {};
}
std::vector<Uint32> airState(Game& game)
{
    std::vector<Uint32> state,buildings,units;
    game.checkSum(&state,&buildings,&units,true);
    state.erase(state.begin());
    state.insert(state.end(),buildings.begin(),buildings.end());
    state.insert(state.end(),units.begin(),units.end());
    return state;
}
std::string saveAir(Game& game,bool text)
{
    auto* backend=new GAGCore::MemoryStreamBackend;
    std::unique_ptr<GAGCore::OutputStream> output(text
        ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend))
        : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(backend)));
    game.save(output.get(),false,"delayed air fields"); output->flush();
    return backend->takeContents();
}
bool loadAir(Game& game,const std::string& bytes,bool text)
{
    auto backend=std::make_unique<GAGCore::MemoryStreamBackend>();
    backend->write(bytes.data(),bytes.size()); backend->seekFromStart(0);
    if (text) { GAGCore::TextInputStream input(backend.get()); return game.load(&input); }
    GAGCore::BinaryInputStream input(backend.release()); return game.load(&input);
}
void configureAir(Game& game,unsigned participants)
{
    game.map.configureCompute(participants);
    game.map.configureGradientPipeline(participants-1,3);
    game.gameHeader.setResourceGrowthDisabled(true);
}
}

TEST_SUITE("AirGradientPublication")
{
TEST_CASE("queued private and published air outputs preserve binary text continuation at every worker count [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    for (unsigned participants:{1u,2u,4u,8u}) for (bool text:{false,true}) for (unsigned checkpoint:{0u,2u,7u}) {
        CAPTURE(participants); CAPTURE(text); CAPTURE(checkpoint);
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true,.seed=713});
        auto& game=world.game;
        configureAir(game,participants);
        game.map.setResourceByIndex(12,12,WHEAT,1); game.map.addGuardArea(20,20,0);
        Key material; material.family=Family::Material; material.material=WHEAT;
        Key guard; guard.family=Family::Guard;
        game.map.requestAirField(material); game.map.requestAirField(guard);
        for (unsigned boundary=0;boundary<checkpoint;++boundary) advanceAir(game);
        const auto bytes=saveAir(game,text);
        const auto name="air-"+std::string(text ? "text" : "binary")+"-participants-"+std::to_string(participants)
            +"-checkpoint-"+std::to_string(checkpoint);
        glob2test::writeFile(glob2test::artifactDir()/(name+(text ? ".txt" : ".g2s")),bytes);
        GameGUI resumed;
        REQUIRE(loadAir(resumed.game,bytes,text));
        CHECK(airState(game)==airState(resumed.game));
        std::ostringstream trace;
        trace<<"boundary\toriginal_checksum\tresumed_checksum\n";
        for (unsigned boundary=0;boundary<24;++boundary) {
            advanceAir(game); advanceAir(resumed.game);
            const auto original=airState(game),restored=airState(resumed.game);
            CHECK(original==restored);
            const auto hash=[](const auto& state) { Uint32 checksum=2166136261u; for (Uint32 value:state) checksum=(checksum^value)*16777619u; return checksum; };
            trace<<game.stepCounter<<'\t'<<hash(original)<<'\t'<<hash(restored)<<'\n';
        }
        glob2test::writeFile(glob2test::artifactDir()/(name+"-continuation.tsv"),trace.str());
        game.map.finishGradientPipeline(); resumed.game.map.finishGradientPipeline();
    }
}

TEST_CASE("material visibility and y-major goal ties are captured until a delayed refresh")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.teams=2,.clearImmobile=true,.header=true});
    auto& game=world.game; auto& map=game.map;
    configureAir(game,2);
    REQUIRE(map.resourcePropertiesByIndex(CHERRY).visibleToHarvest);
    map.setResourceByIndex(14,16,CHERRY,1); map.setResourceByIndex(18,16,CHERRY,1);
    map.setMapDiscovered(18,16,world.team->me);
    Key key; key.family=Family::Material; key.material=CHERRY;
    air_guidance::Sample chosen;
    CHECK(map.queryAirDestination(key,16,16,&chosen)==RouteResult::Pending);
    const auto first=awaitAir(game,key);
    REQUIRE(first.output); CHECK(first.output->goals.size()==1);
    REQUIRE(map.queryAirDestination(key,16,16,&chosen)==RouteResult::Reachable);
    CHECK(chosen.x==18); CHECK(chosen.y==16); CHECK(chosen.cost==20);
    map.setMapDiscovered(14,16,world.team->me);
    map.requestAirField(key,true);
    CHECK(map.lookupAirField(key).values==first.values);
    for (unsigned boundary=0;boundary<32 && map.lookupAirField(key).values==first.values;++boundary) advanceAir(game);
    REQUIRE(map.lookupAirField(key).values!=first.values);
    REQUIRE(map.queryAirDestination(key,16,16,&chosen)==RouteResult::Reachable);
    CHECK(chosen.x==14); CHECK(chosen.y==16); CHECK(chosen.cost==20);
    // A different team cannot borrow discovery or goals from team zero.
    key.team=1;
    CHECK(map.queryAirDestination(key,16,16,&chosen)==RouteResult::Pending);
    awaitAir(game,key);
    CHECK(map.queryAirDestination(key,16,16,&chosen)==RouteResult::Unreachable);
}

TEST_CASE("supplier modes exclude the consumer and foreign stock without inventing natural sources")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game; auto& map=game.map;
    game.gameHeader.getExperiments().set(ExperimentId::MarketsV2); game.configureBuildingCatalog();
    configureAir(game,2);
    auto* supplier=world.addBuilding("market",12,12);
    auto* foreign=world.addBuilding("market",20,20,0,1);
    REQUIRE(supplier); REQUIRE(foreign);
    supplier->setMaterial(CHERRY,20); supplier->updateCallLists();
    foreign->setMaterial(CHERRY,20); foreign->updateCallLists();
    Key key; key.family=Family::Material; key.material=CHERRY; key.withMarkets=true; key.modes=3;
    air_guidance::Sample chosen;
    CHECK(map.queryAirDestination(key,8,8,&chosen)==RouteResult::Pending);
    awaitAir(game,key);
    REQUIRE(map.queryAirDestination(key,8,8,&chosen)==RouteResult::Reachable);
    CHECK(chosen.supplier==Game::refOf(supplier));
    key.consumer=Game::refOf(supplier);
    key.epoch=(unsigned(supplier->typeNum)<<1)|unsigned(supplier->usesTeamMaterials());
    awaitAir(game,key);
    CHECK(map.queryAirDestination(key,8,8,&chosen)==RouteResult::Unreachable);
    key.consumer={}; key.epoch=0; key.withMarkets=false;
    awaitAir(game,key);
    CHECK(map.queryAirDestination(key,8,8,&chosen)==RouteResult::Unreachable);
}

TEST_CASE("consumer configuration changes invalidate both published and pending supplier fields without slot reuse")
{
    glob2test::HeadlessGlobals globals;
    for (bool publishFirst:{false,true}) {
        CAPTURE(publishFirst);
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
        auto& game=world.game; auto& map=game.map;
        game.gameHeader.getExperiments().set(ExperimentId::MarketsV2); game.configureBuildingCatalog();
        configureAir(game,2);
        auto* supplier=world.addBuilding("market",12,12);
        auto* consumer=world.addBuilding("inn",4,4);
        REQUIRE(supplier); REQUIRE(consumer);
        supplier->setMaterial(CHERRY,20); supplier->updateCallLists();
        Key key; key.family=Family::Material; key.material=CHERRY; key.withMarkets=true; key.modes=3;
        key.consumer=Game::refOf(consumer);
        key.epoch=(unsigned(consumer->typeNum)<<1)|unsigned(consumer->usesTeamMaterials());
        map.requestAirField(key);
        if (publishFirst) awaitAir(game,key);
        else advanceAir(game); // The output owns the old captured configuration.
        const auto identity=Game::refOf(consumer);
        const auto hospital=game.buildingsTypes.getTypeNum("hospital",0,false);
        REQUIRE(hospital!=consumer->typeNum);
        consumer->bindType(hospital);
        CHECK(Game::refOf(consumer)==identity);
        CHECK(map.lookupAirField(key).readiness==FieldReadiness::Pending);
        CHECK(map.queryAirDestination(key,8,8,nullptr)==RouteResult::Pending);
        key.epoch=(unsigned(consumer->typeNum)<<1)|unsigned(consumer->usesTeamMaterials());
        CHECK(map.requestAirField(key)==FieldReadiness::Pending);
        awaitAir(game,key);
        air_guidance::Sample selected;
        REQUIRE(map.queryAirDestination(key,8,8,&selected)==RouteResult::Reachable);
        CHECK(selected.supplier==Game::refOf(supplier));
    }
}

TEST_CASE("guard labels retain scan order and clearing-zone goals honor range material and forbidden paint")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game; auto& map=game.map;
    configureAir(game,2);
    map.addGuardArea(8,4,0); map.addGuardArea(4,8,0);
    Key guard; guard.family=Family::Guard;
    awaitAir(game,guard);
    air_guidance::Sample chosen;
    REQUIRE(map.queryAirDestination(guard,8,8,&chosen)==RouteResult::Reachable);
    CHECK(chosen.x==8); CHECK(chosen.y==4);
    map.setResourceByIndex(8,8,WOOD,1); map.setResourceByIndex(10,8,WOOD,1); map.setResourceByIndex(20,20,WOOD,1);
    map.addForbidden(8,8,0);
    Key clear; clear.family=Family::ClearZone; clear.x=8; clear.y=8; clear.range=3; clear.materialMask=1u<<WOOD;
    const auto field=awaitAir(game,clear);
    REQUIRE(field.output); REQUIRE(field.output->goals.size()==1);
    CHECK(field.output->goals.front().x==10); CHECK(field.output->goals.front().y==8);
    REQUIRE(map.queryAirDestination(clear,12,8,&chosen)==RouteResult::Reachable);
    CHECK(chosen.x==10); CHECK(chosen.y==8);
}

TEST_CASE("private clearing choice searches beyond six claimed nearest goals and supersedes a moved source")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game; auto& map=game.map;
    configureAir(game,2);
    auto* unit=world.addUnit(EXPLORER,8,8); REQUIRE(unit);
    for (unsigned i=0;i<7;++i) {
        map.setResourceByIndex(10+i,8,WOOD,1); map.addClearArea(10+i,8,0);
        if (i<6) {
            auto* claimant=world.addUnit(WORKER,20+i,24); REQUIRE(claimant);
            claimant->previousClearingArea=Unit::ClearingAreaClaim{10+i,8};
            claimant->previousClearingAreaDistance=1;
            map.setClearingAreaClaimed(10+i,8,0,claimant->gid);
        }
    }
    Key key; key.family=Family::ClearChoice; key.unit=Game::refOf(unit); key.sourceX=8; key.sourceY=8;
    key.epoch=unsigned(unit->activity)|(unsigned(unit->displacement)<<8)|(unsigned(unit->medical)<<16);
    CHECK(map.queryAirDestination(key,8,8,nullptr)==RouteResult::Pending);
    const auto field=awaitAir(game,key);
    REQUIRE(field.output); REQUIRE(field.output->choice.available);
    CHECK(field.output->choice.sample.x==16); CHECK(field.output->choice.sample.y==8);
    CHECK(field.output->choice.sample.cost==80);
    unit->posX=9;
    CHECK(map.lookupAirField(key).readiness==FieldReadiness::Pending);
    CHECK(map.queryAirDestination(key,9,8,nullptr)==RouteResult::Pending);
    key.sourceX=9;
    const auto moved=awaitAir(game,key);
    REQUIRE(moved.output->choice.available);
    CHECK(moved.output->choice.sample.x==16); CHECK(moved.output->choice.sample.cost==70);
}

TEST_CASE("air publication and binary text continuations agree with one two four and eight participants [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    std::vector<std::vector<Uint32>> baseline;
    for (unsigned participants:{1u,2u,4u,8u}) {
        CAPTURE(participants);
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true,.seed=713});
        auto& game=world.game; auto& map=game.map;
        configureAir(game,participants);
        map.setResourceByIndex(12,12,WHEAT,1); map.addGuardArea(20,20,0);
        Key material; material.family=Family::Material; material.material=WHEAT;
        Key guard; guard.family=Family::Guard;
        for (unsigned i=0;i<16;++i) CHECK(map.requestAirField(material)==FieldReadiness::Pending);
        map.requestAirField(guard);
        GradientExecutionAudit audit;
        std::vector<std::vector<Uint32>> trajectory;
        std::ostringstream trace;
        trace<<"boundary\tmaterial_ready\tguard_ready\n";
        for (unsigned boundary=0;boundary<16;++boundary) {
            advanceAir(game);
            trajectory.push_back(airState(game));
            trace<<game.stepCounter<<'\t'<<(map.lookupAirField(material).readiness==FieldReadiness::Ready)
                <<'\t'<<(map.lookupAirField(guard).readiness==FieldReadiness::Ready)<<'\n';
            if (boundary<3) CHECK(map.lookupAirField(material).readiness==FieldReadiness::Pending);
            if (boundary==3) CHECK(map.lookupAirField(material).readiness==FieldReadiness::Ready);
        }
        if (baseline.empty()) baseline=trajectory; else CHECK(trajectory==baseline);
        glob2test::writeFile(glob2test::artifactDir()/("air-participants-"+std::to_string(participants)+".tsv"),trace.str());
        for (bool text:{false,true}) {
            const auto bytes=saveAir(game,text);
            glob2test::writeFile(glob2test::artifactDir()/("air-participants-"+std::to_string(participants)+(text ? ".txt" : ".g2s")),bytes);
            GameGUI resumed;
            REQUIRE(loadAir(resumed.game,bytes,text));
            CHECK(airState(game)==airState(resumed.game));
            for (unsigned boundary=0;boundary<16;++boundary) {
                advanceAir(game); advanceAir(resumed.game);
                CHECK(airState(game)==airState(resumed.game));
            }
            resumed.game.map.finishGradientPipeline();
        }
        map.finishGradientPipeline();
        const auto owner=audit.counts(GradientExecutionAudit::Kind::AirFieldInitialization).owner
            +audit.counts(GradientExecutionAudit::Kind::AirFieldExpansion).owner
            +audit.counts(GradientExecutionAudit::Kind::AirTraversalPlane).owner
            +audit.counts(GradientExecutionAudit::Kind::AirGoalEnumeration).owner;
        if (participants>1 && GAGCore::ThreadSupport::available) CHECK(owner==0);
        else CHECK(owner>0);
    }
}
}
