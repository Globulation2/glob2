// SPDX-License-Identifier: GPL-3.0-or-later
// Live claim recovery: real route failures (no synthetic results), the job
// release that follows them, and exact save continuation of recovery state.
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include "ActiveRouteRuntime.h"
#include "Building.h"
#include "BuildingUtils.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <fstream>
#include <algorithm>

namespace {
std::unique_ptr<glob2test::HeadlessGame> copyRecoveryGame(glob2test::HeadlessGame& source,unsigned participants,const std::string& exportName={})
{
    auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(bytes);
    const auto before=source.game.checkSum();source.gui.save(&output,"claim recovery adversarial continuation");output.flush();
    if(!exportName.empty()) {
        std::ofstream file(glob2test::artifactDir()/(exportName+".game"),std::ios::binary);
        file.write(bytes->getBuffer(),bytes->getPosition());REQUIRE(file.good());
    }
    auto* copied=new GAGCore::MemoryStreamBackend(*bytes);copied->seekFromStart(0);
    auto result=std::make_unique<glob2test::HeadlessGame>();
    result->game.map.clear();result->game.map.resourceCells.clear();result->game.map.configureCompute(participants);
    GAGCore::BinaryInputStream input(copied);REQUIRE(result->gui.load(&input,true));
    REQUIRE(result->game.checkSum()==before);REQUIRE(source.game.checkSum()==before);
    return result;
}

// Two land strips separated by water columns. The worker and its reachable
// stock (14,18) share the middle strip; six further stocks lie across the
// water, which the terrain prefilter keeps out of selection. The nearest
// stock (12,8) is held by a second worker.
struct Islands {
    glob2test::HeadlessGame world;
    Unit* worker=nullptr;Unit* holder=nullptr;Building* consumer=nullptr;
    Uint32 nearest=0,reachable=0;
    explicit Islands(unsigned participants)
        :world({.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9341}) {
        auto& game=world.game;auto& map=game.map;
        map.configureCompute(participants);game.gameHeader.setResourceGrowthDisabled(true);game.gameHeader.getWinningConditions().clear();
        {Map::TerrainEditBatch edit(map);for(int y=0;y<32;++y)for(int x:{7,8,16,17})map.setVertexTerrain(x,y,WATER);}
        map.enableResourceClaims();
        consumer=world.addBuilding("inn",9,22);REQUIRE(consumer);
        worker=world.addUnit(WORKER,8,8);holder=world.addUnit(WORKER,11,10);REQUIRE(worker);REQUIRE(holder);
        worker->setPerformance(SWIM,0);holder->setPerformance(SWIM,0);
        consumer->maxUnitWorking=2;consumer->setMaterial(WHEAT,0);consumer->updateCallLists();
        for(auto* unit:{worker,holder}) {unit->hungriness=0;unit->medical=Unit::MED_FREE;unit->activity=Unit::ACT_RANDOM;}
        nearest=Uint32(map.coordToIndex(12,8));reachable=Uint32(map.coordToIndex(14,18));
        for(auto [x,y]:{std::pair{12,8},{14,18},{5,8},{4,10},{3,12},{2,14},{1,16},{0,18}}) {
            REQUIRE(map.incResourceByIndex(x,y,WHEAT,0));map.setMaterialAmountSlot(map.coordToIndex(x,y),WHEAT,1);
        }
        hire(2);
        REQUIRE(worker->attachedBuilding==consumer);REQUIRE(holder->attachedBuilding==consumer);
        REQUIRE(holder->acquireMaterialClaimAt(nearest));
        REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
        game.invalidateObservation();game.observeCompletedWorld();
    }
    void hire(unsigned count) {
        glob2test::publishCurrentHiringSuggestions(world.game);
        for(unsigned i=0;i<count;++i)REQUIRE(consumer->subscribeToBringMaterialsStep());
        glob2test::discardPrematureHiringSuggestions(world.game);
    }
};
}

TEST_CASE("adjacent pickup exchange survives full-save continuation with an outstanding old route" * doctest::test_suite("ActiveClaimRoutes"))
{
    glob2test::HeadlessGlobals globals;
    for(unsigned participants:{1u,3u}) {
        CAPTURE(participants);
        Islands fixture(participants);
        auto& game=fixture.world.game;auto& map=game.map;
        REQUIRE(fixture.worker->acquireMaterialClaimAt(fixture.reachable));
        REQUIRE(map.incResourceByIndex(9,8,WHEAT,0));map.setMaterialAmountSlot(map.coordToIndex(9,8),WHEAT,1);
        game.invalidateObservation();game.observeCompletedWorld();int dx=0,dy=0;
        // The route is computing (the worker may take a gradient step meanwhile).
        REQUIRE(map.nextClaimRouteStep(*fixture.worker,&dx,&dy)!=ResourceClaims::StepResult::Failed);
        REQUIRE(map.claimRouteComputing(*fixture.worker));
        map.flushClaimRoutes(); // A checkpoint boundary has sealed this tick's submissions.
        const auto gid=fixture.worker->gid;
        auto resumed=copyRecoveryGame(fixture.world,participants,participants==1?"adjacent-pickup-pending":"");
        if(participants==1) {
            std::ofstream metadata(glob2test::artifactDir()/"adjacent-pickup-v3-pending.json");
            metadata<<nlohmann::json{{"start_tick",game.stepCounter},{"gid",gid},{"old_source",fixture.reachable},
                {"adjacent_access",map.coordToIndex(9,8)},{"simulation_checksum",game.checkSum()}}.dump();REQUIRE(metadata.good());
        }
        REQUIRE(resumed->game.map.activeClaimsEnabled());
        const auto complete=[&](Game& branch) {
            auto* unit=branch.teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];REQUIRE(unit);
            auto& world=branch.map;const auto random=unit->entityRandom;
            unit->handleDisplacement();REQUIRE(unit->displacement==Unit::DIS_HARVESTING);
            REQUIRE(unit->activeMaterialClaim);CHECK(unit->activeMaterialClaim->access==world.coordToIndex(9,8));
            CHECK(world.materialClaimLedger().materialCount(fixture.reachable,WHEAT)==0);
            CHECK(world.materialClaimLedger().materialCount(world.coordToIndex(9,8),WHEAT)==1);
            world.flushClaimRoutes();++branch.stepCounter;world.publishClaimRoutes();
            CHECK(world.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Arrived);
            REQUIRE(unit->takeActiveMaterialClaim());
            CHECK(world.materialAmountAtSlot(world.coordToIndex(9,8),WHEAT)==0);
            CHECK(world.materialAmountAtSlot(fixture.reachable,WHEAT)==1);
            CHECK(world.materialClaimLedger().materialCount(world.coordToIndex(9,8),WHEAT)==0);
            CHECK(unit->entityRandom==random);return branch.checkSum();
        };
        CHECK(complete(game)==complete(resumed->game));
    }
}

TEST_CASE("the terrain prefilter keeps island stock out of selection" * doctest::test_suite("ClaimRecoveryAdversarial"))
{
    glob2test::HeadlessGlobals globals;
    std::vector<Uint32> reference;
    for(unsigned participants:{1u,3u}) {
        CAPTURE(participants);
        Islands fixture(participants);
        auto& game=fixture.world.game;auto& map=game.map;
        // The worker claims the reachable stock first time and walks there
        // without ever routing to an island.
        std::vector<Uint32> trace;
        bool arrived=false;
        for(unsigned decision=0;decision<64 && !arrived;++decision) {
            fixture.worker->handleActiveMaterialMovement();
            REQUIRE(fixture.worker->activeMaterialClaim);
            CHECK(fixture.worker->activeMaterialClaim->access==fixture.reachable);
            int dx=0,dy=0;
            const auto step=map.nextClaimRouteStep(*fixture.worker,&dx,&dy);
            REQUIRE(step!=ResourceClaims::StepResult::Failed);
            arrived=step==ResourceClaims::StepResult::Arrived;
            if(step==ResourceClaims::StepResult::Ready) {
                map.setGroundUnit(fixture.worker->posX,fixture.worker->posY,NOGUID);
                fixture.worker->posX=(fixture.worker->posX+dx+32)%32;fixture.worker->posY=(fixture.worker->posY+dy+32)%32;
                map.setGroundUnit(fixture.worker->posX,fixture.worker->posY,fixture.worker->gid);
            }
            map.flushClaimRoutes();++game.stepCounter;game.invalidateObservation();game.observeCompletedWorld();
            trace.push_back(map.activeClaimRouteChecksum(true));
        }
        CHECK(arrived);
        const auto owner=ResourceClaims::unitOwner(fixture.worker->gid,fixture.worker->scriptIdentity);
        REQUIRE(map.materialClaimRoutes);
        CHECK(map.materialClaimRoutes->recovery.at(owner).failureStreak==0);
        if(reference.empty())reference=trace;else CHECK(trace==reference);
    }
}

TEST_CASE("a route failure releases the job, rejects the access and continues exactly after save" * doctest::test_suite("ClaimRecoveryAdversarial"))
{
    glob2test::HeadlessGlobals globals;
    std::vector<Uint32> reference;
    for(unsigned participants:{1u,3u}) {
        CAPTURE(participants);
        Islands fixture(participants);
        auto& game=fixture.world.game;auto& map=game.map;
        // Enclose the reachable stock with forbidden area after the gradient
        // was published: the stale destination plane still leads selection
        // to it, the terrain prefilter cannot see forbidden area, and the
        // route search finds every pickup stance blocked.
        for(int y=17;y<=19;++y)for(int x=13;x<=15;++x)if(x!=14 || y!=18)map.addForbidden(x,y,0);
        game.invalidateObservation();game.observeCompletedWorld();
        const auto owner=ResourceClaims::unitOwner(fixture.worker->gid,fixture.worker->scriptIdentity);
        const auto gid=fixture.worker->gid,consumerGid=fixture.consumer->gid;
        std::vector<Uint32> trace;
        bool claimed=false,released=false;
        for(unsigned decision=0;decision<32 && !released;++decision) {
            fixture.worker->handleActiveMaterialMovement();
            if(fixture.worker->activeMaterialClaim) {claimed=true;CHECK(fixture.worker->activeMaterialClaim->access==fixture.reachable);}
            released=!fixture.worker->attachedBuilding;
            map.flushClaimRoutes();++game.stepCounter;game.observeCompletedWorld();
            trace.push_back(game.checkSum());
        }
        REQUIRE(claimed);REQUIRE(released);
        CHECK_FALSE(fixture.worker->activeMaterialClaim);
        CHECK(map.materialClaimLedger().size()==1); // The independent holder remains reserved.
        const auto recovery=map.materialClaimRoutes->recovery.at(owner);
        CHECK(recovery.failureStreak==1);
        CHECK(recovery.rejects(fixture.reachable,Uint32(game.stepCounter)));
        // The recovery record is saved and continues exactly.
        auto resumed=copyRecoveryGame(fixture.world,participants,participants==1?"recovery-released":"");
        CHECK(resumed->game.map.materialClaimRoutes->recovery.at(owner)==recovery);
        // Rehired within the exclusion window, the worker skips the failed
        // access; with nothing else to claim it gives up the job again.
        for(auto* current:{&fixture.world,resumed.get()}) {
            auto* unit=current->game.teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];REQUIRE(unit);
            auto* consumer=current->game.teams[0]->myBuildings[BuildingUtils::GIDtoID(consumerGid)];REQUIRE(consumer);
            unit->medical=Unit::MED_FREE;unit->activity=Unit::ACT_RANDOM;unit->hungriness=0;
            glob2test::publishCurrentHiringSuggestions(current->game);
            consumer->subscribeToBringMaterialsStep();
            glob2test::discardPrematureHiringSuggestions(current->game);
            if(unit->attachedBuilding!=consumer)continue; // The hire check may already skip it.
            unit->handleActiveMaterialMovement();
            CHECK_FALSE(unit->activeMaterialClaim);
            CHECK_FALSE(unit->attachedBuilding);
        }
        CHECK(game.checkSum()==resumed->game.checkSum());
        trace.push_back(game.checkSum());
        if(reference.empty())reference=trace;else CHECK(trace==reference);
    }
}
