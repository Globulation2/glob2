// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ActiveMaterialClaims.h"
#include "Version.h"
#include "ClaimPolicyConfig.h"
#include "GradientFixture.h"
#include "ClaimRouteSearch.h"
#include "ResourceClaimRouteView.h"
#include <fstream>
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <memory>

namespace
{
std::string saveRoutes(Map& map, bool text)
{
    auto* backend = new GAGCore::MemoryStreamBackend;
    std::unique_ptr<GAGCore::OutputStream> output(text
        ? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend))
        : static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(backend)));
    map.saveActiveClaimRoutes(output.get());
    output->flush();
    return backend->takeContents();
}
void restoreRoutes(Map& map, const std::string& bytes, bool text, int version = VERSION_MINOR)
{
    auto backend = std::make_unique<GAGCore::MemoryStreamBackend>();
    backend->write(bytes.data(), bytes.size()); backend->seekFromStart(0);
    if (text) { GAGCore::TextInputStream input(backend.get()); map.loadActiveClaimRoutes(&input, version); }
    else { GAGCore::BinaryInputStream input(backend.release()); map.loadActiveClaimRoutes(&input, version); }
}
void enable(Map& map)
{
    map.setMaterialDestinationsEnabled(true);
    map.setActiveClaimPolicy(ResourceClaims::ActiveClaimPolicy::PrototypeStationaryV1);
}
struct Assignment
{
    Unit& unit;
    ~Assignment() { unit.releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other); unit.attachedBuilding = nullptr; }
};
void assign(Unit& unit, Building& building, Map& map, int x, int y)
{
    unit.releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);
    unit.attachedBuilding = &building; unit.activity = Unit::ACT_FILLING;
    unit.displacement = Unit::DIS_GOING_TO_RESOURCE; unit.destinationPurpose = WHEAT; unit.medical = Unit::MED_FREE;
    const auto target = Uint32(map.coordToIndex(x, y));
    map.setResourceByIndex(x, y, WHEAT, 1);
    map.setMaterialAmountSlot(target, WHEAT, 3);
    REQUIRE(unit.acquireMaterialClaimAt(target));
    REQUIRE(unit.activeMaterialClaimValid());
}
}

TEST_SUITE("ActiveClaimRoutes")
{
    TEST_CASE("adjacent pickup exchange preserves sticky controls and never steals another claim")
    {
        glob2test::HeadlessGlobals globals;
        for(unsigned version:{2u,3u})for(bool opportunistic:{false,true}) {
            if(version==2 && opportunistic)continue;
            CAPTURE(version);CAPTURE(opportunistic);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
            auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);
            ResourceClaims::ClaimPolicyConfig config;config.sourceVersion=version;config.opportunisticAdjacentPickup=opportunistic;
            map.setMaterialClaimPolicyConfig(config);
            auto* consumer=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            auto* holder=fixture.addUnit(WORKER,9,9);Assignment cleanup{*unit},cleanupHolder{*holder};
            assign(*unit,*consumer,map,14,8);assign(*holder,*consumer,map,9,8);
            map.setMaterialAmountSlot(map.coordToIndex(9,8),WHEAT,1);
            const auto original=*unit->activeMaterialClaim;const auto task=unit->materialClaimTask;
            const auto random=unit->entityRandom;const auto routes=saveRoutes(map,false);
            CHECK_FALSE(unit->prepareActiveMaterialPickup(9,8));
            CHECK(unit->activeMaterialClaim==original);CHECK(unit->materialClaimTask==task);
            CHECK(saveRoutes(map,false)==routes);
            holder->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);
            CHECK(unit->prepareActiveMaterialPickup(9,8)==opportunistic);
            CHECK(unit->entityRandom==random);
            if(opportunistic) {
                CHECK(unit->activeMaterialClaim->access==map.coordToIndex(9,8));
                CHECK(unit->materialClaimTask==task+1);
                CHECK(map.materialClaimState().at(original.source)==0);
                CHECK(map.materialClaimState().at(map.coordToIndex(9,8))==1);
                CHECK(map.materialAmountAtSlot(original.source,WHEAT)==3);
                CHECK(map.materialAmountAtSlot(map.coordToIndex(9,8),WHEAT)==1);
            } else {
                CHECK(unit->activeMaterialClaim==original);CHECK(unit->materialClaimTask==task);
            }
        }
    }
    TEST_CASE("adjacent pickup exchange prefers the held reachable claim and rejects distant pickup calls")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig config;config.sourceVersion=3;config.opportunisticAdjacentPickup=true;map.setMaterialClaimPolicyConfig(config);
        auto* consumer=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);Assignment cleanup{*unit};
        assign(*unit,*consumer,map,9,8);const auto task=unit->materialClaimTask;
        map.setResourceByIndex(7,8,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(7,8),WHEAT,1);
        CHECK_FALSE(unit->prepareActiveMaterialPickup(7,8));CHECK(unit->materialClaimTask==task);
        CHECK(unit->prepareActiveMaterialPickup(9,8));
        assign(*unit,*consumer,map,14,8);const auto far=*unit->activeMaterialClaim;
        map.setResourceByIndex(15,8,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(15,8),WHEAT,1);
        CHECK_FALSE(unit->prepareActiveMaterialPickup(15,8));CHECK(unit->activeMaterialClaim==far);
    }
    TEST_CASE("denied adjacent pickup exchange preserves the outstanding route and its publication deadline")
    {
        glob2test::HeadlessGlobals globals;
        for(unsigned participants:{1u,3u}) {
            CAPTURE(participants);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
            auto& game=fixture.game;auto& map=game.map;map.configureCompute(participants);
            map.setMaterialDestinationsEnabled(true);
            ResourceClaims::ClaimPolicyConfig config;config.sourceVersion=3;config.opportunisticAdjacentPickup=true;
            map.setMaterialClaimPolicyConfig(config);
            auto* consumer=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            auto* holder=fixture.addUnit(WORKER,9,9);Assignment cleanup{*unit},holderCleanup{*holder};
            assign(*unit,*consumer,map,14,8);assign(*holder,*consumer,map,9,8);
            const auto adjacent=Uint32(map.coordToIndex(9,8));map.setMaterialAmountSlot(adjacent,WHEAT,1);
            game.observeCompletedWorld();int dx=0,dy=0;
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
            map.flushClaimRoutes();
            const auto admittedTick=game.stepCounter;
            const auto task=unit->materialClaimTask;
            const auto original=*unit->activeMaterialClaim,other=*holder->activeMaterialClaim;
            const auto random=unit->entityRandom;
            // Both serializations include the request identity, due tick and
            // privately completed output, while owner publication stays pending.
            const auto binary=saveRoutes(map,false),text=saveRoutes(map,true);
            const auto checksum=map.activeClaimRouteChecksum(true);
            REQUIRE_FALSE(unit->prepareActiveMaterialPickup(9,8));
            CHECK(unit->activeMaterialClaim==original);CHECK(unit->materialClaimTask==task);
            CHECK(holder->activeMaterialClaim==other);CHECK(unit->entityRandom==random);
            CHECK(map.materialClaimState().at(original.source)==1);CHECK(map.materialClaimState().at(adjacent)==1);
            CHECK(saveRoutes(map,false)==binary);CHECK(saveRoutes(map,true)==text);
            CHECK(map.activeClaimRouteChecksum(true)==checksum);
            map.publishClaimRoutes();
            CHECK(game.stepCounter==admittedTick);
            CHECK(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
            CHECK(saveRoutes(map,false)==binary);
            ++game.stepCounter;map.publishClaimRoutes();
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
            CHECK(dx==1);CHECK(std::abs(dy)<=1);
            CHECK(unit->activeMaterialClaim==original);CHECK(unit->materialClaimTask==task);
            CHECK(map.materialAmountAtSlot(original.source,WHEAT)==3);
            CHECK(map.materialAmountAtSlot(adjacent,WHEAT)==1);
        }
    }
    TEST_CASE("adjacent pickup exchange uses toroidal distance at map edges")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig config;config.sourceVersion=3;config.opportunisticAdjacentPickup=true;map.setMaterialClaimPolicyConfig(config);
        auto* consumer=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,0,0);Assignment cleanup{*unit};
        assign(*unit,*consumer,map,14,8);const auto old=unit->activeMaterialClaim->source;
        map.setResourceByIndex(map.getW()-1,map.getH()-1,WHEAT,1);
        map.setMaterialAmountSlot(map.coordToIndex(-1,-1),WHEAT,1);
        REQUIRE(unit->prepareActiveMaterialPickup(-1,-1));
        CHECK(unit->targetX==map.getW()-1);CHECK(unit->targetY==map.getH()-1);
        CHECK(map.materialClaimState().at(old)==0);
        CHECK(map.materialClaimState().at(map.coordToIndex(-1,-1))==1);
    }
    TEST_CASE("adjacent farm pickup can keep its remote reserved stock while changing access")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::GameOptions options{.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true};
        options.experiments.set(ExperimentId::FarmAreas,true);glob2test::HeadlessGame fixture(options);
        auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig config;config.sourceVersion=3;config.opportunisticAdjacentPickup=true;map.setMaterialClaimPolicyConfig(config);
        auto* consumer=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,7,12);Assignment cleanup{*unit};
        for(int x=8;x<=14;++x) {
            map.setResourceByIndex(x,12,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(x,12),WHEAT,x==14?2:1);map.addFarmArea(x,12,0);
        }
        unit->attachedBuilding=consumer;unit->activity=Unit::ACT_FILLING;unit->displacement=Unit::DIS_GOING_TO_RESOURCE;
        unit->destinationPurpose=WHEAT;unit->medical=Unit::MED_FREE;
        REQUIRE(unit->acquireMaterialClaimAt(map.coordToIndex(14,12)));
        const auto source=unit->activeMaterialClaim->source;const auto task=unit->materialClaimTask;
        REQUIRE(source==map.coordToIndex(14,12));REQUIRE(unit->activeMaterialClaim->reserve==1);
        REQUIRE(unit->prepareActiveMaterialPickup(8,12));
        CHECK(unit->activeMaterialClaim->source==source);CHECK(unit->activeMaterialClaim->access==map.coordToIndex(8,12));
        CHECK(unit->materialClaimTask==task+1);CHECK(map.materialClaimState().at(source)==1);
        CHECK(map.materialClaimState().at(map.coordToIndex(8,12))==0);
        CHECK(map.materialAmountAtSlot(source,WHEAT)==2);
    }
    TEST_CASE("pending and published routes preserve publication and bytes across binary and text saves")
    {
        glob2test::HeadlessGlobals globals;
        for (unsigned threads : {1u, 3u}) for (bool text : {false, true}) {
            CAPTURE(threads); CAPTURE(text);
            glob2test::HeadlessGame fixture({.discovered=true, .clearImmobile=true, .loadDefaultRace=true, .header=true});
            auto& game = fixture.game; auto& map = game.map;
            map.computeExecutor().configure(threads); enable(map);
            auto* building = fixture.addBuilding("inn", 2, 2);
            auto* unit = fixture.addUnit(WORKER, 8, 8); Assignment cleanup{*unit}; assign(*unit, *building, map, 12, 8);
            game.observeCompletedWorld();
            int dx = 99, dy = 99;
            CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
            map.flushClaimRoutes();
            const auto before = map.activeClaimRouteChecksum(true);
            const auto pending = saveRoutes(map, text);
            restoreRoutes(map, pending, text);
            CHECK(map.activeClaimRouteChecksum(true) == before);
            CHECK(saveRoutes(map, text) == pending);
            map.publishClaimRoutes();
            CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
            ++game.stepCounter; map.publishClaimRoutes();
            CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Ready);
            CHECK(dx == 1); CHECK(std::abs(dy) <= 1);
            const auto published = saveRoutes(map, text);
            const auto publishedHash = map.activeClaimRouteChecksum(true);
            restoreRoutes(map, published, text);
            CHECK(map.activeClaimRouteChecksum(true) == publishedHash);
            CHECK(saveRoutes(map, text) == published);
            // Merely querying an unexecuted direction must not advance cursor.
            CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Ready);
            CHECK(saveRoutes(map, text) == published);
        }
    }
    TEST_CASE("changed tasks and blocked cached steps cannot reuse an old route")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true, .clearImmobile=true, .loadDefaultRace=true, .header=true});
        auto& game = fixture.game; auto& map = game.map; enable(map);
        auto* building = fixture.addBuilding("inn", 2, 2);
        auto* unit = fixture.addUnit(WORKER, 8, 8); Assignment cleanup{*unit}; assign(*unit, *building, map, 12, 8);
        game.observeCompletedWorld();
        int dx = 0, dy = 0;
        REQUIRE(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
        map.flushClaimRoutes(); ++game.stepCounter; map.publishClaimRoutes();
        REQUIRE(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Ready);
        const auto beforeTraffic = saveRoutes(map, false);
        fixture.addUnit(WORKER, unit->posX + dx, unit->posY + dy);
        map.markImmobileUnit(unit->posX + dx, unit->posY + dy, unit->owner->teamNumber);
        CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
        CHECK(unit->activeMaterialClaim.has_value());
        CHECK(saveRoutes(map, false) == beforeTraffic);
        map.addForbidden(unit->posX + dx, unit->posY + dy, unit->owner->teamNumber);
        CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Failed);
        // Complete the next immutable observation before admitting another tick.
        game.invalidateObservation(); game.observeCompletedWorld();
        assign(*unit, *building, map, 8, 12);
        CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
        map.cancelClaimRoute(ResourceClaims::unitOwner(unit->gid, unit->scriptIdentity));
        map.flushClaimRoutes(); ++game.stepCounter; map.publishClaimRoutes();
        game.invalidateObservation(); game.observeCompletedWorld();
        CHECK(map.nextClaimRouteStep(*unit, &dx, &dy) == ResourceClaims::StepResult::Pending);
    }
    TEST_CASE("disabled and historical route state has no implicit active policy")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true, .header=true});
        auto& map = fixture.game.map;
        const auto before = map.activeClaimRouteChecksum(true);
        const auto disabled = saveRoutes(map, false); restoreRoutes(map, disabled, false);
        CHECK_FALSE(map.activeClaimsEnabled()); CHECK(map.activeClaimRouteChecksum(true) == before);
        enable(map);
        restoreRoutes(map, {}, false, 194);
        CHECK_FALSE(map.activeClaimsEnabled());
    }
    TEST_CASE("cached route cadence exposes persistent new immobile traffic")
    {
        // Diagnostic control for the route experiment: a zero-refresh path
        // currently waits forever behind traffic which appeared after capture.
        // Preserve this observation rather than silently selecting a new policy.
        glob2test::HeadlessGlobals globals;
        std::ofstream evidence(glob2test::artifactDir()/"traffic-cadence.tsv");
        REQUIRE(evidence.good());evidence<<"participants\tcadence\tready\twaiting\n";
        for(unsigned threads:{1u,3u}) for(unsigned cadence:{0u,1u}) {
            CAPTURE(threads);CAPTURE(cadence);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;
            map.configureCompute(threads);map.setMaterialDestinationsEnabled(true);
            ResourceClaims::ClaimPolicyConfig config;config.routeRefreshJ=cadence;
            map.setMaterialClaimPolicyConfig(config);
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            Assignment cleanup{*unit};assign(*unit,*building,map,14,8);
            unit->materialClaimPolicyState.decisionOrdinal=1;game.observeCompletedWorld();
            int dx=0,dy=0;
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
            map.flushClaimRoutes();++game.stepCounter;map.publishClaimRoutes();
            game.invalidateObservation();game.observeCompletedWorld();
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
            const int blockedX=unit->posX+dx,blockedY=unit->posY+dy;
            REQUIRE(fixture.addUnit(WORKER,blockedX,blockedY));
            map.markImmobileUnit(blockedX,blockedY,unit->owner->teamNumber);
            unsigned ready=0,waiting=0;
            for(unsigned decision=0;decision<32;++decision) {
                map.flushClaimRoutes();++game.stepCounter;map.publishClaimRoutes();
                game.invalidateObservation();game.observeCompletedWorld();
                ++unit->materialClaimPolicyState.decisionOrdinal;
                const auto result=map.nextClaimRouteStep(*unit,&dx,&dy);
                REQUIRE(result!=ResourceClaims::StepResult::Failed);
                if(result==ResourceClaims::StepResult::Ready) {
                    ++ready;
                    CHECK_FALSE((unit->posX+dx==blockedX && unit->posY+dy==blockedY));
                } else ++waiting;
                REQUIRE(unit->activeMaterialClaim);
            }
            if(cadence)CHECK(ready>=30);
            else {CHECK(ready==0);CHECK(waiting==32);}
            evidence<<threads<<'\t'<<cadence<<'\t'<<ready<<'\t'<<waiting<<'\n';
        }
        REQUIRE(evidence.good());
    }
    TEST_CASE("experimental heading refresh and displaced publication retain deterministic saved state")
    {
        glob2test::HeadlessGlobals globals;
        for(bool text:{false,true}) for(unsigned threads:{1u,3u}) {
            CAPTURE(text);CAPTURE(threads);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;
            map.computeExecutor().configure(threads);map.setMaterialDestinationsEnabled(true);
            ResourceClaims::ClaimPolicyConfig config;config.pendingStep=ResourceClaims::ClaimPolicyConfig::PendingStep::DirectHeading;
            config.routeRefreshJ=1;map.setMaterialClaimPolicyConfig(config);
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            Assignment cleanup{*unit};assign(*unit,*building,map,14,8);
            unit->materialClaimPolicyState.decisionOrdinal=1;game.observeCompletedWorld();
            const auto move=[&](int dx,int dy){map.setGroundUnit(unit->posX,unit->posY,NOGUID);unit->posX=(unit->posX+dx+map.getW())%map.getW();unit->posY=(unit->posY+dy+map.getH())%map.getH();map.setGroundUnit(unit->posX,unit->posY,unit->gid);};
            const auto roundtrip=[&]{const auto before=map.activeClaimRouteChecksum(true);const auto bytes=saveRoutes(map,text);restoreRoutes(map,bytes,text);CHECK(saveRoutes(map,text)==bytes);CHECK(map.activeClaimRouteChecksum(true)==before);};
            int dx=0,dy=0;const auto random=unit->entityRandom;
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
            CHECK(dx==1);CHECK(dy==0);move(dx,dy);roundtrip();
            // Export joins but does not publish early, and heading is RNG-free.
            CHECK(unit->entityRandom==random);
            ++game.stepCounter;map.publishClaimRoutes();game.invalidateObservation();game.observeCompletedWorld();
            unit->materialClaimPolicyState.decisionOrdinal=2;
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
            move(dx,dy);roundtrip(); // Old path and replacement coexist in this save.
            ++game.stepCounter;map.publishClaimRoutes();game.invalidateObservation();game.observeCompletedWorld();
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
            CHECK(unit->activeMaterialClaim->access==map.coordToIndex(14,8));CHECK(unit->entityRandom==random);
            roundtrip();
        }
    }
    TEST_CASE("successful periodic refreshes remain unrestricted before recovery")
    {
        glob2test::HeadlessGlobals globals;
        for(unsigned threads:{1u,3u}) {
            CAPTURE(threads);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;
            map.configureCompute(threads);map.setMaterialDestinationsEnabled(true);
            ResourceClaims::ClaimPolicyConfig config;config.routeRefreshJ=1;
            map.setMaterialClaimPolicyConfig(config);
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            Assignment cleanup{*unit};assign(*unit,*building,map,14,8);
            unit->materialClaimPolicyState.decisionOrdinal=1;game.observeCompletedWorld();
            int dx=0,dy=0;
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
            for(unsigned tick=0;tick<10;++tick) {
                map.flushClaimRoutes();++game.stepCounter;map.publishClaimRoutes();
                game.invalidateObservation();game.observeCompletedWorld();
                ++unit->materialClaimPolicyState.decisionOrdinal;
                const auto before=map.activeClaimSnapshotChecksum();
                REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
                // Every J=1 decision admits a new request while keeping its
                // previous route usable. A retry cap must not erase this factor.
                CHECK(map.activeClaimSnapshotChecksum()!=before);
            }
        }
    }
    TEST_CASE("experimental probe arrival follows only the held access and stale tails replan directly")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
        auto& game=fixture.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig config;config.routeTarget=ResourceClaims::ClaimPolicyConfig::RouteTarget::ProbeThenGradient;
        map.setMaterialClaimPolicyConfig(config);
        auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
        Assignment cleanup{*unit};assign(*unit,*building,map,14,8);unit->materialClaimPolicyState.probe=map.coordToIndex(8,8);
        REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
        game.observeCompletedWorld();int dx=0,dy=0;
        REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
        map.flushClaimRoutes();++game.stepCounter;game.observeCompletedWorld();
        REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);CHECK(dx==1);CHECK(dy==0);
        map.setGroundUnit(8,8,NOGUID);unit->posX=9;map.setGroundUnit(9,8,unit->gid);
        REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);CHECK(dx==1);
        const auto saved=saveRoutes(map,true);const auto hash=map.activeClaimRouteChecksum(true);
        restoreRoutes(map,saved,true);CHECK(map.activeClaimRouteChecksum(true)==hash);
        // A new nearer resource relabels the gradient; held access stays fixed.
        map.setResourceByIndex(11,8,WHEAT,1);map.updateMaterialGradient(0,WHEAT,MovementAccess::Land,false);
        const auto relabeled=[&]{const auto* cost=map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);const auto labels=map.materialGradientDestinations(cost);return labels&&labels->x[map.coordToIndex(9,8)]==11;};
        // Owner work is already admitted: visit every publication boundary so
        // statistics/hiring deadlines advance with the material refresh.
        for(unsigned boundary=0;!relabeled() && boundary<4096;++boundary){++game.stepCounter;game.observeCompletedWorld();}
        REQUIRE(relabeled());
        game.invalidateObservation();game.observeCompletedWorld();
        CHECK(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
        CHECK(unit->activeMaterialClaim->access==map.coordToIndex(14,8));
        const auto direct=saveRoutes(map,false);restoreRoutes(map,direct,false);CHECK(saveRoutes(map,false)==direct);
        ++game.stepCounter;map.publishClaimRoutes();
        CHECK(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
    }

    TEST_CASE("experimental refresh cadence cannot retry failed paths indefinitely")
    {
        glob2test::HeadlessGlobals globals;
        for(bool probe:{false,true}) {
            CAPTURE(probe);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;map.setMaterialDestinationsEnabled(true);
            ResourceClaims::ClaimPolicyConfig config;config.routeRefreshJ=1;config.maximumExpansions=1;
            if(probe)config.routeTarget=ResourceClaims::ClaimPolicyConfig::RouteTarget::ProbeThenGradient;
            map.setMaterialClaimPolicyConfig(config);
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            Assignment cleanup{*unit};assign(*unit,*building,map,14,8);unit->materialClaimPolicyState.probe=map.coordToIndex(12,8);
            game.observeCompletedWorld();int dx=0,dy=0;
            unit->materialClaimPolicyState.decisionOrdinal=1;
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
            map.flushClaimRoutes();++game.stepCounter;map.publishClaimRoutes();
            unit->materialClaimPolicyState.decisionOrdinal=2;
            if(probe) {
                game.invalidateObservation();game.observeCompletedWorld();
                REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
                map.flushClaimRoutes();++game.stepCounter;map.publishClaimRoutes();
                unit->materialClaimPolicyState.decisionOrdinal=3;
            }
            CHECK(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Failed);
        }
    }

    TEST_CASE("experimental publication outside bounded prefix replans without releasing reservation")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
        auto& game=fixture.game;auto& map=game.map;map.setMaterialDestinationsEnabled(true);
        map.setMaterialClaimPolicyConfig(ResourceClaims::ClaimPolicyConfig{});
        auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
        Assignment cleanup{*unit};assign(*unit,*building,map,14,8);game.observeCompletedWorld();
        const auto claim=unit->activeMaterialClaim;const auto task=unit->materialClaimTask;
        int dx=0,dy=0;REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
        map.flushClaimRoutes();map.setGroundUnit(8,8,NOGUID);unit->posX=4;unit->posY=20;map.setGroundUnit(4,20,unit->gid);
        ++game.stepCounter;map.publishClaimRoutes();game.invalidateObservation();game.observeCompletedWorld();
        CHECK(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
        CHECK(unit->activeMaterialClaim==claim);CHECK(unit->materialClaimTask==task);
        const auto saved=saveRoutes(map,false);const auto hash=map.activeClaimRouteChecksum(true);
        restoreRoutes(map,saved,false);CHECK(map.activeClaimRouteChecksum(true)==hash);CHECK(saveRoutes(map,false)==saved);
        ++game.stepCounter;map.publishClaimRoutes();
        CHECK(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
    }

    TEST_CASE("experimental saved routes reject incompatible pending and phase metadata")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
        auto& game=fixture.game;auto& map=game.map;map.setMaterialDestinationsEnabled(true);
        map.setMaterialClaimPolicyConfig(ResourceClaims::ClaimPolicyConfig{});
        auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
        Assignment cleanup{*unit};assign(*unit,*building,map,14,8);game.observeCompletedWorld();
        int dx=0,dy=0;REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
        const auto saved=saveRoutes(map,true);const auto hash=map.activeClaimRouteChecksum(true);
        for(const auto& replacements:std::vector<std::pair<std::string,std::string>>{
            {"pendingRadius = 1;","pendingRadius = 2;"},
            {"pendingRadius = 1;","pendingRadius = 0;"},
            {"pendingTarget = "+std::to_string(map.coordToIndex(14,8))+";","pendingTarget = 0;"},
            {"followingGradient = 0;","followingGradient = 1;"}}) {
            CAPTURE(replacements.first);CAPTURE(replacements.second);
            auto corrupt=saved;const auto at=corrupt.find(replacements.first);REQUIRE(at!=std::string::npos);
            corrupt.replace(at,replacements.first.size(),replacements.second);
            CHECK_THROWS_AS(restoreRoutes(map,corrupt,true),std::runtime_error);
            restoreRoutes(map,saved,true);CHECK(map.activeClaimRouteChecksum(true)==hash);
        }
    }

    TEST_CASE("experimental obsolete probe refresh retires without reversing a saved gradient tail")
    {
        glob2test::HeadlessGlobals globals;
        for(bool text:{false,true}) {
            CAPTURE(text);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;map.setMaterialDestinationsEnabled(true);
            ResourceClaims::ClaimPolicyConfig config;config.routeTarget=ResourceClaims::ClaimPolicyConfig::RouteTarget::ProbeThenGradient;config.routeRefreshJ=1;
            map.setMaterialClaimPolicyConfig(config);
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            Assignment cleanup{*unit};assign(*unit,*building,map,14,8);unit->materialClaimPolicyState.probe=map.coordToIndex(10,8);
            REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
            const auto move=[&](int dx,int dy){map.setGroundUnit(unit->posX,unit->posY,NOGUID);unit->posX=(unit->posX+dx+map.getW())%map.getW();unit->posY=(unit->posY+dy+map.getH())%map.getH();map.setGroundUnit(unit->posX,unit->posY,unit->gid);game.invalidateObservation();game.observeCompletedWorld();};
            game.observeCompletedWorld();unit->materialClaimPolicyState.decisionOrdinal=1;int dx=0,dy=0;
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
            map.flushClaimRoutes();++game.stepCounter;map.publishClaimRoutes();
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);move(dx,dy);
            REQUIRE(unit->posX==9);REQUIRE(unit->posY==8);
            // Refresh toward the probe, then reach and leave it using the old
            // path/tail before the fixed publication boundary of that refresh.
            unit->materialClaimPolicyState.decisionOrdinal=2;
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);move(dx,dy);
            REQUIRE(unit->posX==10);REQUIRE(unit->posY==8);
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);move(dx,dy);
            REQUIRE(unit->posX==11);REQUIRE(unit->posY==8);
            const auto pending=saveRoutes(map,text);const auto hash=map.activeClaimRouteChecksum(true);
            restoreRoutes(map,pending,text);CHECK(map.activeClaimRouteChecksum(true)==hash);
            ++game.stepCounter;map.publishClaimRoutes();
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
            CHECK(dx==1);CHECK(dy==0);CHECK(unit->activeMaterialClaim->access==map.coordToIndex(14,8));
            const auto retired=saveRoutes(map,text);const auto retiredHash=map.activeClaimRouteChecksum(true);
            restoreRoutes(map,retired,text);CHECK(saveRoutes(map,text)==retired);CHECK(map.activeClaimRouteChecksum(true)==retiredHash);
        }
    }

    TEST_CASE("unreachable first alternative repeatedly bypasses available fallback and later reachable stock")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
        auto& game=fixture.game;auto& map=game.map;
        game.gameHeader.setResourceGrowthDisabled(true);
        { Map::TerrainEditBatch edit(map);
            // Two adjacent water vertex columns create one uniform water
            // cell column, with walkable shoreline cells on either bank.
            for(int y=0;y<32;++y)for(int x:{7,8,16,17})map.setVertexTerrain(x,y,WATER);
        }
        map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig config;
        config.sourceVersion=1; // Retain the frozen pre-recovery control/reproducer.
        config.fallback=ResourceClaims::ClaimPolicyConfig::Fallback::BoundedWaitRelease;
        config.fallbackAfterDecisions=1;
        map.setMaterialClaimPolicyConfig(config);
        auto* building=fixture.addBuilding("inn",9,22);
        auto* unit=fixture.addUnit(WORKER,8,8);auto* holder=fixture.addUnit(WORKER,11,10);
        Assignment cleanup{*unit},holderCleanup{*holder};
        unit->setPerformance(SWIM,0);holder->setPerformance(SWIM,0);
        building->maxUnitWorking=2;building->setMaterial(WHEAT,0);building->updateCallLists();
        for(auto* worker:{unit,holder}) {worker->medical=Unit::MED_FREE;worker->activity=Unit::ACT_RANDOM;worker->hungriness=0;}
        const auto nearest=Uint32(map.coordToIndex(12,8));
        const auto unreachable=Uint32(map.coordToIndex(5,8));
        const auto reachable=Uint32(map.coordToIndex(14,18));
        for(const auto at:{nearest,unreachable,reachable}) {
            REQUIRE(map.incResourceByIndex(int(at%32),int(at/32),WHEAT,0));map.setMaterialAmountSlot(at,WHEAT,1);
        }
        glob2test::publishCurrentHiringSuggestions(game);
        REQUIRE(building->subscribeToBringMaterialsStep());
        REQUIRE(building->subscribeToBringMaterialsStep());
        glob2test::discardPrematureHiringSuggestions(game);
        for(auto* worker:{unit,holder}) {
            REQUIRE(worker->attachedBuilding==building);REQUIRE(worker->jobPurpose==UnitJobPurpose::Transport);
            REQUIRE(worker->destinationPurpose==WHEAT);REQUIRE(worker->movementAccess()==MovementAccess::Land);
        }
        REQUIRE(holder->acquireMaterialClaimAt(nearest));
        REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
        game.invalidateObservation();game.observeCompletedWorld();
        const auto view=map.stateView();ResourceClaims::RouteSearch oracle;
        const ResourceClaims::RouteProfile profile{0,unit->owner->me,unit->gid,ResourceClaims::OccupancyPolicy::Static};
        const auto exact=[&](Uint32 target) {
            return oracle.find(field::Grid(32,32),Uint32(map.coordToIndex(unit->posX,unit->posY)),65533,
                [&](Uint32 i){return ResourceClaims::routePassable(view,i,profile);},
                [&](Uint32 i,bool diagonal){return ResourceClaims::routeEntryCost(view,i,profile,diagonal);},
                [&](Uint32 i){return map.warpDistMax(int(i%32),int(i/32),int(target%32),int(target/32))<=1;},
                [](Uint32){return Uint64(0);});
        };
        REQUIRE(exact(unreachable).status==ResourceClaims::RouteStatus::Unreachable);
        const auto possible=exact(reachable);REQUIRE(possible.status==ResourceClaims::RouteStatus::Found);
        std::ofstream trace(glob2test::artifactDir()/"unreachable-alternative-loop.tsv");
        trace<<"cycle\ttick\ttask\taccess\tfallback_count\tx\ty\n";
        for(unsigned cycle=0;cycle<4;++cycle) {
            unit->handleActiveMaterialMovement();REQUIRE(unit->activeMaterialClaim);
            CHECK(unit->activeMaterialClaim->access==unreachable);
            trace<<cycle<<'\t'<<game.stepCounter<<'\t'<<unit->materialClaimTask<<'\t'<<unit->activeMaterialClaim->access<<'\t'<<unit->materialClaimPolicyState.fallbackCount<<'\t'<<unit->posX<<'\t'<<unit->posY<<'\n';
            map.flushClaimRoutes();++game.stepCounter;game.observeCompletedWorld();
            int dx=0,dy=0;REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Failed);
            unit->handleActiveMaterialMovement();CHECK_FALSE(unit->activeMaterialClaim);
            CHECK(unit->attachedBuilding==building);CHECK(unit->materialClaimPolicyState.fallbackCount==0);
            CHECK(unit->posX==8);CHECK(unit->posY==8);
            ++game.stepCounter;game.observeCompletedWorld();
        }
        // A later pattern candidate is actually usable in this unchanged world.
        REQUIRE(unit->acquireMaterialClaimAt(reachable));int dx=0,dy=0;
        REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
        map.flushClaimRoutes();++game.stepCounter;game.observeCompletedWorld();
        CHECK(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
        CHECK(map.materialAmountAtSlot(unreachable,WHEAT)==1);
        CHECK(map.materialAmountAtSlot(reachable,WHEAT)==1);
        trace<<"reachable_cost\t"<<possible.cost<<'\n';
    }

    TEST_CASE("terrain component prefilter selects reachable stock across the water divide")
    {
        glob2test::HeadlessGlobals globals;
        for(unsigned version:{1u,5u}) {
        CAPTURE(version);
        glob2test::HeadlessGame fixture({.wDec=5,.hDec=5,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
        auto& game=fixture.game;auto& map=game.map;
        game.gameHeader.setResourceGrowthDisabled(true);
        { Map::TerrainEditBatch edit(map);
            for(int y=0;y<32;++y)for(int x:{7,8,16,17})map.setVertexTerrain(x,y,WATER);
        }
        // Terrain-only components: two land strips, joined across the torus
        // seam on the far side; water separates them for ground movement.
        const auto labels=map.claimTerrainComponents(accessClass(MovementAccess::Land));
        const auto label=[&](int x,int y){return labels->at(Uint32(map.coordToIndex(x,y)));};
        CHECK(label(7,8)==ResourceClaims::TerrainComponentLabels::None);
        CHECK(label(16,8)==ResourceClaims::TerrainComponentLabels::None);
        CHECK(label(8,8)!=ResourceClaims::TerrainComponentLabels::None);
        CHECK(label(8,8)==label(14,18));CHECK(label(8,0)==label(15,31));
        CHECK(label(5,8)==label(20,8));CHECK(label(0,8)==label(31,8));CHECK(label(5,8)!=label(8,8));
        CHECK_FALSE(ResourceClaims::terrainComponentMayReach(*labels,8,8,Uint32(map.coordToIndex(5,8))));
        CHECK(ResourceClaims::terrainComponentMayReach(*labels,8,8,Uint32(map.coordToIndex(14,18))));
        // The water cell next to the bank is reachable through its stance.
        CHECK(ResourceClaims::terrainComponentMayReach(*labels,8,8,Uint32(map.coordToIndex(16,8))));
        const auto amphibious=map.claimTerrainComponents(accessClass(MovementAccess::Amphibious));
        CHECK(amphibious->at(Uint32(map.coordToIndex(8,8)))==amphibious->at(Uint32(map.coordToIndex(5,8))));
        const auto rebuilds=map.claimTerrainComponentRebuilds();
        CHECK(map.claimTerrainComponents(accessClass(MovementAccess::Land))==labels);
        CHECK(map.claimTerrainComponentRebuilds()==rebuilds);
        map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig config;
        config.sourceVersion=version;
        if(version==5) {config.pattern="final-r32-exact24-assembled-v1";config.patternFrame=ResourceClaims::ClaimPolicyConfig::PatternFrame::NearestD4;
            config.opportunisticAdjacentPickup=true;}
        config.fallback=ResourceClaims::ClaimPolicyConfig::Fallback::BoundedWaitRelease;
        config.fallbackAfterDecisions=1;config.terrainComponentPrefilter=true;
        map.setMaterialClaimPolicyConfig(config);
        auto* building=fixture.addBuilding("inn",9,22);
        auto* unit=fixture.addUnit(WORKER,8,8);auto* holder=fixture.addUnit(WORKER,11,10);
        Assignment cleanup{*unit},holderCleanup{*holder};
        unit->setPerformance(SWIM,0);holder->setPerformance(SWIM,0);
        building->maxUnitWorking=2;building->setMaterial(WHEAT,0);building->updateCallLists();
        for(auto* worker:{unit,holder}) {worker->medical=Unit::MED_FREE;worker->activity=Unit::ACT_RANDOM;worker->hungriness=0;}
        const auto nearest=Uint32(map.coordToIndex(12,8));
        const auto unreachable=Uint32(map.coordToIndex(5,8));
        const auto reachable=Uint32(map.coordToIndex(14,18));
        for(const auto at:{nearest,unreachable,reachable}) {
            REQUIRE(map.incResourceByIndex(int(at%32),int(at/32),WHEAT,0));map.setMaterialAmountSlot(at,WHEAT,1);
        }
        glob2test::publishCurrentHiringSuggestions(game);
        REQUIRE(building->subscribeToBringMaterialsStep());
        REQUIRE(building->subscribeToBringMaterialsStep());
        glob2test::discardPrematureHiringSuggestions(game);
        for(auto* worker:{unit,holder}) {REQUIRE(worker->attachedBuilding==building);REQUIRE(worker->movementAccess()==MovementAccess::Land);}
        REQUIRE(holder->acquireMaterialClaimAt(nearest));
        REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
        game.invalidateObservation();game.observeCompletedWorld();
        // Resource placement changes no terrain: the cached labelling stays.
        CHECK(map.claimTerrainComponents(accessClass(MovementAccess::Land))==labels);
        unit->handleActiveMaterialMovement();REQUIRE(unit->activeMaterialClaim);
        CHECK(unit->activeMaterialClaim->access==reachable);
        const auto start=map.warpDistMax(unit->posX,unit->posY,int(reachable%32),int(reachable/32));
        int dx=0,dy=0;
        map.flushClaimRoutes();++game.stepCounter;game.observeCompletedWorld();
        REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
        // The first step moves toward the reachable stock.
        CHECK(map.warpDistMax(unit->posX+dx,unit->posY+dy,int(reachable%32),int(reachable/32))<start);
        CHECK(map.materialAmountAtSlot(unreachable,WHEAT)==1);
        // Terrain edits invalidate the derived labels and rebuild them exactly.
        { Map::TerrainEditBatch edit(map); for(int x:{7,8})map.setVertexTerrain(x,8,GRASS); }
        const auto rebuilt=map.claimTerrainComponents(accessClass(MovementAccess::Land));
        CHECK(rebuilt!=labels);CHECK(map.claimTerrainComponentRebuilds()>rebuilds);
        CHECK(*rebuilt==*ResourceClaims::labelTerrainComponents(map,accessClass(MovementAccess::Land)));
        }
    }
    TEST_CASE("refresh switch margin keeps the held claim unless an alternative is sufficiently closer")
    {
        glob2test::HeadlessGlobals globals;
        for(unsigned margin:{0u,400u,500u}) {
            CAPTURE(margin);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.setMaterialDestinationsEnabled(true);
            ResourceClaims::ClaimPolicyConfig config;config.destinationRefreshJ=1;config.refreshSwitchMarginPermille=margin;
            map.setMaterialClaimPolicyConfig(config);
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            Assignment cleanup{*unit};assign(*unit,*building,map,14,8);
            const auto held=Uint32(map.coordToIndex(14,8)),closer=Uint32(map.coordToIndex(11,8));
            map.setResourceByIndex(11,8,WHEAT,1);map.setMaterialAmountSlot(closer,WHEAT,3);
            REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
            map.updateMaterialGradient(0,WHEAT,MovementAccess::Land,false);
            const auto relabeled=[&]{const auto* cost=map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);const auto labels=map.materialGradientDestinations(cost);return labels&&labels->x[map.coordToIndex(8,8)]==11;};
            for(unsigned boundary=0;!relabeled() && boundary<4096;++boundary){++game.stepCounter;game.observeCompletedWorld();}
            REQUIRE(relabeled());
            game.invalidateObservation();game.observeCompletedWorld();
            REQUIRE(unit->activeMaterialClaimValid());
            const auto task=unit->materialClaimTask;
            unit->materialClaimPolicyState.decisionOrdinal=4;unit->materialClaimPolicyState.lastGoalAttempt=0;
            unit->handleActiveMaterialMovement();
            REQUIRE(unit->activeMaterialClaim);
            CHECK(unit->materialClaimPolicyState.lastGoalAttempt==5);
            // Held distance 6, alternative 3: 3*1000 < (1000-margin)*6 only below 500.
            if(margin==500) {CHECK(unit->activeMaterialClaim->access==held);CHECK(unit->materialClaimTask==task);}
            else {CHECK(unit->activeMaterialClaim->access==closer);CHECK(unit->materialClaimTask!=task);}
        }
    }

    TEST_CASE("claim oversubscription admits claimants up to the scaled limit and later holders fail cleanly")
    {
        glob2test::HeadlessGlobals globals;
        using Config=ResourceClaims::ClaimPolicyConfig;
        // Stock 1: exact admits one, 2000 two, 3000 three, unlimited all five.
        for(const auto& [permille,expected]:std::vector<std::pair<unsigned,unsigned>>{{1000u,1u},{2000u,2u},{3000u,3u},{Config::UnlimitedClaimOversubscription,5u}}) {
            CAPTURE(permille);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.setMaterialDestinationsEnabled(true);
            Config config;config.claimOversubscriptionPermille=permille;
            map.setMaterialClaimPolicyConfig(config);
            CHECK(map.materialClaimLimitPermille()==permille);
            auto* building=fixture.addBuilding("inn",2,2);
            const int tx=14,ty=8;const auto target=Uint32(map.coordToIndex(tx,ty));
            map.setResourceByIndex(tx,ty,WHEAT,1);map.setMaterialAmountSlot(target,WHEAT,1);
            std::vector<Unit*> units;
            struct Cleanup { std::vector<Unit*>& units; ~Cleanup(){for(auto* u:units){u->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);u->attachedBuilding=nullptr;}} } cleanup{units};
            for(int i=0;i<5;++i) {
                auto* unit=fixture.addUnit(WORKER,8,6+i);REQUIRE(unit);units.push_back(unit);
                unit->attachedBuilding=building;unit->activity=Unit::ACT_FILLING;
                unit->displacement=Unit::DIS_GOING_TO_RESOURCE;unit->destinationPurpose=WHEAT;unit->medical=Unit::MED_FREE;
            }
            unsigned admitted=0;
            for(auto* unit:units) {
                const bool acquired=unit->acquireMaterialClaimAt(target);
                CHECK(acquired==(admitted<expected));
                admitted+=acquired;
                if(acquired)CHECK(unit->activeMaterialClaimValid());else CHECK_FALSE(unit->activeMaterialClaim);
            }
            CHECK(admitted==expected);
            CHECK(map.materialClaimLedger().materialCount(target,WHEAT)==expected);
            // Selection's availability check agrees with acquisition.
            const auto& probe=*units.front()->activeMaterialClaim;
            const auto lastOwner=ResourceClaims::unitOwner(units.back()->gid,units.back()->scriptIdentity);
            CHECK(map.materialClaimAvailable(lastOwner,probe,1)==(permille==Config::UnlimitedClaimOversubscription));
            // The first holder harvests the only unit; the others then hold a
            // depleted claim, which reports ResourceDepleted and releases.
            auto* first=units.front();const auto claim=*first->activeMaterialClaim;
            first->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Harvest);
            REQUIRE(map.takeClaimedMaterial(claim,first->owner->me));
            CHECK(map.materialAmountAtSlot(target,WHEAT)==0);
            CHECK_FALSE(map.takeClaimedMaterial(claim,first->owner->me));
            for(auto* unit:units) if(unit!=first && unit->activeMaterialClaim) {
                CHECK(unit->activeMaterialClaimInvalidity()==ResourceClaims::ReleaseReason::ResourceDepleted);
                unit->releaseInvalidActiveMaterialClaim();CHECK_FALSE(unit->activeMaterialClaim);
            }
            CHECK(map.materialClaimLedger().materialCount(target,WHEAT)==0);
        }
        // A stock decrease releases only the holders beyond the scaled limit,
        // retaining the lowest owner IDs exactly as at exact capacity.
        {
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.setMaterialDestinationsEnabled(true);
            Config config;config.claimOversubscriptionPermille=1500;
            map.setMaterialClaimPolicyConfig(config);
            auto* building=fixture.addBuilding("inn",2,2);
            const int tx=14,ty=8;const auto target=Uint32(map.coordToIndex(tx,ty));
            map.setResourceByIndex(tx,ty,WHEAT,1);map.setMaterialAmountSlot(target,WHEAT,4);
            std::vector<Unit*> units;
            struct Cleanup { std::vector<Unit*>& units; ~Cleanup(){for(auto* u:units){u->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);u->attachedBuilding=nullptr;}} } cleanup{units};
            std::vector<ResourceClaims::Owner> owners;
            for(int i=0;i<8;++i) {
                auto* unit=fixture.addUnit(WORKER,8,4+i);REQUIRE(unit);units.push_back(unit);
                unit->attachedBuilding=building;unit->activity=Unit::ACT_FILLING;
                unit->displacement=Unit::DIS_GOING_TO_RESOURCE;unit->destinationPurpose=WHEAT;unit->medical=Unit::MED_FREE;
                if(unit->acquireMaterialClaimAt(target))owners.push_back(ResourceClaims::unitOwner(unit->gid,unit->scriptIdentity));
            }
            REQUIRE(owners.size()==6); // floor(4*1500/1000)
            map.setMaterialAmountSlot(target,WHEAT,3);
            map.revalidateMaterialClaimOwners(owners);
            CHECK(map.materialClaimLedger().materialCount(target,WHEAT)==4); // floor(3*1500/1000)
            std::sort(owners.begin(),owners.end());
            for(std::size_t i=0;i<owners.size();++i)CHECK((map.materialClaimLedger().find(owners[i])!=nullptr)==(i<4));
        }
    }

    TEST_CASE("pending gradient step moves the worker while its route computes and stays deterministic")
    {
        glob2test::HeadlessGlobals globals;
        using Config=ResourceClaims::ClaimPolicyConfig;
        std::vector<std::vector<int>> traces;
        for(auto step:{Config::PendingStep::Wait,Config::PendingStep::Gradient}) for(unsigned threads:{1u,2u}) for(bool text:{false,true}) {
            CAPTURE(int(step));CAPTURE(threads);CAPTURE(text);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;
            map.computeExecutor().configure(threads);map.setMaterialDestinationsEnabled(true);
            Config config;config.pendingStep=step;map.setMaterialClaimPolicyConfig(config);
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            Assignment cleanup{*unit};assign(*unit,*building,map,14,8);
            REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
            unit->materialClaimPolicyState.decisionOrdinal=1;game.invalidateObservation();game.observeCompletedWorld();
            const auto target=unit->activeMaterialClaim->access;
            const auto distance=[&](int x,int y){return map.warpDistMax(x,y,int(target%map.getW()),int(target/map.getW()));};
            const auto move=[&](int dx,int dy){map.setGroundUnit(unit->posX,unit->posY,NOGUID);unit->posX=(unit->posX+dx+map.getW())%map.getW();unit->posY=(unit->posY+dy+map.getH())%map.getH();map.setGroundUnit(unit->posX,unit->posY,unit->gid);};
            const auto roundtrip=[&]{const auto before=map.activeClaimRouteChecksum(true);const auto bytes=saveRoutes(map,text);restoreRoutes(map,bytes,text);CHECK(saveRoutes(map,text)==bytes);CHECK(map.activeClaimRouteChecksum(true)==before);};
            int dx=0,dy=0;
            const auto first=map.nextClaimRouteStep(*unit,&dx,&dy);
            if(step==Config::PendingStep::Wait){CHECK(first==ResourceClaims::StepResult::Pending);continue;}
            // The route is still computing; the worker takes the gradient step.
            REQUIRE(first==ResourceClaims::StepResult::Ready);
            CHECK(distance(unit->posX+dx,unit->posY+dy)<distance(unit->posX,unit->posY));
            std::vector<int> trace{dx,dy};
            move(dx,dy);roundtrip();
            for(unsigned decision=0;decision<4 && distance(unit->posX,unit->posY)>1;++decision) {
                map.flushClaimRoutes();++game.stepCounter;map.publishClaimRoutes();game.invalidateObservation();game.observeCompletedWorld();
                ++unit->materialClaimPolicyState.decisionOrdinal;
                const auto result=map.nextClaimRouteStep(*unit,&dx,&dy);
                REQUIRE(result==ResourceClaims::StepResult::Ready);
                CHECK(distance(unit->posX+dx,unit->posY+dy)<distance(unit->posX,unit->posY));
                trace.insert(trace.end(),{dx,dy});move(dx,dy);roundtrip();
            }
            REQUIRE(unit->activeMaterialClaim);CHECK(unit->activeMaterialClaim->access==target);
            trace.push_back(int(map.activeClaimRouteChecksum(true)));
            traces.push_back(trace);
        }
        REQUIRE(traces.size()==4);
        for(const auto& trace:traces)CHECK(trace==traces.front());
    }

    TEST_CASE("claim distance factor rejects candidates far beyond the nearest destination")
    {
        glob2test::HeadlessGlobals globals;
        using Config=ResourceClaims::ClaimPolicyConfig;
        // Nearest destination (12,8) is held by another worker at distance 4;
        // the only free stock is (14,18) at distance 10. Limit: f*4/1000+4.
        for(unsigned version:{2u,5u}) for(unsigned factor:{0u,1000u,1500u,2000u}) {
            CAPTURE(version);CAPTURE(factor);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.setMaterialDestinationsEnabled(true);
            Config config;config.sourceVersion=version;config.maxClaimDistanceFactorPermille=factor;
            if(version==5) {config.pattern="final-r32-exact24-assembled-v1";config.patternFrame=Config::PatternFrame::NearestD4;config.opportunisticAdjacentPickup=true;}
            map.setMaterialClaimPolicyConfig(config);
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);auto* holder=fixture.addUnit(WORKER,11,11);
            Assignment cleanup{*unit},holderCleanup{*holder};
            assign(*holder,*building,map,12,8);map.setMaterialAmountSlot(Uint32(map.coordToIndex(12,8)),WHEAT,1);
            assign(*unit,*building,map,14,18);map.setMaterialAmountSlot(Uint32(map.coordToIndex(14,18)),WHEAT,1);
            unit->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);
            REQUIRE(holder->activeMaterialClaimValid());
            REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
            game.invalidateObservation();game.observeCompletedWorld();
            const auto* field=map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);REQUIRE(field);
            const auto destinations=map.materialGradientDestinations(field);REQUIRE(destinations);
            REQUIRE(destinations->x[map.coordToIndex(8,8)]==12);REQUIRE(destinations->y[map.coordToIndex(8,8)]==8);
            const bool selected=unit->selectActiveMaterialClaim(field);
            const bool admitted=factor==0 || factor>=1500; // 1000*10 <= f*4+4000
            CHECK(selected==admitted);
            if(selected)CHECK(unit->activeMaterialClaim->access==Uint32(map.coordToIndex(14,18)));
            else CHECK_FALSE(unit->activeMaterialClaim);
        }
    }

    TEST_CASE("traffic sidestep and wait limit move past a unit blocking the next route cell")
    {
        glob2test::HeadlessGlobals globals;
        using Config=ResourceClaims::ClaimPolicyConfig;
        struct Arm { bool sidestep; unsigned limit; };
        for(const auto arm:{Arm{false,0},Arm{true,0},Arm{false,3},Arm{true,2}}) for(bool text:{false,true}) {
            CAPTURE(arm.sidestep);CAPTURE(arm.limit);CAPTURE(text);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;map.setMaterialDestinationsEnabled(true);
            Config config;config.trafficSidestep=arm.sidestep;config.trafficWaitLimit=arm.limit;map.setMaterialClaimPolicyConfig(config);
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);
            Assignment cleanup{*unit};assign(*unit,*building,map,14,8);
            unit->materialClaimPolicyState.decisionOrdinal=1;game.observeCompletedWorld();
            const auto target=unit->activeMaterialClaim->access;
            const auto distance=[&](int x,int y){return map.warpDistMax(x,y,int(target%map.getW()),int(target/map.getW()));};
            const auto move=[&](int dx,int dy){map.setGroundUnit(unit->posX,unit->posY,NOGUID);unit->posX=(unit->posX+dx+map.getW())%map.getW();unit->posY=(unit->posY+dy+map.getH())%map.getH();map.setGroundUnit(unit->posX,unit->posY,unit->gid);};
            const auto roundtrip=[&]{const auto before=map.activeClaimRouteChecksum(true);const auto bytes=saveRoutes(map,text);restoreRoutes(map,bytes,text);CHECK(saveRoutes(map,text)==bytes);CHECK(map.activeClaimRouteChecksum(true)==before);};
            const auto boundary=[&]{map.flushClaimRoutes();++game.stepCounter;map.publishClaimRoutes();game.invalidateObservation();game.observeCompletedWorld();};
            int dx=0,dy=0;
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Pending);
            boundary();
            REQUIRE(map.nextClaimRouteStep(*unit,&dx,&dy)==ResourceClaims::StepResult::Ready);
            const int blockedX=unit->posX+dx,blockedY=unit->posY+dy;
            // An ordinary (mobile) unit: static routes ignore it, traffic does not.
            REQUIRE(fixture.addUnit(WORKER,blockedX,blockedY));
            const auto start=distance(unit->posX,unit->posY);
            unsigned ready=0,waits=0;
            for(unsigned decision=0;decision<16 && distance(unit->posX,unit->posY)>1;++decision) {
                boundary();++unit->materialClaimPolicyState.decisionOrdinal;
                const auto result=map.nextClaimRouteStep(*unit,&dx,&dy);
                REQUIRE(result!=ResourceClaims::StepResult::Failed);
                REQUIRE(unit->activeMaterialClaim);
                if(result==ResourceClaims::StepResult::Ready) {
                    CHECK_FALSE((unit->posX+dx==blockedX && unit->posY+dy==blockedY));
                    move(dx,dy);++ready;
                } else {++waits;roundtrip();}
            }
            if(!arm.sidestep && !arm.limit) {CHECK(ready==0);CHECK(waits==16);continue;}
            CHECK(distance(unit->posX,unit->posY)<=1);CHECK(distance(unit->posX,unit->posY)<start);
            if(arm.sidestep)CHECK(waits==0);
            else CHECK(waits==arm.limit); // the last wait requests the repair
        }
    }

    TEST_CASE("gradient fallback takes the claims-off material step and keeps the job")
    {
        glob2test::HeadlessGlobals globals;
        using Config=ResourceClaims::ClaimPolicyConfig;
        for(auto fallback:{Config::Fallback::Wait,Config::Fallback::Gradient}) for(unsigned version:{2u,5u}) {
            CAPTURE(int(fallback));CAPTURE(version);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=9183});
            auto& game=fixture.game;auto& map=game.map;game.gameHeader.setResourceGrowthDisabled(true);map.setMaterialDestinationsEnabled(true);
            Config config;config.sourceVersion=version;config.fallback=fallback;
            if(version==5) {config.pattern="final-r32-exact24-assembled-v1";config.patternFrame=Config::PatternFrame::NearestD4;config.opportunisticAdjacentPickup=true;}
            map.setMaterialClaimPolicyConfig(config);
            auto* building=fixture.addBuilding("inn",2,2);auto* unit=fixture.addUnit(WORKER,8,8);auto* holder=fixture.addUnit(WORKER,11,11);
            Assignment cleanup{*unit},holderCleanup{*holder};
            // The only stock is held by another worker: selection finds nothing.
            assign(*holder,*building,map,12,8);map.setMaterialAmountSlot(Uint32(map.coordToIndex(12,8)),WHEAT,1);
            unit->attachedBuilding=building;unit->activity=Unit::ACT_FILLING;unit->displacement=Unit::DIS_GOING_TO_RESOURCE;
            unit->destinationPurpose=WHEAT;unit->medical=Unit::MED_FREE;
            REQUIRE(glob2test::awaitGradient(game,[&]{return map.getMaterialGradientSlot(0,WHEAT,MovementAccess::Land);}));
            game.invalidateObservation();game.observeCompletedWorld();
            const auto random=unit->entityRandom;
            unit->handleActiveMaterialMovement();
            CHECK_FALSE(unit->activeMaterialClaim);CHECK(unit->attachedBuilding==building);CHECK(unit->activity==Unit::ACT_FILLING);
            if(fallback==Config::Fallback::Wait) {CHECK(unit->dx==0);CHECK(unit->dy==0);continue;}
            const int dx=unit->dx,dy=unit->dy;const auto movement=unit->movement;const auto after=unit->entityRandom;
            REQUIRE((dx||dy));
            CHECK(map.warpDistMax(8+dx,8+dy,12,8)<map.warpDistMax(8,8,12,8));
            // The same decision taken by the ordinary claims-off movement.
            unit->entityRandom=random;unit->dx=0;unit->dy=0;unit->direction=UNIT_DIRECTION_NONE;
            unit->handleClassicMaterialMovement();
            CHECK(unit->dx==dx);CHECK(unit->dy==dy);CHECK(unit->movement==movement);CHECK(unit->entityRandom==after);
        }
    }
}
