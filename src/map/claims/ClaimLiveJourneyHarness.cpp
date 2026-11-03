// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "ResourceClaimProbe.h"
#include "ClaimPolicyConfig.h"
#include <nlohmann/json.hpp>
#include <zlib.h>
#include <sstream>
#include <filesystem>

namespace {
struct JourneyUnit : Unit {
    JourneyUnit(int x,int y,int slot,Team* team):Unit(x,y,GIDfrom(slot,0),WORKER,team,0) {}
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
            CHECK(trace.back()["completed"]==0);CHECK(trace.back()["live"]["journey_completed"]==1);
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
    for(auto* unit:units){unit->releaseActiveMaterialClaim();unit->attachedBuilding=nullptr;}
}
}
