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
