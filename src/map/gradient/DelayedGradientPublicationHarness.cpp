// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientExecutionAudit.h"
#include "GradientRuntime.h"
#include "BuildingGradientSearch.h"
#include <array>
#include <vector>
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <memory>
#include <sstream>
#include <iomanip>

namespace {
// Advance the same publication/capture boundaries as the game, without unit
// decisions obscuring which cold demand generated a field.
void advanceFields(Game& game)
{
    game.map.advanceGradientPipeline();
    ++game.stepCounter;
    game.map.stagePeriodicGradientPreparation();
    game.map.preparePendingGradient();
}

std::vector<Uint32> normalizedChecksums(Game& game)
{
    std::vector<Uint32> state,buildings,units;
    game.checkSum(&state,&buildings,&units,true);
    state.erase(state.begin()); // The loader records the saved format.
    state.insert(state.end(),buildings.begin(),buildings.end());
    state.insert(state.end(),units.begin(),units.end());
    return state;
}

// Explicit byte order keeps these evidence hashes portable. Assertions below
// still compare the complete arrays/checksums rather than relying on a digest.
template<class Values> std::string evidenceHash(const Values& values)
{
    Uint64 hash=14695981039346656037ull;
    for (const auto value:values)
        for (unsigned shift=0;shift<32;shift+=8) {
            hash^=(Uint32(value)>>shift)&255;
            hash*=1099511628211ull;
        }
    std::ostringstream out;
    out<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;
    return out.str();
}
}

TEST_SUITE("DelayedGradientPublication")
{
TEST_CASE("cold queued and privately completed fields continue through binary and text saves [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    for (bool text : {false,true}) for (unsigned checkpoint : {0u,2u,7u})
    {
        CAPTURE(text); CAPTURE(checkpoint);
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=731});
        auto& game=world.game;
        auto& map=game.map;
        game.gameHeader.setBuildingGradientDelay(3);
        map.configureCompute(2);
        map.configureGradientPipeline(1,3);
        auto* building=world.addBuilding("inn",4,4);
        REQUIRE(building);
        map.setResourceByIndex(20,20,WHEAT,1);
        map.addForbidden(24,24,0);
        map.addGuardArea(22,22,0);
        map.addClearArea(20,20,0);
        map.requestBuildingField(building,0,BuildingRoute::Footprint);
        map.getMaterialGradientSlot(0,WHEAT,0);
        map.getForbiddenGradient(0,0);
        map.getGuardAreasGradient(0,0);
        map.getClearAreasGradient(0,0);
        for (unsigned i=0;i<checkpoint;++i) advanceFields(game);
        if (checkpoint==2) {
            // Two distinct destinations are admitted; two others still wait.
            // Their numeric save entries must not overwrite each other's
            // paths in text streams (binary streams are positional).
            REQUIRE(map.gradientPipelineStatus().jobs==2);
            REQUIRE(map.gradientPipelineStatus().pending==2);
            REQUIRE(map.gradientRuntime->fieldRequests.size()==2);
        }
        GradientExecutionAudit audit;
        auto* storage=new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> output(text
            ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(storage))
            : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(storage)));
        game.save(output.get(),false,"delayed gradient continuation");
        output->flush();
        const auto bytes=storage->takeContents();
        const auto evidenceName=std::string(text ? "text" : "binary")+"-checkpoint-"+std::to_string(checkpoint);
        glob2test::writeFile(glob2test::artifactDir()/(evidenceName+(text ? ".txt" : ".g2s")),bytes);
        GameGUI resumed;
        auto inputBackend=std::make_unique<GAGCore::MemoryStreamBackend>(bytes.data(),bytes.size());
        inputBackend->seekFromStart(0); // TextInputStream parses in its constructor.
        std::unique_ptr<GAGCore::InputStream> input(text
            ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(inputBackend.get()))
            : static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(inputBackend.release())));
        REQUIRE(resumed.game.load(input.get()));
        // Saving can finish private work; loading restores it without kernels.
        if (GAGCore::ThreadSupport::available) CHECK(audit.totals().owner==0);
        std::ostringstream trace;
        trace<<"boundary\toriginal_checksum_hash\tresumed_checksum_hash\n";
        const auto record=[&] {
            const auto original=normalizedChecksums(game), restored=normalizedChecksums(resumed.game);
            trace<<game.stepCounter<<'\t'<<evidenceHash(original)<<'\t'<<evidenceHash(restored)<<'\n';
            REQUIRE(original.size()==restored.size());
            for (size_t component=0;component<original.size();++component)
                CHECK_MESSAGE(original[component]==restored[component],
                    "simulation checksum component ",component," at boundary ",game.stepCounter);
        };
        record();
        for (int tick=0;tick<32;++tick) {
            CAPTURE(tick);
            advanceFields(game);
            advanceFields(resumed.game);
            record();
        }
        glob2test::writeFile(glob2test::artifactDir()/(evidenceName+"-continuation.tsv"),trace.str());
        map.finishGradientPipeline();
        resumed.game.map.finishGradientPipeline();
    }
}

TEST_CASE("cold metadata beyond the former queue limit admits FIFO without owner fallback")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.wDec=6,.hDec=6,.clearImmobile=true,.header=true,.seed=731});
    auto& game=world.game;
    auto& map=game.map;
    game.gameHeader.setBuildingGradientDelay(3);
    map.configureCompute(2);
    map.configureGradientPipeline(1,3);
    std::vector<Building*> buildings;
    std::vector<std::pair<Building*,int>> requested;
    for (int i=0;i<12;++i) {
        auto* b=world.addBuilding("inn",4+12*(i%4),4+12*(i/4));
        REQUIRE(b);
        buildings.push_back(b);
    }
    // Creation prewarms walking footprint routes in building order. Those
    // existing requests retain their FIFO positions when demand enrolls the
    // remaining classes, so the observed order must include them first.
    for (auto* b:buildings) requested.emplace_back(b,0);
    for (auto* b:buildings)
        for (int swim=1;swim<SWIM_CLASS_COUNT;++swim) requested.emplace_back(b,swim);
    GradientExecutionAudit audit;
    for (const auto [building,swim]:requested)
        REQUIRE(map.requestBuildingField(building,swim,BuildingRoute::Footprint)==FieldReadiness::Pending);
    REQUIRE(requested.size()>64);
    CHECK(map.buildingGradientPipelineStatus().synchronous==0);
    for (unsigned tick=0;tick<requested.size()+4;++tick)
    {
        advanceFields(game);
        bool missing=false;
        for (const auto [building,swim]:requested) {
            const auto* field=map.lookupBuildingField(building,swim,BuildingRoute::Footprint);
            if (!field) missing=true;
            else CHECK_FALSE(missing); // Every observed publication is a FIFO prefix.
        }
    }
    for (const auto [building,swim]:requested)
        CHECK(map.lookupBuildingField(building,swim,BuildingRoute::Footprint)!=nullptr);
    map.finishGradientPipeline();
    CHECK(map.buildingGradientPipelineStatus().synchronous==0);
    if (GAGCore::ThreadSupport::available) CHECK(audit.totals().owner==0);
}

TEST_CASE("cold simulation fields publish identical complete generations with one two four and eight participants [artifacts]")
{
    glob2test::HeadlessGlobals globals;
    std::vector<std::vector<Uint32>> expected;
    std::vector<std::vector<Uint32>> expectedChecksums;
    for (unsigned participants : {1u, 2u, 4u, 8u})
    {
        CAPTURE(participants);
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=731});
        auto& game = world.game;
        auto& map = game.map;
        game.gameHeader.setBuildingGradientDelay(3);
        map.configureCompute(participants);
        map.configureGradientPipeline(participants-1,3);
        auto* building = world.addBuilding("inn",4,4);
        REQUIRE(building);
        map.setResourceByIndex(20,20,WHEAT,1);
        map.addGuardArea(22,22,0);
        map.addClearArea(20,20,0);
        map.addForbidden(24,24,0);
        const auto fields = [&] {
            return std::array<const Uint16*,6>{
                map.lookupBuildingField(building,0,BuildingRoute::Footprint),
                map.getMaterialGradientSlot(0,WHEAT,0),
                map.getMaterialGradientSlot(0,WOOD,0),
                map.getGuardAreasGradient(0,0),
                map.getClearAreasGradient(0,0),
                map.getForbiddenGradient(0,0)};
        };
        GradientExecutionAudit audit;
        REQUIRE(map.requestBuildingField(building,0,BuildingRoute::Footprint)==FieldReadiness::Pending);
        for (const auto* field : fields()) CHECK(field==nullptr);
        const auto queued = map.buildingGradientPipelineStatus().queued;
        for (int i=0;i<20;++i)
            REQUIRE(map.requestBuildingField(building,0,BuildingRoute::Footprint)==FieldReadiness::Pending);
        CHECK(map.buildingGradientPipelineStatus().queued==queued);

        std::vector<std::vector<Uint32>> trajectory;
        std::vector<std::vector<Uint32>> checksumTrajectory;
        std::ostringstream trace;
        trace<<"boundary\tchecksum_hash\tfield_hash\n";
        for (unsigned tick=0;tick<48;++tick)
        {
            advanceFields(game);
            auto& state=trajectory.emplace_back();
            for (const auto* field : fields())
            {
                state.push_back(field!=nullptr);
                if (field) state.insert(state.end(),field,field+map.getW()*map.getH());
            }
            const auto& checksums=checksumTrajectory.emplace_back(normalizedChecksums(game));
            trace<<game.stepCounter<<'\t'<<evidenceHash(checksums)<<'\t'<<evidenceHash(state)<<'\n';
        }
        const auto ready=fields();
        for (const auto* field : ready) REQUIRE(field!=nullptr);
        // No source is a completed field, distinct from the initial nullptr.
        CHECK(ready[2][map.coordToIndex(16,16)]==GRADIENT_UNREACHABLE);
        int distance=-1;
        CHECK(map.queryBuildingDistance(building,0,16,16,&distance,BuildingRoute::Footprint)==RouteResult::Reachable);
        const auto* search=building->globalGradientSearch[building->routeSlot(0,BuildingRoute::Footprint)].get();
        CHECK((!search || search->complete()));
        // Drain outstanding private work before the audit's lifetime ends.
        map.finishGradientPipeline();
        const auto counts=audit.totals();
        const auto evidenceName="participants-"+std::to_string(participants);
        glob2test::writeFile(glob2test::artifactDir()/(evidenceName+".tsv"),trace.str());
        glob2test::writeFile(glob2test::artifactDir()/(evidenceName+"-kernel-entries.tsv"),
            "owner\tother\n"+std::to_string(counts.owner)+'\t'+std::to_string(counts.other)+'\n');
        if (participants>1 && GAGCore::ThreadSupport::available) {
            CHECK(counts.owner==0);
            CHECK(counts.other>0);
        } else CHECK(counts.owner>0);
        if (expected.empty()) {
            expected=std::move(trajectory);
            expectedChecksums=std::move(checksumTrajectory);
        } else {
            CHECK(trajectory==expected);
            CHECK(checksumTrajectory==expectedChecksums);
        }
    }
}
}

TEST_CASE("ordinary terrain edits with markets disabled round-trip binary and text saves" * doctest::test_suite("DelayedGradientPublication"))
{
    glob2test::HeadlessGlobals globals;
    for (bool text:{false,true}) {
        CAPTURE(text);
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true,.seed=947});
        auto& game=world.game; auto& map=game.map;
        game.gameHeader.getExperiments().set(ExperimentId::MarketsV2,false);
        game.gameHeader.setBuildingGradientDelay(3);
        map.configureCompute(2); map.configureGradientPipeline(1,3);
        REQUIRE_FALSE(map.marketsV2Enabled());
        map.setResourceByIndex(20,20,WHEAT,1);
        map.requestMaterialField(0,WHEAT,0);
        advanceFields(game); // Keep an ordinary material ticket during painting.
        map.paintCell(12,12,WATER);
        // Loading rejects market routing state when the feature is disabled,
        // so the round trip below also verifies the dirty-flag invariant.
        auto* storage=new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> output(text
            ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(storage))
            : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(storage)));
        game.save(output.get(),false,"terrain edit without markets"); output->flush();
        const auto bytes=storage->takeContents();
        GameGUI resumed;
        // TextInputStream borrows its backend; BinaryInputStream owns it.
        auto inputBackend=std::make_unique<GAGCore::MemoryStreamBackend>(bytes.data(),bytes.size());
        inputBackend->seekFromStart(0);
        std::unique_ptr<GAGCore::InputStream> input(text
            ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(inputBackend.get()))
            : static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(inputBackend.release())));
        REQUIRE(resumed.game.load(input.get()));
        REQUIRE_FALSE(resumed.game.map.marketsV2Enabled());
        CHECK(normalizedChecksums(game)==normalizedChecksums(resumed.game));
        for (unsigned i=0;i<12;++i) {
            advanceFields(game); advanceFields(resumed.game);
            CHECK(normalizedChecksums(game)==normalizedChecksums(resumed.game));
        }
    }
}
