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
#include <algorithm>
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
Uint64 airStateHash(const std::vector<Uint32>& state)
{
    Uint64 hash=14695981039346656037ull;
    for (Uint32 value:state) for (unsigned shift=0;shift<32;shift+=8) {
        hash^=(value>>shift)&255;
        hash*=1099511628211ull;
    }
    return hash;
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
TEST_CASE("resource visibility supplier and claim revisions refresh unavailable air guidance before its age limit")
{
    glob2test::HeadlessGlobals globals;
    for (unsigned scenario=0;scenario<4;++scenario) {
        CAPTURE(scenario);
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto& game=world.game; auto& map=game.map;
        game.gameHeader.getExperiments().set(ExperimentId::MarketsV2); game.configureBuildingCatalog();
        configureAir(game,2);
        Key key; key.family=Family::Material; key.material=CHERRY;
        Building* supplier=nullptr;
        Unit* claimant=nullptr;
        const auto goal=map.coordToIndex(12,12);
        if (scenario<2) {
            // Empty cherry trees persist, so changing harvest availability
            // changes resource stamps without changing movement topology.
            map.setResourceByIndex(12,12,CHERRY,1);
            if (scenario==0) { map.setMaterialAmountSlot(goal,CHERRY,0); map.setMapDiscovered(12,12,world.team->me); }
        } else if (scenario==2) {
            supplier=world.addBuilding("market",12,12); REQUIRE(supplier);
            key.withMarkets=true; key.modes=3;
        } else {
            map.setResourceByIndex(12,12,WOOD,1); map.addClearArea(12,12,0);
            auto* unit=world.addUnit(EXPLORER,8,8); claimant=world.addUnit(WORKER,24,24);
            REQUIRE(unit); REQUIRE(claimant);
            claimant->previousClearingArea=Unit::ClearingAreaClaim{12,12}; claimant->previousClearingAreaDistance=1;
            map.setClearingAreaClaimed(12,12,0,claimant->gid);
            key.family=Family::ClearChoice; key.unit=Game::refOf(unit); key.sourceX=8; key.sourceY=8;
            key.epoch=unsigned(unit->activity)|(unsigned(unit->displacement)<<8)|(unsigned(unit->medical)<<16);
        }
        const auto roundTrip=[&] {
            for (bool text:{false,true}) {
                const auto bytes=saveAir(game,text);
                GameGUI resumed; REQUIRE(loadAir(resumed.game,bytes,text));
                CHECK(airState(game)==airState(resumed.game));
                resumed.game.map.finishGradientPipeline();
            }
        };
        map.requestAirField(key);
        roundTrip(); // Cold metadata with normalized current/stale tracker bits.
        const auto old=awaitAir(game,key);
        CHECK(map.queryAirDestination(key,8,8,nullptr)==RouteResult::Unreachable);
        const auto topology=map.topologyGeneration;
        if (scenario==0) map.setMaterialAmountSlot(goal,CHERRY,1);
        else if (scenario==1) map.setMapDiscovered(12,12,world.team->me);
        else if (scenario==2) { supplier->setMaterial(CHERRY,20); supplier->updateCallLists(); }
        else { map.setClearingAreaUnclaimed(12,12,0); claimant->previousClearingArea.reset(); }
        CHECK(map.topologyGeneration==topology);
        roundTrip(); // Published guidance now has stale dependency stamps.
        const auto id=map.gradientRuntime->air.keyIndex.at(key);
        auto& entry=map.gradientRuntime->air.entries.at(id);
        REQUIRE(game.stepCounter-entry.capturedTick<128);
        CHECK(map.queryAirDestination(key,8,8,nullptr)==RouteResult::Unreachable);
        CHECK(std::any_of(map.gradientRuntime->fieldRequests.begin(),map.gradientRuntime->fieldRequests.end(),
            [&](const auto& request) { return request.slot==&entry.slot; }));
        advanceAir(game); // Admission, not the original query, starts K=3.
        roundTrip(); // Save may finish the private result but cannot publish it.
        CHECK(map.lookupAirField(key).values==old.values);
        for (int advance=0;advance<2;++advance) {
            advanceAir(game);
            CHECK(map.lookupAirField(key).values==old.values);
        }
        advanceAir(game);
        CHECK(map.lookupAirField(key).values!=old.values);
        air_guidance::Sample selected;
        REQUIRE(map.queryAirDestination(key,8,8,&selected)==RouteResult::Reachable);
        CHECK(selected.x==12); CHECK(selected.y==12);
        if (supplier) CHECK(selected.supplier==Game::refOf(supplier));
        roundTrip();
        map.finishGradientPipeline();
    }
}

TEST_CASE("cold demand beyond the plane budget remains uncharged FIFO metadata and completes on workers")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game; auto& map=game.map;
    configureAir(game,2); map.addGuardArea(20,20,0);
    std::vector<Key> keys;
    GradientExecutionAudit audit;
    for (unsigned i=0;i<200;++i) {
        Key key; key.family=Family::Guard; key.epoch=i;
        keys.push_back(key);
        CHECK(map.requestAirField(key)==FieldReadiness::Pending);
    }
    auto& runtime=map.gradientRuntime->air;
    REQUIRE(runtime.entries.size()==keys.size());
    REQUIRE(map.gradientRuntime->fieldRequests.size()==keys.size());
    for (const auto& [id,entry]:runtime.entries) { CHECK_FALSE(entry.charged); CHECK_FALSE(entry.values); }
    CHECK(map.gradientPipelineStatus().pending==0);
    CHECK(audit.totals().owner==0); CHECK(audit.totals().other==0);
    std::vector<bool> seen(keys.size());
    unsigned next=0;
    for (unsigned boundary=0;next<keys.size() && boundary<512;++boundary) {
        advanceAir(game);
        for (unsigned i=0;i<keys.size();++i) {
            if (map.lookupAirField(keys[i]).readiness!=FieldReadiness::Ready) continue;
            if (!seen[i]) { CHECK(i==next); ++next; seen[i]=true; }
            // Give every publication its consumer opportunity, so later cold
            // demand can reuse its plane charge without evicting unread work.
            CHECK(map.queryAirDestination(keys[i],8,8,nullptr)==RouteResult::Reachable);
        }
    }
    CHECK(next==keys.size());
    map.finishGradientPipeline();
    if (GAGCore::ThreadSupport::available) CHECK(audit.totals().owner==0);
}

TEST_CASE("a cold FIFO head defers without a deadline then evicts queued warm allocations without moving their entries")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.header=true});
    auto& game=world.game; auto& map=game.map;
    configureAir(game,2); map.addGuardArea(20,20,0);
    Key first; first.family=Family::Guard;
    awaitAir(game,first);
    auto& runtime=map.gradientRuntime->air;
    const auto prototype=runtime.entries.begin()->second;
    Uint16** coldSlot=nullptr;
    Key cold;
    // Fill logical charges using one immutable fixture generation. Sharing its
    // physical storage keeps this a small correctness test; admission still
    // charges every distinct field exactly as it would after real publication.
    for (unsigned i=0;i<200;++i) {
        Key key=first; key.epoch=1000+i;
        map.requestAirField(key);
        auto found=std::find_if(runtime.entries.begin(),runtime.entries.end(),[&](const auto& item) { return item.second.key==key; });
        REQUIRE(found!=runtime.entries.end());
        auto& entry=found->second;
        if (!runtime.tryAdmit(map,&entry.slot)) { coldSlot=&entry.slot; cold=key; break; }
        entry.values=prototype.values; entry.output=prototype.output; entry.slot=const_cast<Uint16*>(entry.values.get());
        entry.unread=true; entry.publishedTick=game.stepCounter;
        std::erase_if(map.gradientRuntime->fieldRequests,[&](const auto& request) { return request.slot==&entry.slot; });
    }
    REQUIRE(coldSlot!=nullptr);
    REQUIRE(map.gradientRuntime->fieldRequests.size()==1);
    CHECK(map.gradientRuntime->fieldRequests.front().slot==coldSlot);
    CHECK_FALSE(runtime.tryAdmit(map,coldSlot));
    unsigned assigned=0;
    map.gradientRuntime->pipeline.visitLogical([&](auto* slot,unsigned,bool) { assigned+=slot==coldSlot; });
    CHECK(assigned==0); // Budget waiting has not reserved a publication deadline.
    std::vector<Uint16**> warmSlots;
    for (auto& [id,entry]:runtime.entries) if (entry.values) {
        entry.unread=false;
        warmSlots.push_back(&entry.slot);
        map.requestAirField(entry.key,true);
    }
    advanceAir(game);
    CHECK(runtime.entries.size()==warmSlots.size()+1);
    assigned=0;
    map.gradientRuntime->pipeline.visitLogical([&](auto* slot,unsigned remaining,bool) {
        if (slot==coldSlot) { ++assigned; CHECK(remaining==3); }
    });
    CHECK(assigned==1);
    CHECK(map.gradientRuntime->fieldRequests.front().slot==warmSlots.front());
    CHECK(*warmSlots.front()==nullptr); // Allocation evicted; stable metadata survives.
    for (int advance=0;advance<3;++advance) { map.advanceGradientPipeline(); ++game.stepCounter; }
    CHECK(map.lookupAirField(cold).readiness==FieldReadiness::Ready);
    map.finishGradientPipeline();
}

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
            std::shared_ptr<Uint16[]> values(new Uint16[map.cellCount()]);
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
        // Add demand before retiring that boundary. Metadata enrollment and
        // retirement accounting must agree even though only the original run
        // has an external lease to the replaced generation.
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
        trace<<"boundary\tmaterial_ready\tguard_ready\tchecksum_hash\n";
        for (unsigned boundary=0;boundary<16;++boundary) {
            advanceAir(game);
            trajectory.push_back(airState(game));
            trace<<game.stepCounter<<'\t'<<(map.lookupAirField(material).readiness==FieldReadiness::Ready)
                <<'\t'<<(map.lookupAirField(guard).readiness==FieldReadiness::Ready)
                <<'\t'<<airStateHash(trajectory.back())<<'\n';
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
        using AuditKind=GradientExecutionAudit::Kind;
        const std::array categories{
            std::pair{"AirFieldInitialization",AuditKind::AirFieldInitialization},
            std::pair{"AirFieldExpansion",AuditKind::AirFieldExpansion},
            std::pair{"AirTraversalPlane",AuditKind::AirTraversalPlane},
            std::pair{"AirGoalEnumeration",AuditKind::AirGoalEnumeration},
            std::pair{"ImmediateAirSearch",AuditKind::ImmediateAirSearch},
            std::pair{"TerrainSafety",AuditKind::TerrainSafety},
            std::pair{"LegacyAirDistanceField",AuditKind::AirSearch}};
        std::ostringstream kernelTrace;
        kernelTrace<<"category\towner\tother\n";
        for (const auto [name,kind]:categories) {
            const auto counts=audit.counts(kind);
            kernelTrace<<name<<'\t'<<counts.owner<<'\t'<<counts.other<<'\n';
        }
        glob2test::writeFile(glob2test::artifactDir()/("air-participants-"+std::to_string(participants)+"-kernel-entries.tsv"),kernelTrace.str());
        const auto owner=audit.counts(GradientExecutionAudit::Kind::AirFieldInitialization).owner
            +audit.counts(GradientExecutionAudit::Kind::AirFieldExpansion).owner
            +audit.counts(GradientExecutionAudit::Kind::AirTraversalPlane).owner
            +audit.counts(GradientExecutionAudit::Kind::AirGoalEnumeration).owner;
        if (participants>1 && GAGCore::ThreadSupport::available) CHECK(owner==0);
        else CHECK(owner>0);
    }
}
}
