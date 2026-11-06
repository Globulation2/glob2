// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "GradientFixture.h"
#include "ResourceClaimProbe.h"
#include "ClaimPolicyConfig.h"
#include "ClaimRecovery.h"
#include <nlohmann/json.hpp>
#include <zlib.h>
#include <sstream>
#include <filesystem>
#include <set>

namespace {
struct JourneyUnit : Unit {
    JourneyUnit(int x,int y,int slot,Team* team):Unit(x,y,GIDfrom(slot,0),WORKER,team,0) {}
    void pickupDecision() { handleDisplacement(); }
    void endJob() { setNeedToRecheckMedical(true); stopAttachedForBuilding(false); }
    void committedMove(int x,int y) {
        owner->map->setGroundUnit(posX,posY,NOGUID);dx=x;dy=y;
        wrapPosition();owner->map->setGroundUnit(posX,posY,gid);
    }
};
struct JourneyExplorer : Unit {
    JourneyExplorer(Team* team):Unit(16,16,GIDfrom(1,0),EXPLORER,team,0) {}
    void committedMove() {
        owner->map->setAirUnit(posX,posY,NOGUID);dx=1;dy=0;wrapPosition();
        owner->map->setAirUnit(posX,posY,gid);
    }
};
JourneyUnit* worker(glob2test::HeadlessGame& fixture,Building* consumer,int slot=0) {
    auto* unit=new JourneyUnit(8,8+slot,slot,fixture.game.teams[0]);
    fixture.game.teams[0]->attachUnit(slot,unit);fixture.game.map.setGroundUnit(unit->posX,unit->posY,unit->gid);
    unit->attachedBuilding=consumer;unit->activity=Unit::ACT_FILLING;unit->medical=Unit::MED_FREE;
    unit->displacement=Unit::DIS_GOING_TO_RESOURCE;unit->destinationPurpose=WHEAT;
    return unit;
}
std::vector<nlohmann::json> rows(const std::string& path) {
    gzFile file=gzopen(path.c_str(),"rb");REQUIRE(file);
    std::string text;char buffer[4096];int count;
    while((count=gzread(file,buffer,sizeof(buffer)))>0)text.append(buffer,count);
    CHECK(count==0);CHECK(gzclose(file)==Z_OK);
    std::istringstream input(text);std::string line;std::vector<nlohmann::json> result;
    while(std::getline(input,line))result.push_back(nlohmann::json::parse(line));
    return result;
}
}
TEST_SUITE("ClaimLiveJourney") {
TEST_CASE("movement-only observation preserves classic and active worlds") {
    glob2test::HeadlessGlobals globals;
    for(bool active:{false,true}) {
        std::vector<Uint32> reference;
        for(bool observe:{false,true}) {
            CAPTURE(active);CAPTURE(observe);
            glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=1937});
            auto& map=fixture.game.map;
            if(active) {map.setMaterialDestinationsEnabled(true);map.setMaterialClaimPolicyConfig(ResourceClaims::ClaimPolicyConfig{});}
            auto* consumer=fixture.addBuilding("inn",20,20);auto* unit=worker(fixture,consumer);
            map.setResourceByIndex(12,8,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(12,8),WHEAT,1);
            const auto path=(glob2test::artifactDir()/"movement-only.jsonl.gz").string();
            std::shared_ptr<ResourceClaims::Probe> probe;
            if(observe) {
                std::filesystem::remove(path);ResourceClaims::Probe::Config config;config.movementOnly=true;config.movementStartTick=0;
                probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
            }
            if(active)REQUIRE(unit->acquireMaterialClaimAt(map.coordToIndex(12,8)));
            const auto random=unit->entityRandom;std::vector<Uint32> checksums;
            for(unsigned tick=0;tick<3;++tick) {
                fixture.game.stepCounter=tick;
                unit->committedMove(1,0);checksums.push_back(fixture.game.checkSum());
            }
            CHECK(unit->entityRandom==random);
            CHECK(map.activeClaimsEnabled()==active);
            if(!observe)reference=checksums;
            else {
                CHECK(checksums==reference);probe->publish(3);probe->finish();
                CHECK(probe->movementMetrics().totalMoves==3);
                CHECK(probe->movementMetrics().moves[unsigned(active?ResourceClaims::Probe::MovementKind::ClaimedResource:ResourceClaims::Probe::MovementKind::UnclaimedResource)]==3);
                CHECK(probe->metrics().captureBytes==0);
                CHECK(probe->liveMetrics().journeyEligible==0);CHECK(probe->liveMetrics().moves==0);
                const auto trace=rows(path);REQUIRE(trace.size()==2);
                CHECK(trace.front()["movement_only"]==true);CHECK(trace.front()["version"]==2);
                CHECK(trace.back()["movement_audit"]["coverage_known"]==true);
            }
            unit->attachedBuilding=nullptr;
        }
    }
}
TEST_CASE("actual committed movement and harvest observation preserve simulation checksums") {
    glob2test::HeadlessGlobals globals;
    std::vector<Uint32> reference;
    for(bool observe:{false,true}) {
        CAPTURE(observe);
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=1937});
        auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);
        map.setMaterialClaimPolicyConfig(ResourceClaims::ClaimPolicyConfig{});
        auto* consumer=fixture.addBuilding("inn",20,20);auto* unit=worker(fixture,consumer);
        map.setResourceByIndex(12,8,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(12,8),WHEAT,1);
        const auto path=(glob2test::artifactDir()/"live-journey.jsonl.gz").string();
        std::shared_ptr<ResourceClaims::Probe> probe;
        if(observe) {std::filesystem::remove(path);ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=16;
            config.movementStartTick=fixture.game.stepCounter;
            probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);}
        REQUIRE(unit->acquireMaterialClaimAt(map.coordToIndex(12,8)));
        std::vector<Uint32> checksums{fixture.game.checkSum()};
        const auto random=unit->entityRandom;
        unit->committedMove(0,0);
        for(int step=0;step<3;++step) {++fixture.game.stepCounter;unit->committedMove(1,0);checksums.push_back(fixture.game.checkSum());}
        unit->dx=1;unit->dy=0;REQUIRE(unit->takeActiveMaterialClaim());checksums.push_back(fixture.game.checkSum());
        CHECK(unit->entityRandom==random);
        if(!observe)reference=checksums;
        else {
            CHECK(checksums==reference);probe->finish();
            const auto& m=probe->liveMetrics();CHECK(m.moves==3);CHECK(m.liveCost==30);
            CHECK(probe->movementMetrics().totalMoves==3);
            CHECK(probe->movementMetrics().moves[unsigned(ResourceClaims::Probe::MovementKind::ClaimedResource)]==3);
            CHECK(m.journeyAdmitted==1);CHECK(m.journeyCompleted==1);
            CHECK(m.events[unsigned(ResourceClaims::Probe::LiveEvent::Harvested)]==1);
            const auto trace=rows(path);REQUIRE(trace.size()==3);
            CHECK(trace[1]["kind"]=="journey");CHECK(trace[1]["terminal"]=="harvested");
            CHECK(trace[1]["steps"]==3);CHECK(trace[1]["live_cost"]==30);
            CHECK(trace[1]["baseline_cost"]==30);CHECK(trace[1]["baseline_status"]=="reached");
            CHECK(trace.back()["live"]["journey_completed"]==1);
            CHECK(trace.back()["live"]["scope"]=="held-material-claim-pursuit-only");
            CHECK(trace.back()["live"]["unclaimed_movement_available"]==false);
        }
        unit->attachedBuilding=nullptr;
    }
}
TEST_CASE("unclaimed and idle transport movement is observed without changing the world") {
    glob2test::HeadlessGlobals globals;
    std::vector<Uint32> reference;
    for(bool observe:{false,true}) {
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=1937});
        auto& map=fixture.game.map;auto* consumer=fixture.addBuilding("inn",20,20);auto* unit=worker(fixture,consumer);
        const auto path=(glob2test::artifactDir()/"all-transport-movement.jsonl.gz").string();
        std::shared_ptr<ResourceClaims::Probe> probe;
        if(observe) {
            std::filesystem::remove(path);ResourceClaims::Probe::Config config;config.movementStartTick=0;
            probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
        }
        const auto random=unit->entityRandom;
        std::vector<Uint32> checksums;
        const auto move=[&](int x,int y){++fixture.game.stepCounter;unit->committedMove(x,y);checksums.push_back(fixture.game.checkSum());};
        move(1,0); // Legacy/unclaimed resource pursuit, including supplier travel.
        unit->medical=Unit::MED_HUNGRY;move(0,1);
        unit->medical=Unit::MED_FREE;unit->displacement=Unit::DIS_GOING_TO_BUILDING;move(1,1);
        unit->activity=Unit::ACT_RANDOM;move(-1,0);
        unit->activity=Unit::ACT_FLAG;move(0,1);
        move(0,0); // A stationary decision is not a committed move.
        move(3,0); // Teleports/non-neighbor updates have no inferred path cost.
        auto* explorer=new JourneyExplorer(fixture.game.teams[0]);fixture.game.teams[0]->attachUnit(1,explorer);
        REQUIRE_FALSE(explorer->hasCapability(UnitRuntimeTraits::Transport));
        map.setAirUnit(16,16,explorer->gid);++fixture.game.stepCounter;explorer->committedMove();
        checksums.push_back(fixture.game.checkSum());
        CHECK(unit->entityRandom==random);
        if(!observe)reference=checksums;
        else {
            CHECK(checksums==reference);probe->publish(fixture.game.stepCounter);probe->finish();
            CHECK(probe->liveMetrics().moves==0);
            const auto& m=probe->movementMetrics();CHECK(m.totalMoves==5);CHECK(m.totalCost==54);
            CHECK(m.excludedNonNeighbor==1);
            for(auto kind:{ResourceClaims::Probe::MovementKind::Medical,ResourceClaims::Probe::MovementKind::UnclaimedResource,
                          ResourceClaims::Probe::MovementKind::ReturnToJob,ResourceClaims::Probe::MovementKind::Idle,
                          ResourceClaims::Probe::MovementKind::Other})CHECK(m.moves[unsigned(kind)]==1);
            const auto trace=rows(path);REQUIRE(trace.size()==2);
            CHECK(trace.front()["movement_audit_version"]==1);
            const auto audit=trace.back()["movement_audit"];
            CHECK(audit["coverage_known"]==true);CHECK(audit["coverage_start"]==0);CHECK(audit["coverage_end"]==8);
            CHECK(audit["total_moves"]==5);CHECK(audit["excluded_non_neighbor_moves"]==1);
        }
        unit->attachedBuilding=nullptr;
    }
}
TEST_CASE("movement audit leaves unspecified continuation coverage unknown") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=1937});
    auto* consumer=fixture.addBuilding("inn",20,20);auto* unit=worker(fixture,consumer);
    const auto path=(glob2test::artifactDir()/"movement-unknown-coverage.jsonl.gz").string();std::filesystem::remove(path);
    auto probe=std::make_shared<ResourceClaims::Probe>(fixture.game.map.computeExecutor(),path,ResourceClaims::Probe::Config{});
    fixture.game.map.setMaterialClaimProbe(probe);fixture.game.stepCounter=5;unit->committedMove(1,0);probe->finish();
    const auto trace=rows(path);CHECK(trace.back()["movement_audit"]["coverage_known"]==false);
    CHECK(trace.back()["movement_audit"]["coverage_start"].is_null());
    CHECK(trace.back()["movement_audit"]["coverage_end"].is_null());
    CHECK(trace.back()["movement_audit"]["total_moves"]==1);unit->attachedBuilding=nullptr;
}
TEST_CASE("movement coverage requires a boundary after the last observed move") {
    glob2test::HeadlessGlobals globals;
    for(bool advanceBoundary:{false,true}) {
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=1937});
        auto* consumer=fixture.addBuilding("inn",20,20);auto* unit=worker(fixture,consumer);
        const auto path=(glob2test::artifactDir()/(advanceBoundary?"movement-next-boundary.jsonl.gz":"movement-same-boundary.jsonl.gz")).string();
        std::filesystem::remove(path);ResourceClaims::Probe::Config config;config.movementStartTick=5;
        auto probe=std::make_shared<ResourceClaims::Probe>(fixture.game.map.computeExecutor(),path,config);fixture.game.map.setMaterialClaimProbe(probe);
        fixture.game.stepCounter=5;probe->publish(5);unit->committedMove(1,0);
        fixture.game.stepCounter=advanceBoundary?6:5;probe->publish(fixture.game.stepCounter);probe->finish();
        const auto audit=rows(path).back()["movement_audit"];
        CHECK(audit["total_moves"]==1);CHECK(audit["coverage_known"]==advanceBoundary);
        if(advanceBoundary)CHECK(audit["coverage_end"]==6);
        else CHECK(audit["coverage_end"].is_null());
        unit->attachedBuilding=nullptr;
    }
}
TEST_CASE("sampled journey capacity and segment censoring are bounded independently of live claims") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=1937});
    auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);map.setMaterialClaimPolicyConfig(ResourceClaims::ClaimPolicyConfig{});
    auto* consumer=fixture.addBuilding("inn",20,20);
    for(int slot=0;slot<10;++slot){map.setResourceByIndex(15,8+slot,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(15,8+slot),WHEAT,1);}
    ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=16;
    const auto path=(glob2test::artifactDir()/"live-journey-cap.jsonl.gz").string();
    std::filesystem::remove(path);
    auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
    std::vector<JourneyUnit*> units;
    for(int slot=0;slot<10;++slot) {auto* unit=worker(fixture,consumer,slot);units.push_back(unit);REQUIRE(unit->acquireMaterialClaimAt(map.coordToIndex(15,8+slot)));}
    CHECK(probe->liveMetrics().journeyAdmitted==8);CHECK(probe->liveMetrics().excludedCapacity==2);
    CHECK(map.materialClaimLedger().size()==10);
    fixture.game.stepCounter=7;map.finishMaterialClaimProbe();const auto trace=rows(path);REQUIRE(trace.size()==10);
    for(unsigned i=1;i<=8;++i){CHECK(trace[i]["terminal"]=="end_of_segment");CHECK(trace[i]["end_tick"]==7);}
    CHECK(trace.back()["live"]["journey_completed"]==8);
    CHECK(trace.back()["live"]["sampling_censored"]==true);CHECK(trace.back()["live"]["admission_uncensored"]==false);
    CHECK(trace.back()["live"]["censored_journey_outcomes"]==8);
    CHECK(trace.back()["live"]["excluded_by_phase"][0]==2);
    for(auto* unit:units){unit->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);unit->attachedBuilding=nullptr;}
}
TEST_CASE("decision outcome counters record selection and fallback without changing simulation") {
    glob2test::HeadlessGlobals globals;
    std::vector<Uint32> reference;
    for(int observation:{0,1,2}) {
        CAPTURE(observation);
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=3197});
        auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig policy;policy.sourceVersion=1;policy.usePrimaryGradient=false;map.setMaterialClaimPolicyConfig(policy);
        auto* consumer=fixture.addBuilding("inn",20,20);
        auto* seeker=worker(fixture,consumer,0);auto* holder=worker(fixture,consumer,1);auto* other=worker(fixture,consumer,2);
        const auto nearest=Uint32(map.coordToIndex(12,8)),alternative=Uint32(map.coordToIndex(14,8));
        for(int x:{12,14}){map.setResourceByIndex(x,8,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(x,8),WHEAT,1);}
        REQUIRE(holder->acquireMaterialClaimAt(nearest));
        glob2test::awaitGradient(fixture.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,seeker->movementAccess());});
        // Fixture advances gradient boundaries without Game::step; route jobs
        // still require the normal completed-world snapshot before submission.
        fixture.game.invalidateObservation();fixture.game.observeCompletedWorld();
        const auto path=(glob2test::artifactDir()/"actual-decision-outcomes.jsonl.gz").string();
        std::shared_ptr<ResourceClaims::Probe> probe;
        if(observation) {
            std::filesystem::remove(path);ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=8;config.movementOnly=observation==2;
            probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
        }
        const auto random=seeker->entityRandom;
        seeker->handleActiveMaterialMovement();REQUIRE(seeker->activeMaterialClaim);CHECK(seeker->activeMaterialClaim->access==alternative);
        std::vector<Uint32> checksums{fixture.game.checkSum(nullptr,nullptr,nullptr,false)};
        seeker->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);REQUIRE(other->acquireMaterialClaimAt(alternative));
        seeker->handleActiveMaterialMovement();CHECK_FALSE(seeker->activeMaterialClaim);
        checksums.push_back(fixture.game.checkSum(nullptr,nullptr,nullptr,false));CHECK(seeker->entityRandom==random);
        if(!observation)reference=checksums;else {
            CHECK(checksums==reference);probe->finish();
            const auto trace=rows(path);REQUIRE(trace.back()["kind"]=="summary");
            const auto& footer=trace.back()["decision_outcomes"];CHECK(footer["version"]==2);
            const auto& counters=footer["material_phase_counters"][WHEAT][0];
            if(observation==2)for(const auto& value:counters)CHECK(value==0);
            else {
                // eligible, attempted, acquired, no_candidate, route_pending, fallback_wait
                CHECK(counters[0]==2);CHECK(counters[1]==2);CHECK(counters[2]==1);CHECK(counters[6]==1);
                CHECK(counters[11]==1);CHECK(counters[14]==1);
            }
        }
        for(auto* unit:{seeker,holder,other}){unit->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);unit->attachedBuilding=nullptr;}
    }
}
TEST_CASE("pickup outcome counters span failed first candidate and successful later candidate") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=3199});
    auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);ResourceClaims::ClaimPolicyConfig policy;policy.sourceVersion=3;policy.opportunisticAdjacentPickup=true;map.setMaterialClaimPolicyConfig(policy);
    auto* consumer=fixture.addBuilding("inn",20,20);auto* seeker=worker(fixture,consumer,0);auto* holder=worker(fixture,consumer,1);
    const auto first=Uint32(map.coordToIndex(7,7)),second=Uint32(map.coordToIndex(7,8));
    for(int y:{7,8}){map.setResourceByIndex(7,y,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(7,y),WHEAT,1);}
    REQUIRE(holder->acquireMaterialClaimAt(first));
    auto* exchanger=worker(fixture,consumer,2);
    map.setResourceByIndex(14,10,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(14,10),WHEAT,1);
    map.setResourceByIndex(7,9,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(7,9),WHEAT,1);
    REQUIRE(exchanger->acquireMaterialClaimAt(Uint32(map.coordToIndex(14,10))));
    glob2test::awaitGradient(fixture.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,seeker->movementAccess());});
        // Fixture advances gradient boundaries without Game::step; route jobs
        // still require the normal completed-world snapshot before submission.
        fixture.game.invalidateObservation();fixture.game.observeCompletedWorld();
    const auto path=(glob2test::artifactDir()/"actual-pickup-outcomes.jsonl.gz").string();std::filesystem::remove(path);
    ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=8;
    auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
    seeker->pickupDecision();REQUIRE(seeker->activeMaterialClaim);CHECK(seeker->activeMaterialClaim->access==second);
    exchanger->pickupDecision();REQUIRE(exchanger->activeMaterialClaim);CHECK(exchanger->activeMaterialClaim->access==map.coordToIndex(7,9));
    probe->finish();
    const auto trace=rows(path);REQUIRE(trace.back()["kind"]=="summary");
    const auto& counts=trace.back()["decision_outcomes"]["material_phase_counters"][WHEAT][1];
    // eligible, acquired, retarget_access
    CHECK(counts[0]==2);CHECK(counts[2]==1);CHECK(counts[4]==1);
    for(auto* unit:{seeker,holder,exchanger}){unit->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);unit->attachedBuilding=nullptr;}
}

TEST_CASE("outcome counters keep selection before route failure and count cooldown scans separately") {
    glob2test::HeadlessGlobals globals;
    for(bool failure:{false,true}) {
        CAPTURE(failure);
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=3203});
        auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig policy;policy.sourceVersion=2;policy.usePrimaryGradient=false;
        policy.maximumExpansions=1;policy.destinationRefreshJ=1;map.setMaterialClaimPolicyConfig(policy);
        auto* consumer=fixture.addBuilding("inn",20,20);auto* seeker=worker(fixture,consumer,0);auto* holder=worker(fixture,consumer,1);
        const auto nearest=Uint32(map.coordToIndex(12,8)),alternative=Uint32(map.coordToIndex(14,8));
        for(int x:{12,14}){map.setResourceByIndex(x,8,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(x,8),WHEAT,1);}
        REQUIRE(holder->acquireMaterialClaimAt(nearest));
        glob2test::awaitGradient(fixture.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,seeker->movementAccess());});
        // Fixture advances gradient boundaries without Game::step; route jobs
        // still require the normal completed-world snapshot before submission.
        fixture.game.invalidateObservation();fixture.game.observeCompletedWorld();
        if(!failure) {
            // Fixture establishes an already exhausted owner admission window.
            // Actual selection still scans adjacent goals while routes cool down.
            const auto recovery=map.beginClaimRecoverySelection(*seeker);REQUIRE(recovery.state);
            auto& state=*const_cast<ResourceClaims::ClaimRecovery*>(recovery.state);
            for(unsigned i=0;i<ResourceClaims::ClaimRecovery::MaxAttempts;++i)REQUIRE(state.admit(fixture.game.stepCounter));
            // Normal cadence admissions are not recovery until a terminal
            // failure establishes a streak. Match that required prior state.
            state.recordFailure(fixture.game.stepCounter,alternative,ResourceClaims::ClaimRecovery::BackoffTicks);
            REQUIRE_FALSE(map.beginClaimRecoverySelection(*seeker).mayRoute);
        }
        const auto path=(glob2test::artifactDir()/(failure?"outcome-route-failed.jsonl.gz":"outcome-cooldown.jsonl.gz")).string();std::filesystem::remove(path);
        ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=8;
        auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
        seeker->handleActiveMaterialMovement();
        if(failure) {
            REQUIRE(seeker->activeMaterialClaim);CHECK(seeker->activeMaterialClaim->access==alternative);
            map.flushClaimRoutes();++fixture.game.stepCounter;map.publishClaimRoutes();
            fixture.game.invalidateObservation();fixture.game.observeCompletedWorld();
            seeker->handleActiveMaterialMovement();
        }
        CHECK_FALSE(seeker->activeMaterialClaim);probe->finish();
        const auto trace=rows(path);
        const auto& counts=trace.back()["decision_outcomes"]["material_phase_counters"][WHEAT][0];
        if(failure) {
            // Second decision: attempted, held_same selection, then route_failed.
            CHECK(counts[0]==2);CHECK(counts[3]==1);CHECK(counts[12]==1);
        } else {
            // attempted, no_candidate, no cooldown_not_attempted, cooldown_wait
            CHECK(counts[1]==1);CHECK(counts[6]==1);CHECK(counts[8]==0);CHECK(counts[17]==1);
        }
        for(auto* unit:{seeker,holder}){unit->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);unit->attachedBuilding=nullptr;}
    }
}

TEST_CASE("source four direct resource probes recover stale and fully claimed XY without replacing first success") {
    glob2test::HeadlessGlobals globals;
    for(unsigned mode:{3u,4u,5u})for(unsigned scenario:{0u,1u,2u})for(bool closest:{false,true}) {
        CAPTURE(mode);CAPTURE(scenario);CAPTURE(closest);
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=3211});
        auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig policy;policy.sourceVersion=mode==3?3:4;policy.directResourceProbes=mode==5;
        policy.selection=closest?ResourceClaims::ClaimPolicyConfig::Selection::ClosestGeometric:ResourceClaims::ClaimPolicyConfig::Selection::FirstAvailable;
        map.setMaterialClaimPolicyConfig(policy);
        auto* consumer=fixture.addBuilding("inn",20,20);auto* seeker=worker(fixture,consumer,0);auto* holder=worker(fixture,consumer,1);
        const auto old=Uint32(map.coordToIndex(12,8)),fresh=Uint32(map.coordToIndex(10,8));
        map.setResourceByIndex(12,8,WHEAT,1);map.setMaterialAmountSlot(old,WHEAT,1);
        const auto* field=glob2test::awaitGradient(fixture.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,seeker->movementAccess());});
        const auto lease=map.materialGradientDestinations(field);REQUIRE(lease);CHECK(lease->x[map.coordToIndex(8,8)]==12);
        map.setResourceByIndex(10,8,WHEAT,1);map.setMaterialAmountSlot(fresh,WHEAT,1);
        if(scenario==0)map.setMaterialAmountSlot(old,WHEAT,0);
        else if(scenario==1)REQUIRE(holder->acquireMaterialClaimAt(old));
        const auto random=seeker->entityRandom;const bool selected=seeker->selectActiveMaterialClaim(field);
        const bool expected=scenario==2 || mode==5;CHECK(selected==expected);CHECK(seeker->entityRandom==random);
        if(expected){REQUIRE(seeker->activeMaterialClaim);CHECK(seeker->activeMaterialClaim->access==((scenario!=2 || (closest && mode==5))?fresh:old));}
        for(auto* unit:{seeker,holder}){unit->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);unit->attachedBuilding=nullptr;}
    }
}
TEST_CASE("raw probes reuse natural goal eligibility and pooled source reservation") {
    glob2test::HeadlessGlobals globals;
    for(unsigned scenario:{0u,1u,2u,3u,4u,5u}) {
        // forbidden, wrong material, infinite stone, farm donor, water algae,
        // invisible fruit: raw coordinates never bypass natural-goal rules.
        CAPTURE(scenario);
        glob2test::HeadlessGame::Options options;options.discovered=true;options.clearImmobile=true;options.loadDefaultRace=true;options.header=true;
        if(scenario==3)options.experiments.set(ExperimentId::FarmAreas,true);
        glob2test::HeadlessGame fixture(options);auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig policy;policy.sourceVersion=4;policy.directResourceProbes=true;map.setMaterialClaimPolicyConfig(policy);
        auto* consumer=fixture.addBuilding("inn",20,20);auto* seeker=worker(fixture,consumer,0);auto* other=worker(fixture,consumer,1);
        const int material=scenario==2?STONE:scenario==4?ALGA:scenario==5?CHERRY:WHEAT;
        seeker->destinationPurpose=material;other->destinationPurpose=material;
        if(scenario==4){seeker->setPerformance(SWIM,1);other->setPerformance(SWIM,1);map.paintCell(12,8,WATER);map.paintCell(9,8,WATER);}
        const auto old=Uint32(map.coordToIndex(12,8)),fresh=Uint32(map.coordToIndex(9,8));
        map.setResourceByIndex(12,8,material,1);map.setMaterialAmountSlot(old,material,1);map.fogOfWar[old]|=seeker->owner->me;
        const auto* field=glob2test::awaitGradient(fixture.game,[&]{return map.getMaterialGradientSlot(0,material,seeker->movementAccess());});
        const auto lease=map.materialGradientDestinations(field);REQUIRE(lease);
        map.setMaterialAmountSlot(old,material,0);
        map.setResourceByIndex(9,8,scenario==1?WOOD:material,1);map.setMaterialAmountSlot(fresh,scenario==1?WOOD:material,1);
        map.fogOfWar[fresh]|=seeker->owner->me;
        if(scenario==0)map.addForbidden(9,8,0);
        if(scenario==5)map.fogOfWar[fresh]&=~seeker->owner->me;
        const auto donor=Uint32(map.coordToIndex(10,8));
        if(scenario==3){map.setResourceByIndex(10,8,WHEAT,1);map.setMaterialAmountSlot(donor,WHEAT,2);map.addFarmArea(9,8,0);map.addFarmArea(10,8,0);}
        const bool selected=seeker->selectActiveMaterialClaim(field);CHECK(selected==(scenario>=2 && scenario<=4));
        if(selected) {
            REQUIRE(seeker->activeMaterialClaim);CHECK(seeker->activeMaterialClaim->access==fresh);
            CHECK(seeker->activeMaterialClaim->source==(scenario==3?donor:fresh));CHECK(seeker->activeMaterialClaimValid());
            CHECK(other->acquireMaterialClaimAt(fresh)==(scenario==2));
            if(scenario==3)CHECK(seeker->activeMaterialClaim->reserve==1);
        }
        for(auto* unit:{seeker,other}){unit->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);unit->attachedBuilding=nullptr;}
    }
}

TEST_CASE("mixed source five uses exact D4 typed direct entry after stale initial XY") {
    glob2test::HeadlessGlobals globals;
    for(unsigned frame=0;frame<8;++frame)for(bool available:{false,true}) {
        CAPTURE(frame);CAPTURE(available);
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=3211});
        auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig policy;policy.sourceVersion=5;
        policy.pattern="illustrative258-middle13-v1";
        policy.patternFrame=ResourceClaims::ClaimPolicyConfig::PatternFrame::NearestD4;
        map.setMaterialClaimPolicyConfig(policy);
        auto* consumer=fixture.addBuilding("inn",20,20);auto* seeker=worker(fixture,consumer,0);
        const auto transform=[&](int x,int y){if(frame&4)std::swap(x,y);if(frame&1)x=-x;if(frame&2)y=-y;return std::pair{x,y};};
        const auto [dx,dy]=transform(4,2);const int nx=8+dx,ny=8+dy;
        const auto old=Uint32(map.coordToIndex(nx,ny));
        map.setResourceByIndex(nx,ny,WHEAT,1);map.setMaterialAmountSlot(old,WHEAT,1);
        const auto* field=glob2test::awaitGradient(fixture.game,[&]{return map.getMaterialGradientSlot(0,WHEAT,seeker->movementAccess());});
        const auto lease=map.materialGradientDestinations(field);REQUIRE(lease);
        REQUIRE(lease->x[map.coordToIndex(8,8)]==nx);REQUIRE(lease->y[map.coordToIndex(8,8)]==ny);
        const auto [rx,ry]=transform(3,1);const auto fresh=Uint32(map.coordToIndex(nx+rx,ny+ry));
        map.setResourceByIndex(nx+rx,ny+ry,WHEAT,1);map.setMaterialAmountSlot(fresh,WHEAT,1);
        if(!available)map.setMaterialAmountSlot(old,WHEAT,0);
        const auto random=seeker->entityRandom;REQUIRE(seeker->selectActiveMaterialClaim(field));
        REQUIRE(seeker->activeMaterialClaim);CHECK(seeker->activeMaterialClaim->access==(available?old:fresh));
        CHECK(seeker->materialClaimPolicyState.probe==(available?Uint32(map.coordToIndex(8,8)):fresh));
        CHECK(seeker->entityRandom==random);CHECK(seeker->activeMaterialClaimValid());
        const auto task=seeker->materialClaimTask;const auto probe=seeker->materialClaimPolicyState.probe;
        REQUIRE(seeker->selectActiveMaterialClaim(field));CHECK(seeker->materialClaimTask==task);CHECK(seeker->materialClaimPolicyState.probe==probe);
        seeker->releaseActiveMaterialClaim(ResourceClaims::ReleaseReason::Other);seeker->attachedBuilding=nullptr;
    }
}

TEST_CASE("release reasons count harvest route failure job end and invalidation causes") {
    glob2test::HeadlessGlobals globals;
    using R=ResourceClaims::ReleaseReason;
    std::vector<Uint32> reference;
    for(bool observe:{false,true}) {
        CAPTURE(observe);
        glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=1937});
        auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);
        ResourceClaims::ClaimPolicyConfig policy;policy.maximumExpansions=1;map.setMaterialClaimPolicyConfig(policy);
        auto* consumer=fixture.addBuilding("inn",20,20);
        std::vector<JourneyUnit*> units;for(int slot=0;slot<5;++slot)units.push_back(worker(fixture,consumer,slot));
        // Unit k stands at (8,8+k). Harvest and invalidation stock is adjacent;
        // the route-failure claim is far beyond a one-expansion search.
        const std::array<std::pair<int,int>,5> stock{{{9,8},{24,9},{9,10},{9,11},{9,12}}};
        for(const auto& [x,y]:stock){map.setResourceByIndex(x,y,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(x,y),WHEAT,2);}
        const auto path=(glob2test::artifactDir()/"release-reasons.jsonl.gz").string();
        std::shared_ptr<ResourceClaims::Probe> probe;
        if(observe) {std::filesystem::remove(path);ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=16;
            probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);}
        for(unsigned i=0;i<units.size();++i)REQUIRE(units[i]->acquireMaterialClaimAt(map.coordToIndex(stock[i].first,stock[i].second)));
        std::vector<Uint32> checksums{fixture.game.checkSum()};
        units[0]->dx=1;units[0]->dy=0;REQUIRE(units[0]->takeActiveMaterialClaim());
        fixture.game.invalidateObservation();fixture.game.observeCompletedWorld();
        units[1]->handleActiveMaterialMovement();REQUIRE(units[1]->activeMaterialClaim);
        for(unsigned attempt=0;attempt<8 && units[1]->activeMaterialClaim;++attempt) {
            map.flushClaimRoutes();++fixture.game.stepCounter;map.publishClaimRoutes();
            fixture.game.invalidateObservation();fixture.game.observeCompletedWorld();
            units[1]->handleActiveMaterialMovement();
        }
        CHECK_FALSE(units[1]->activeMaterialClaim);
        units[2]->endJob();CHECK_FALSE(units[2]->activeMaterialClaim);
        units[3]->displacement=Unit::DIS_RANDOM;units[3]->reconcileMaterialClaim();CHECK_FALSE(units[3]->activeMaterialClaim);
        // Exhausting reserved stock invalidates its owners at the cell itself.
        map.setMaterialAmountSlot(map.coordToIndex(9,12),WHEAT,0);units[4]->reconcileMaterialClaim();CHECK_FALSE(units[4]->activeMaterialClaim);
        checksums.push_back(fixture.game.checkSum());
        if(!observe)reference=checksums;
        else {
            CHECK(checksums==reference);
            const auto& m=probe->liveMetrics();
            std::array<Uint64,unsigned(R::Count)> expected{};
            for(auto reason:{R::Harvest,R::RouteFailed,R::JobEnded,R::UnitStateChanged,R::MapInvalidation})expected[unsigned(reason)]=1;
            CHECK(m.releaseReasons==expected);
            CHECK(m.events[unsigned(ResourceClaims::Probe::LiveEvent::Released)]==5);
            probe->finish();
            const auto trace=rows(path);const auto& live=trace.back()["live"];
            CHECK(live["release_reason_version"]==1);
            CHECK(live["release_reason_names"]==nlohmann::json(ResourceClaims::ReleaseReasonNames));
            CHECK(live["release_reason_counts"]==nlohmann::json(expected));
            CHECK(live["selection_filter_counts"]["terrain_prefilter_rejections"]==0);
            std::multiset<std::string> reasons;
            for(const auto& row:trace)if(row["kind"]=="journey") {
                if(row["terminal"]=="released")reasons.insert(row["release_reason"].get<std::string>());
                else CHECK(row["release_reason"].is_null());
            }
            CHECK(reasons==std::multiset<std::string>{"route_failed","job_ended","unit_state_changed","map_invalidation"});
        }
        for(auto* unit:units){unit->releaseActiveMaterialClaim(R::Other);unit->attachedBuilding=nullptr;}
    }
}
}
