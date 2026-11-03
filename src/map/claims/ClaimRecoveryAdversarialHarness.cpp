// SPDX-License-Identifier: GPL-3.0-or-later
// Temporary adversarial live-recovery experiments; no synthetic route failures.
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include "ActiveRouteRuntime.h"
#include "ClaimPolicyConfig.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <nlohmann/json.hpp>
#include <memory>

namespace {
std::unique_ptr<glob2test::HeadlessGame> copyRecoveryGame(glob2test::HeadlessGame& source,unsigned participants)
{
    auto* bytes=new GAGCore::MemoryStreamBackend;GAGCore::BinaryOutputStream output(bytes);
    const auto before=source.game.checkSum();source.gui.save(&output,"claim recovery adversarial continuation");output.flush();
    auto* copied=new GAGCore::MemoryStreamBackend(*bytes);copied->seekFromStart(0);
    auto result=std::make_unique<glob2test::HeadlessGame>();
    result->game.map.clear();result->game.map.resourceCells.clear();result->game.map.configureCompute(participants);
    GAGCore::BinaryInputStream input(copied);REQUIRE(result->gui.load(&input,true));
    REQUIRE(result->game.checkSum()==before);REQUIRE(source.game.checkSum()==before);
    return result;
}

struct Islands {
    glob2test::HeadlessGame world;
    Unit* worker=nullptr;Unit* holder=nullptr;Building* consumer=nullptr;
    Uint32 nearest=0,reachable=0;
    Islands(unsigned participants,bool closest,ResourceClaims::ClaimPolicyConfig::Fallback fallback=ResourceClaims::ClaimPolicyConfig::Fallback::Wait)
        :world({.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9341}) {
        auto& game=world.game;auto& map=game.map;
        map.configureCompute(participants);game.gameHeader.setResourceGrowthDisabled(true);game.gameHeader.getWinningConditions().clear();
        {Map::TerrainEditBatch edit(map);for(int y=0;y<32;++y)for(int x:{7,8,16,17})map.setVertexTerrain(x,y,WATER);}
        map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig config;
        REQUIRE(nlohmann::json::parse(config.canonicalJson())["source_version"]==2);
        config.fallback=fallback;config.fallbackAfterDecisions=1;
        if(closest)config.selection=ResourceClaims::ClaimPolicyConfig::Selection::ClosestGeometric;
        map.setMaterialClaimPolicyConfig(config);
        consumer=world.addBuilding("inn",9,22);REQUIRE(consumer);
        worker=world.addUnit(WORKER,8,8);holder=world.addUnit(WORKER,11,10);REQUIRE(worker);REQUIRE(holder);
        worker->setPerformance(SWIM,0);holder->setPerformance(SWIM,0);
        consumer->maxUnitWorking=2;consumer->setMaterial(WHEAT,0);consumer->updateCallLists();
        for(auto* unit:{worker,holder}) {unit->hungriness=0;unit->medical=Unit::MED_FREE;unit->activity=Unit::ACT_RANDOM;}
        nearest=Uint32(map.coordToIndex(12,8));reachable=Uint32(map.coordToIndex(14,18));
        // Six independent unavailable-island accesses exceed the four-entry cache.
        for(auto [x,y]:{std::pair{12,8},{14,18},{5,8},{4,10},{3,12},{2,14},{1,16},{0,18}}) {
            REQUIRE(map.incResourceByIndex(x,y,WHEAT,0));map.setMaterialAmountSlot(map.coordToIndex(x,y),WHEAT,1);
        }
        glob2test::publishCurrentHiringSuggestions(game);
        REQUIRE(consumer->subscribeToBringMaterialsStep());REQUIRE(consumer->subscribeToBringMaterialsStep());
        glob2test::discardPrematureHiringSuggestions(game);
        REQUIRE(worker->attachedBuilding==consumer);REQUIRE(holder->attachedBuilding==consumer);
        REQUIRE(holder->acquireMaterialClaimAt(nearest));
        REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,0);}));
        game.invalidateObservation();game.observeCompletedWorld();
    }
};
}

TEST_CASE("a terminal island path failure reaches the configured bounded release fallback" * doctest::test_suite("ClaimRecoveryAdversarial"))
{
    glob2test::HeadlessGlobals globals;
    for(unsigned participants:{1u,3u}) {
        CAPTURE(participants);
        Islands fixture(participants,false,ResourceClaims::ClaimPolicyConfig::Fallback::BoundedWaitRelease);
        auto& game=fixture.world.game;auto& map=game.map;
        bool released=false;
        for(unsigned decision=0;decision<32;++decision) {
            fixture.worker->handleActiveMaterialMovement();
            if(!fixture.worker->attachedBuilding) {released=true;break;}
            map.flushClaimRoutes();++game.stepCounter;game.observeCompletedWorld();
        }
        REQUIRE(released);
        CHECK_FALSE(fixture.worker->activeMaterialClaim);
        CHECK(map.materialClaimLedger().size()==1); // The independent holder remains reserved.
    }
}

TEST_CASE("recovery bounds island attempts and preserves pending GUI continuation" * doctest::test_suite("ClaimRecoveryAdversarial"))
{
    glob2test::HeadlessGlobals globals;
    for(bool closest:{false,true}) {
    CAPTURE(closest);std::vector<Uint32> reference;
    for(unsigned participants:{1u,3u}) {
        CAPTURE(participants);Islands fixture(participants,closest);
        auto& game=fixture.world.game;auto& map=game.map;const auto gid=fixture.worker->gid;
        std::unique_ptr<glob2test::HeadlessGame> resumed;
        bool reachedLater=false,savedPending=false;
        std::vector<Uint32> trace;
        const auto advance=[&](Game& current) {
            auto* unit=current.teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];REQUIRE(unit);
            unit->handleActiveMaterialMovement();current.map.flushClaimRoutes();
            ++current.stepCounter;current.observeCompletedWorld();
        };
        for(unsigned tick=0;tick<256;++tick) {
            CAPTURE(tick);
            if(!savedPending) {
                fixture.worker->handleActiveMaterialMovement();
                REQUIRE(fixture.worker->activeMaterialClaim);
                REQUIRE(map.materialClaimRoutes);
                REQUIRE(map.materialClaimRoutes->states.begin()->second.pending);
                map.flushClaimRoutes();
                resumed=copyRecoveryGame(fixture.world,participants);savedPending=true;
                // This request publishes exactly one tick after its original admission.
                ++game.stepCounter;game.observeCompletedWorld();
                ++resumed->game.stepCounter;resumed->game.observeCompletedWorld();
            } else {
                advance(game);advance(resumed->game);
            }
            REQUIRE(game.checkSum()==resumed->game.checkSum());
            trace.push_back(game.checkSum());
            REQUIRE(map.materialClaimRoutes);
            // First fixed64tick admission window cannot be escaped by evicting
            // one of the six bad accesses from a four-entry rejection cache.
            if(tick<64)CHECK(map.materialClaimRoutes->pipeline.requestSequence()<=4);
            if(fixture.worker->activeMaterialClaim && fixture.worker->activeMaterialClaim->access==fixture.reachable) {
                int dx=0,dy=0;const auto step=map.nextClaimRouteStep(*fixture.worker,&dx,&dy);
                auto* peer=resumed->game.teams[0]->myUnits[Unit::GIDtoID(gid)];int peerX=0,peerY=0;
                CHECK(resumed->game.map.nextClaimRouteStep(*peer,&peerX,&peerY)==step);
                CHECK(peerX==dx);CHECK(peerY==dy);
                if(step==ResourceClaims::StepResult::Ready)reachedLater=true;
            }
        }
        CHECK(savedPending);CHECK(reachedLater);
        if(reference.empty())reference=trace;else CHECK(reference==trace);
    }
    }
}
