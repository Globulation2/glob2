// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientFixture.h"
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
// Advance isolated completed-world boundaries without unit decisions. Publication
// and capture see the same Game tick, so admission at c publishes at c+K.
void advanceFields(Game& game)
{
    ++game.stepCounter;
    game.map.advanceGradientPipeline();
    game.map.stagePeriodicGradientPreparation();
    glob2test::prepareGradientBoundary(game);
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
TEST_CASE("all natural visitation flags initialize and clear including reserved material slots")
{
    glob2test::HeadlessGlobals globals;
    // A reused allocation must not silently supply zeroes for uninitialized
    // reserved bytes: those bytes are still serialized for the AIR cycle.
    alignas(Map) std::array<std::byte,sizeof(Map)> storage;
    storage.fill(std::byte{0x7f});
    auto* map=::new(storage.data()) Map;
    for(int team=0;team<Team::MAX_COUNT;++team)
        for(int material=0;material<MaterialSlotCount;++material)
            for(int swim=0;swim<MOVEMENT_CLASS_COUNT;++swim) {
                CHECK_FALSE(map->gradientUpdated[team][material][swim]);
                map->gradientUpdated[team][material][swim]=true;
            }
    map->clear();
    for(int team=0;team<Team::MAX_COUNT;++team)
        for(int material=0;material<MaterialSlotCount;++material)
            for(int swim=0;swim<MOVEMENT_CLASS_COUNT;++swim)
                CHECK_FALSE(map->gradientUpdated[team][material][swim]);
    map->~Map();
}

TEST_CASE("successful planning reads leave warm natural fields idle beyond the demanded age cap")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& game=world.game;auto& map=game.map;
    map.configureGradientPipeline(0,3);
    auto* worker=world.addUnit(WORKER,8,8);REQUIRE(worker);
    auto* inn=world.addBuilding("inn",10,8);REQUIRE(inn);
    map.setResourceByIndex(20,20,WHEAT,1);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Pending);
    for(unsigned tick=0;tick<4;++tick) advanceFields(game);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Reachable);
    const auto& field=map.gradientRuntime->resources.fields.at(WHEAT*SWIM_CLASS_COUNT);
    const auto submitted=field.submitted, serving=field.serving.tick;
    const auto jobs=map.gradientPipelineStatus().jobs;
    const auto* published=map.lookupMaterialGradientSlot(0,WHEAT,0);
    REQUIRE(published);
    // Recruiters repeatedly inspect this route, but no unit actually moves.
    // Keeping it queried past 128 ticks must not convert the demanded age cap
    // into a fixed refresh interval for every field the planner inspects.
    for(unsigned tick=0;tick<ResourceGradientPolicy::MaximumInterval+16;++tick) {
        int distance=-1;Sint32 targetX=-1,targetY=-1;
        CHECK(map.queryMaterialDistance(0,WHEAT,0,8,8,&distance)==RouteResult::Reachable);
        CHECK(map.materialAvailableSlot(0,WHEAT,0,8,8));
        CHECK(map.materialAvailableSlot(0,WHEAT,0,8,8,&distance));
        CHECK(map.materialAvailableUpdateSlot(0,WHEAT,0,8,8,&targetX,&targetY,&distance));
        CHECK(inn->considerUnitForMaterial(worker,WHEAT,&distance));
        CHECK_FALSE(field.requested);
        CHECK(field.count32==0);CHECK(field.count128==0);
        advanceFields(game);
        CHECK(map.gradientPipelineStatus().jobs==jobs);
    }
    CHECK(Uint32(game.stepCounter-field.submitted)>ResourceGradientPolicy::MaximumInterval);
    CHECK(field.submitted==submitted);CHECK(field.serving.tick==serving);
    CHECK(map.lookupMaterialGradientSlot(0,WHEAT,0)==published);
    CHECK(map.gradientRuntime->fieldRequests.empty());
    CHECK(map.gradientPipelineStatus().pending==0);
}

TEST_CASE("recruitment-only demand recovers a published unreachable natural field after terrain opens")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& game=world.game;auto& map=game.map;
    map.configureGradientPipeline(0,3);
    auto* worker=world.addUnit(WORKER,8,8);REQUIRE(worker);
    auto* inn=world.addBuilding("inn",10,8);REQUIRE(inn);
    map.setResourceByIndex(20,20,WHEAT,1);
    for(int y=19;y<=21;++y) for(int x=19;x<=21;++x)
        if(x!=20 || y!=20) map.paintCell(x,y,WATER);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Pending);
    for(unsigned tick=0;tick<4;++tick) advanceFields(game);
    REQUIRE(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Unreachable);
    REQUIRE(map.hasMaterialSourceSlot(WHEAT));
    const auto jobs=map.gradientPipelineStatus().jobs;
    const auto delay=map.gradientPipelineStatus().delay;
    const auto& field=map.gradientRuntime->resources.fields.at(WHEAT*SWIM_CLASS_COUNT);
    CHECK(field.requested);
    CHECK(field.count32==0);CHECK(field.count128==0);
    int distance=0;
    CHECK_FALSE(inn->considerUnitForMaterial(worker,WHEAT,&distance));
    for(int y=19;y<=21;++y) for(int x=19;x<=21;++x) map.paintCell(x,y,GRASS);
    bool recruited=false;
    unsigned admittedAt=0;
    for(unsigned tick=0;tick<ResourceGradientPolicy::MaximumInterval+delay+4 && !recruited;++tick) {
        // No movement query is made: recruitment itself must request upkeep.
        recruited=inn->considerUnitForMaterial(worker,WHEAT,&distance);
        advanceFields(game);
        if(map.gradientPipelineStatus().jobs>jobs) {
            if(!admittedAt) admittedAt=game.stepCounter;
            CHECK(map.gradientPipelineStatus().jobs==jobs+1);
            // A failed planning read enrolls normal delayed maintenance: it
            // must not make freshly computed guidance visible before c+K.
            if(Uint32(game.stepCounter-admittedAt)<delay)
                CHECK(map.lookupMaterialGradientSlot(0,WHEAT,0)[map.coordToIndex(8,8)]<=GRADIENT_UNREACHABLE);
        }
    }
    REQUIRE(recruited);
    REQUIRE(admittedAt!=0);
    CHECK(map.gradientPipelineStatus().jobs==jobs+1);
    CHECK(map.queryMaterialDistance(0,WHEAT,0,8,8)==RouteResult::Reachable);
    CHECK(field.count32==0);CHECK(field.count128==0);
}

TEST_CASE("a partially visited warm air cycle continues through binary and text saves")
{
    glob2test::HeadlessGlobals globals;
    for(bool text:{false,true}) {
        CAPTURE(text);
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto& game=world.game;auto& map=game.map;
        map.configureGradientPipeline(0,3);
        map.paintCell(30,30,VOID_HOLE);
        map.setResourceByIndex(20,20,WHEAT,1);map.setResourceByIndex(21,21,WOOD,1);
        REQUIRE(map.getMaterialGradientSlot(0,WHEAT,AIR_MOVEMENT_CLASS));
        REQUIRE(map.getMaterialGradientSlot(0,WOOD,AIR_MOVEMENT_CLASS));
        advanceFields(game);
        REQUIRE(map.gradientUpdated[0][WOOD][AIR_MOVEMENT_CLASS]);
        REQUIRE_FALSE(map.gradientUpdated[0][WHEAT][AIR_MOVEMENT_CLASS]);
        REQUIRE(map.gradientPipelineStatus().pending==1);
        auto* memory=new GAGCore::MemoryStreamBackend;
        std::unique_ptr<GAGCore::OutputStream> out(text
            ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(memory))
            : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(memory)));
        game.save(out.get(),false,"partially visited air cycle");out->flush();
        const auto bytes=memory->takeContents();
        auto backend=std::make_unique<GAGCore::MemoryStreamBackend>(bytes.data(),bytes.size());
        backend->seekFromStart(0);
        std::unique_ptr<GAGCore::InputStream> in(text
            ? static_cast<GAGCore::InputStream*>(new GAGCore::TextInputStream(backend.get()))
            : static_cast<GAGCore::InputStream*>(new GAGCore::BinaryInputStream(backend.release())));
        GameGUI resumed;REQUIRE(resumed.game.load(in.get()));
        for(unsigned tick=0;tick<16;++tick) {
            CHECK(normalizedChecksums(game)==normalizedChecksums(resumed.game));
            for(int material:{WHEAT,WOOD}) {
                CHECK(map.gradientUpdated[0][material][AIR_MOVEMENT_CLASS]
                    ==resumed.game.map.gradientUpdated[0][material][AIR_MOVEMENT_CLASS]);
                const auto* a=map.lookupMaterialGradientSlot(0,material,AIR_MOVEMENT_CLASS);
                const auto* b=resumed.game.map.lookupMaterialGradientSlot(0,material,AIR_MOVEMENT_CLASS);
                REQUIRE(a);REQUIRE(b);CHECK(std::equal(a,a+map.cellCount(),b));
            }
            advanceFields(game);advanceFields(resumed.game);
        }
    }
}

TEST_CASE("known-zero movement statistics retain first-use FIFO demand and exclude air from the adaptive policy")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
    auto& game=world.game; auto& map=game.map;
    map.configureGradientPipeline(0,3);
    map.setResourceByIndex(20,20,WHEAT,1);
    auto& stats=world.team->stats.editMeasurements();
    stats.movementClassesKnown=true;
    stats.groundUnitsByMovementClass.fill(0);
    REQUIRE(map.getMaterialGradientSlot(0,WHEAT,0)==nullptr);
    REQUIRE(map.gradientRuntime->fieldRequests.size()==1);
    // The first unit may precede its delayed statistics sample. Its cold ticket
    // must still publish; known-zero counts only suppress periodic maintenance.
    for(unsigned tick=0;tick<4;++tick) advanceFields(game);
    REQUIRE(map.lookupMaterialGradientSlot(0,WHEAT,0)!=nullptr);
    CHECK(map.gradientPipelineStatus().published==1);
    const auto fields=map.gradientRuntime->resources.fields.size();
    EntityRandom random;int dx=0,dy=0;bool stop=false;
    REQUIRE(map.getMaterialGradientSlot(0,WHEAT,AIR_MOVEMENT_CLASS)!=nullptr);
    map.queryMaterialStep(random,0,WHEAT,AIR_MOVEMENT_CLASS,8,8,&dx,&dy,&stop,nullptr);
    CHECK(map.gradientRuntime->resources.fields.size()==fields);
}

TEST_CASE("two demanded warm resources batch alongside one cold FIFO ticket at a shared deadline")
{
    glob2test::HeadlessGlobals globals;
    for(unsigned participants:{1u,4u}) {
        CAPTURE(participants);
        glob2test::HeadlessGame world({.clearImmobile=true,.header=true});
        auto& game=world.game;auto& map=game.map;
        map.configureCompute(participants);map.configureGradientPipeline(participants-1,3);
        map.setResourceByIndex(20,20,WHEAT,1);map.setResourceByIndex(21,21,WOOD,1);
        REQUIRE(map.getMaterialGradientSlot(0,WHEAT,0)==nullptr);
        REQUIRE(map.getMaterialGradientSlot(0,WOOD,0)==nullptr);
        for(unsigned tick=0;tick<5;++tick) advanceFields(game);
        REQUIRE(map.lookupMaterialGradientSlot(0,WHEAT,0));
        REQUIRE(map.lookupMaterialGradientSlot(0,WOOD,0));
        const auto before=map.gradientPipelineStatus().jobs;
        // Force the documented demanded-age cap, independent of fitted scores.
        game.stepCounter+=ResourceGradientPolicy::MaximumInterval;
        EntityRandom random;int dx=0,dy=0;bool stop=false;
        map.queryMaterialStep(random,0,WHEAT,0,8,8,&dx,&dy,&stop,nullptr);
        map.queryMaterialStep(random,0,WOOD,0,8,8,&dx,&dy,&stop,nullptr);
        REQUIRE(map.getForbiddenGradient(0,0)==nullptr);
        advanceFields(game);
        CHECK(map.gradientPipelineStatus().jobs==before+3);
        CHECK(map.gradientPipelineStatus().pending==3);
        CHECK(map.gradientRuntime->fieldRequests.empty());
        for(unsigned tick=1;tick<3;++tick) {
            advanceFields(game);
            CHECK(map.lookupForbiddenGradient(0,0)==nullptr);
        }
        advanceFields(game);
        REQUIRE(map.lookupForbiddenGradient(0,0));
        CHECK(map.gradientPipelineStatus().pending==0);
        CHECK(map.gradientPipelineStatus().published==before+3);
    }
}

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
        map.setResourceByIndex(20,20,WHEAT,1);
        map.addForbidden(24,24,0);
        map.addGuardArea(22,22,0);
        map.addClearArea(20,20,0);
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
        if (GAGCore::ThreadSupport::available) {
            CHECK(audit.counts(GradientExecutionAudit::Kind::Seed).owner==0);
            CHECK(audit.counts(GradientExecutionAudit::Kind::TerrainPlane).owner==0);
        }
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

TEST_CASE("cold material and area fields publish identical generations with one two four and eight participants [artifacts]")
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
        map.setResourceByIndex(20,20,WHEAT,1);
        map.setResourceByIndex(21,21,WOOD,1);
        map.addGuardArea(22,22,0);
        map.addClearArea(20,20,0);
        map.addForbidden(24,24,0);
        const auto fields = [&] {
            return std::array<const Uint16*,5>{
                map.getMaterialGradientSlot(0,WHEAT,0),
                map.getMaterialGradientSlot(0,WOOD,0),
                map.getGuardAreasGradient(0,0),
                map.getClearAreasGradient(0,0),
                map.getForbiddenGradient(0,0)};
        };
        GradientExecutionAudit audit;
        for (const auto* field : fields()) CHECK(field==nullptr);
        const auto queued=map.gradientRuntime->fieldRequests.size();
        for (int i=0;i<20;++i)
            REQUIRE(map.getMaterialGradientSlot(0,WHEAT,0)==nullptr);
        CHECK(map.gradientRuntime->fieldRequests.size()==queued);

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
        // Both source-bearing materials must publish complete guidance.
        CHECK(ready[1][map.coordToIndex(21,21)]==GRADIENT_AT_GOAL);
        // Drain outstanding private work before the audit's lifetime ends.
        map.finishGradientPipeline();
        const auto counts=audit.totals();
        const auto evidenceName="participants-"+std::to_string(participants);
        glob2test::writeFile(glob2test::artifactDir()/(evidenceName+".tsv"),trace.str());
        glob2test::writeFile(glob2test::artifactDir()/(evidenceName+"-kernel-entries.tsv"),
            "owner\tother\n"+std::to_string(counts.owner)+'\t'+std::to_string(counts.other)+'\n');
        if (participants>1 && GAGCore::ThreadSupport::available) {
            CHECK(audit.counts(GradientExecutionAudit::Kind::Seed).owner==0);
            CHECK(audit.counts(GradientExecutionAudit::Kind::TerrainPlane).owner==0);
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
        map.getMaterialGradientSlot(0,WHEAT,0);
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
