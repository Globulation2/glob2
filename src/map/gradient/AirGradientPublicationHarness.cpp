// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientExecutionAudit.h"
#include "AirGradientTypes.h"
#include "GradientRuntime.h"
#include "field/AirGradient.h"
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
std::string saveAirInventory(const AirGradientRuntime& runtime,Map& map,bool text)
{
    auto* backend=new GAGCore::MemoryStreamBackend;
    std::unique_ptr<GAGCore::OutputStream> output(text
        ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend))
        : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(backend)));
    runtime.save(map,output.get()); output->flush(); return backend->takeContents();
}
void loadAirInventory(AirGradientRuntime& runtime,Map& map,const std::string& bytes,bool text)
{
    auto backend=std::make_unique<GAGCore::MemoryStreamBackend>();
    backend->write(bytes.data(),bytes.size()); backend->seekFromStart(0);
    if (text) { GAGCore::TextInputStream input(backend.get()); runtime.load(map,&input,false); }
    else { GAGCore::BinaryInputStream input(backend.release()); runtime.load(map,&input,false); }
}
}

TEST_SUITE("AirGradientPublication")
{
TEST_CASE("published air inventory rejects incomplete metadata malformed goals and invalid bounded values in binary and text")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game; auto& map=game.map;
    configureAir(game,2);
    auto* building=world.addBuilding("inn",4,4); REQUIRE(building);
    Key destination; destination.building=Game::refOf(building); destination.x=4; destination.y=4;
    awaitAir(game,destination);
    map.addGuardArea(20,20,0);
    Key guard; guard.family=Family::Guard;
    awaitAir(game,guard);
    const auto& original=map.gradientRuntime->air;
    for (bool text:{false,true}) for (unsigned malformed=0;malformed<9;++malformed) {
        CAPTURE(text); CAPTURE(malformed);
        // Emit a grammatically complete stream with one semantically invalid
        // component, rather than corrupting array counts and shifting its bytes.
        auto corrupt=original;
        auto entry=std::find_if(corrupt.entries.begin(),corrupt.entries.end(),[&](const auto& item) {
            return item.second.key.family==(malformed==0 || malformed==7 ? Family::Building : Family::Guard);
        });
        REQUIRE(entry!=corrupt.entries.end());
        auto output=std::make_shared<air_guidance::Output>(*entry->second.output);
        entry->second.output=output;
        switch (malformed) {
        case 0: output->entryCosts.clear(); break;
        case 1: output->labels.clear(); break;
        case 2: output->goals.front().x=map.getW(); break;
        case 3: output->goals.front().supplier={NOGBID,1}; break;
        case 4: {
            auto values=std::make_shared<Uint16[]>(map.cellCount());
            std::copy_n(entry->second.values.get(),map.cellCount(),values.get());
            values[0]=2; // A representable integer whose cost exceeds COST_LIMIT.
            entry->second.values=std::move(values);
            break;
        }
        case 5: output->labels[0]=field::air::NoGoal; break;
        case 6: entry->second.key.sourceX=map.getW(); break;
        case 7: entry->second.key.building={}; break;
        case 8: corrupt.retired.push_back({0,1,{},{}}); break; // Missing orphan charge.
        }
        const auto bytes=saveAirInventory(corrupt,map,text);
        AirGradientRuntime loaded;
        CHECK_THROWS_AS(loadAirInventory(loaded,map,bytes,text),std::runtime_error);
    }
    // Empty output metadata is legal for a cold entry, and remains Pending.
    Key cold=guard; cold.epoch=123;
    map.requestAirField(cold);
    for (bool text:{false,true}) {
        AirGradientRuntime loaded;
        CHECK_NOTHROW(loadAirInventory(loaded,map,saveAirInventory(map.gradientRuntime->air,map,text),text));
        CHECK(loaded.lookup(cold).readiness==FieldReadiness::Pending);
    }
    // Exercise the scalar admission guard without allocating an enormous map.
    // Request validation must fail before touching any traversal/output plane.
    const auto actualCells=map.size;
    map.size=AirGradientRuntime::Budget;
    cold.epoch=124;
    CHECK_THROWS_AS(map.requestAirField(cold),std::runtime_error);
    map.size=actualCells;
}

TEST_CASE("retired generation budget attribution survives saves before the next boundary and ignores retained leases")
{
    glob2test::HeadlessGlobals globals;
    for (bool text:{false,true}) {
        CAPTURE(text);
        glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
        auto& game=world.game; auto& map=game.map;
        configureAir(game,2);
        map.addGuardArea(20,20,0);
        Key key; key.family=Family::Guard;
        const auto oldLease=awaitAir(game,key);
        const auto frozen=std::vector<Uint16>(oldLease.values.get(),oldLease.values.get()+map.cellCount());
        map.requestAirField(key,true);
        for (unsigned boundary=0;boundary<32 && map.lookupAirField(key).values==oldLease.values;++boundary) advanceAir(game);
        REQUIRE(map.lookupAirField(key).values!=oldLease.values);
        REQUIRE_FALSE(map.gradientRuntime->air.retired.empty());
        REQUIRE(map.gradientRuntime->air.retired.front().entry!=0);
        air_guidance::Sample selected;
        REQUIRE(map.queryAirDestination(key,8,8,&selected)==RouteResult::Reachable); // Release unread retention.
        const auto bytes=saveAir(game,text);
        GameGUI resumed;
        REQUIRE(loadAir(resumed.game,bytes,text));
        auto& restored=resumed.game.map;
        CHECK(airState(game)==airState(resumed.game));
        const auto& originalRetired=map.gradientRuntime->air.retired;
        const auto& restoredRetired=restored.gradientRuntime->air.retired;
        REQUIRE(restoredRetired.size()==originalRetired.size());
        CHECK(restoredRetired.front().entry==originalRetired.front().entry);
        CHECK(restoredRetired.front().bytes==originalRetired.front().bytes);
        // Add demand before retiring that boundary. Logical budget pressure
        // must make the same decisions, even though only the original run has
        // an external lease to the replaced generation.
        for (unsigned request=0;request<160;++request) {
            Key demand=key; demand.epoch=1000+request;
            CHECK(map.requestAirField(demand)==restored.requestAirField(demand));
        }
        CHECK(airState(game)==airState(resumed.game));
        CHECK(map.gradientRuntime->air.retiredBytes==restored.gradientRuntime->air.retiredBytes);
        CHECK(std::equal(frozen.begin(),frozen.end(),oldLease.values.get()));
        advanceAir(game); advanceAir(resumed.game);
        CHECK(airState(game)==airState(resumed.game));
        map.finishGradientPipeline(); restored.finishGradientPipeline();
    }
}

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
    auto* flag=world.addBuilding("clearingflag",8,8); REQUIRE(flag);
    flag->unitStayRange=3; flag->clearingMaterialMask=1u<<WOOD;
    Key clear; clear.family=Family::ClearZone; clear.building=Game::refOf(flag);
    clear.x=8; clear.y=8; clear.range=3; clear.materialMask=1u<<WOOD;
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
