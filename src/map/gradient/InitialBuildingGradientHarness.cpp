// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include "GradientExecutionAudit.h"
#include "BuildingGradientSearch.h"
#include "GradientRuntime.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <sstream>

namespace {
void advance(Game& game) {
    ++game.stepCounter;
    game.map.advanceGradientPipeline();
    game.map.stagePeriodicGradientPreparation();
    glob2test::prepareGradientBoundary(game);
}
std::vector<Uint32> checksum(Game& game) {
    std::vector<Uint32> state,buildings,units;
    game.checkSum(&state,&buildings,&units,true);
    state.erase(state.begin());
    state.insert(state.end(),buildings.begin(),buildings.end());
    state.insert(state.end(),units.begin(),units.end());
    return state;
}
}
TEST_SUITE("InitialBuildingGradient") {
TEST_CASE("cold initialization publishes a resumable frontier at fixed deadlines across worker counts [artifacts]") {
    glob2test::HeadlessGlobals globals;
    std::vector<std::vector<Uint32>> expected;
    for(unsigned participants:{1u,2u,4u,8u}) {
        CAPTURE(participants);
        glob2test::HeadlessGame world({.wDec=8,.hDec=8,.clearImmobile=true,.header=true,.seed=731});
        auto& game=world.game; auto& map=game.map;
        game.gameHeader.setBuildingGradientDelay(3);
        map.configureCompute(participants);
        auto* b=world.addBuilding("inn",4,4);
        GradientExecutionAudit audit;
        CHECK(map.requestBuildingField(b,0,BuildingRoute::Footprint)==FieldReadiness::Pending);
        CHECK(b->globalGradient[0]==nullptr);
        CHECK(map.buildingGradientPipelineStatus().synchronous==0);
        std::ostringstream trace;
        for(unsigned boundary=1;boundary<=8;++boundary) {
            advance(game);
            if(boundary<2) CHECK(b->globalGradient[0]==nullptr);
            else {
                REQUIRE(b->globalGradientSearch[0]);
                CHECK_FALSE(b->globalGradientSearch[0]->complete());
                CHECK_FALSE(b->globalGradientSearch[0]->resolved(120*256+120));
            }
            auto state=checksum(game);
            if(participants==1) expected.push_back(state);
            else CHECK(state==expected[boundary-1]);
            trace<<boundary;
            for(auto value:state) trace<<'\t'<<value;
            trace<<'\n';
        }
        REQUIRE(b->globalGradientSearch[0]);
        auto* search=b->globalGradientSearch[0].get();
        const int before=search->settledCost(), demand=search->requiredCost();
        search->resolveForProposal(90*256+90);
        CHECK(search->settledCost()>before);
        CHECK(search->requiredCost()==demand);
        CHECK_FALSE(search->complete());
        int distance=-1;
        CHECK(map.queryBuildingDistance(b,0,90,90,&distance,BuildingRoute::Footprint)==RouteResult::Reachable);
        CHECK(search->settledCost()>before);
        CHECK_FALSE(search->resolved(120*256+120));
        // Serial participation can execute the same job on the owner; threaded
        // participation must seed on an executor worker.
        if(participants>1 && GAGCore::ThreadSupport::available) {
            CHECK(audit.counts(GradientExecutionAudit::Kind::Seed).owner==0);
            CHECK(audit.counts(GradientExecutionAudit::Kind::Seed).other==1);
        }
        glob2test::writeFile(glob2test::artifactDir()/("participants-"+std::to_string(participants)+".tsv"),trace.str());
    }
}
TEST_CASE("queued and privately completed cold initialization survives binary and text saves [artifacts]") {
    glob2test::HeadlessGlobals globals;
    for(bool text:{false,true}) for(unsigned checkpoint:{0u,1u,2u,5u}) {
        CAPTURE(text);CAPTURE(checkpoint);
        glob2test::HeadlessGame world({.wDec=7,.hDec=7,.clearImmobile=true,.header=true,.seed=731});
        auto& game=world.game;auto& map=game.map;
        game.gameHeader.setBuildingGradientDelay(3);map.configureCompute(2);
        auto* b=world.addBuilding("inn",4,4);
        REQUIRE(map.requestBuildingField(b,0,BuildingRoute::Footprint)==FieldReadiness::Pending);
        for(unsigned i=0;i<checkpoint;++i)advance(game);
        auto* storage=new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> output(text
            ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(storage))
            : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(storage)));
        game.save(output.get(),false,"cold building gradient");output->flush();
        const auto bytes=storage->takeContents();
        glob2test::writeFile(glob2test::artifactDir()/(std::string(text?"text-":"binary-")+std::to_string(checkpoint)+(text?".txt":".g2s")),bytes);
        GameGUI resumed;
        auto backend=std::make_unique<GAGCore::MemoryStreamBackend>(bytes.data(),bytes.size());backend->seekFromStart(0);
        std::unique_ptr<GAGCore::InputStream> input(text
            ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(backend.release()))
            : static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(backend.release())));
        REQUIRE(resumed.game.load(input.get()));
        for(unsigned tick=0;tick<16;++tick) {
            CAPTURE(tick);CHECK(checksum(game)==checksum(resumed.game));
            advance(game);advance(resumed.game);
        }
        auto* restored=resumed.game.teams[0]->myBuildings[Building::GIDtoID(b->gid)];
        REQUIRE(restored);REQUIRE(b->globalGradient[0]);REQUIRE(restored->globalGradient[0]);
        int d1=-1,d2=-1;
        CHECK(map.queryBuildingDistance(b,0,70,70,&d1,BuildingRoute::Footprint)==RouteResult::Reachable);
        CHECK(resumed.game.map.queryBuildingDistance(restored,0,70,70,&d2,BuildingRoute::Footprint)==RouteResult::Reachable);
        CHECK(d1==d2);
    }
}
TEST_CASE("soft edits retain cold deadlines while a building reset supersedes its private result") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
    auto& game=world.game;auto& map=game.map;
    game.gameHeader.setBuildingGradientDelay(3);map.configureCompute(2);
    auto* b=world.addBuilding("inn",4,4);
    REQUIRE(map.requestBuildingField(b,0,BuildingRoute::Footprint)==FieldReadiness::Pending);
    advance(game);
    const auto epoch=b->refreshEpoch[0];
    for(int tick=0;tick<1;++tick) {
        b->resetPathfindGradients(Building::GradientDrop::Area);
        CHECK(b->refreshEpoch[0]==epoch);
        CHECK(map.requestBuildingField(b,0,BuildingRoute::Footprint)==FieldReadiness::Pending);
        advance(game);
    }
    advance(game);REQUIRE(b->globalGradient[0]);
    b->resetPathfindGradients();
    REQUIRE(map.requestBuildingField(b,0,BuildingRoute::Footprint)==FieldReadiness::Pending);
    advance(game);
    b->resetPathfindGradients();
    for(int tick=0;tick<4;++tick)advance(game);
    CHECK(b->globalGradient[0]==nullptr);
    CHECK(map.buildingGradientPipelineStatus().discarded>=1);
}
TEST_CASE("cold requests beyond the former FIFO limit never initialize synchronously") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.wDec=7,.hDec=7,.clearImmobile=true,.header=true});
    auto& map=world.game.map;world.game.gameHeader.setBuildingGradientDelay(3);map.configureCompute(2);
    for(int i=0;i<70;++i) {
        auto* b=world.addBuilding("inn",(i%10)*8,(i/10)*8);
        REQUIRE(b);CHECK(map.requestBuildingField(b,0,BuildingRoute::Footprint)==FieldReadiness::Pending);
        CHECK(b->globalGradient[0]==nullptr);
    }
    CHECK(map.buildingGradientPipelineStatus().synchronous==0);
    CHECK(map.buildingGradientPipelineStatus().queued==70);
}
}
