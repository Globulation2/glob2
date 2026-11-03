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
namespace {
void checkBlockerHistograms(const nlohmann::json& group) {
    const auto sum=[](const nlohmann::json& values) {Uint64 result=0;for(const auto& v:values)result+=v.get<Uint64>();return result;};
    REQUIRE(group["histograms"].size()==3);Uint64 total=0;
    for(const auto& h:group["histograms"]) {
        REQUIRE(h["source_distance_tiles"].size()==11);REQUIRE(h["access_distance_tiles"].size()==11);
        REQUIRE(h["exact_age_ticks"].size()==9);REQUIRE(h["lower_bound_age_ticks"].size()==9);
        const auto owners=h["potential_conflicting_owners"].get<Uint64>();total+=owners;
        CHECK(sum(h["source_distance_tiles"])==owners);CHECK(sum(h["access_distance_tiles"])==owners);
        CHECK(sum(h["exact_age_ticks"])+sum(h["lower_bound_age_ticks"])+h["age_unavailable"].get<Uint64>()==owners);
    }
    CHECK(total==group["conflicting_owners"].get<Uint64>());
}
}
TEST_SUITE("ClaimLiveJourney") {
TEST_CASE("movement-only observation preserves classic and active worlds without oracle work") {
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
                if(observe)probe->decision(fixture.game,*unit,nullptr); // Must not capture or dereference a field.
                unit->committedMove(1,0);checksums.push_back(fixture.game.checkSum());
            }
            CHECK(unit->entityRandom==random);CHECK(map.materialClaimTrackingEnabled()==false);
            CHECK(map.activeClaimsEnabled()==active);
            if(!observe)reference=checksums;
            else {
                CHECK(checksums==reference);probe->publish(3);probe->finish();
                CHECK(probe->movementMetrics().totalMoves==3);
                CHECK(probe->movementMetrics().moves[unsigned(active?ResourceClaims::Probe::MovementKind::ClaimedResource:ResourceClaims::Probe::MovementKind::UnclaimedResource)]==3);
                CHECK(probe->metrics().decisions==0);CHECK(probe->metrics().captureBytes==0);
                CHECK(probe->liveMetrics().journeyEligible==0);CHECK(probe->liveMetrics().moves==0);
                const auto trace=rows(path);REQUIRE(trace.size()==2);
                CHECK(trace.front()["movement_only"]==true);CHECK(trace.front()["decision_sampling_enabled"]==false);
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
TEST_CASE("sampled blocker census preserves checksums and separates exact inherited and requester ages") {
    glob2test::HeadlessGlobals globals;
    std::vector<Uint32> reference;
    for(bool observe:{false,true}) {
        glob2test::HeadlessGame fixture({.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=3183});
        auto& map=fixture.game.map;map.setMaterialDestinationsEnabled(true);map.setMaterialClaimPolicyConfig(ResourceClaims::ClaimPolicyConfig{});
        auto definitions=nlohmann::json::parse(map.resourceRegistry().serialize());
        definitions["resources"][WHEAT]["yields"]["food"]["capacity"]=20;
        map.installResourceDefinitions(definitions.dump());
        auto* consumer=fixture.addBuilding("inn",20,20);
        const auto target=Uint32(map.coordToIndex(12,8));map.setResourceByIndex(12,8,WHEAT,18);map.setMaterialAmountSlot(target,WHEAT,18);
        std::vector<Unit*> units;for(int i=0;i<19;++i) {
            if(i!=17)units.push_back(worker(fixture,consumer,i));
            else {
                auto* foreign=fixture.addUnit(WORKER,8,25,1);foreign->attachedBuilding=fixture.addBuilding("inn",20,2,0,1);
                foreign->activity=Unit::ACT_FILLING;foreign->medical=Unit::MED_FREE;foreign->displacement=Unit::DIS_GOING_TO_RESOURCE;foreign->destinationPurpose=WHEAT;
                units.push_back(foreign);
            }
        }
        REQUIRE(units[0]->acquireMaterialClaimAt(target)); // Inherited by a later observer.
        fixture.game.stepCounter=5;
        const auto path=(glob2test::artifactDir()/"blocker-census.jsonl.gz").string();
        std::shared_ptr<ResourceClaims::Probe> probe;
        if(observe) {
            std::filesystem::remove(path);ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=32;
            probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
        }
        for(unsigned i=1;i<18;++i)REQUIRE(units[i]->acquireMaterialClaimAt(target));
        std::vector<Uint16> field(map.cellCount(),0);
        fixture.game.stepCounter=15;
        if(observe)probe->decision(fixture.game,*units[18],field.data(),target);
        std::vector<Uint32> checksums{fixture.game.checkSum(nullptr,nullptr,nullptr,false)};
        fixture.game.stepCounter=20;
        if(observe)probe->liveEvent(*units[2],ResourceClaims::Probe::LiveEvent::HarvestBegin);
        units[2]->releaseActiveMaterialClaim();REQUIRE(units[2]->acquireMaterialClaimAt(target));
        fixture.game.stepCounter=25;
        if(observe)probe->decision(fixture.game,*units[18],field.data(),target);
        checksums.push_back(fixture.game.checkSum(nullptr,nullptr,nullptr,false));
        if(!observe)reference=checksums;
        else {
            CHECK(checksums==reference);probe->finish();
            std::vector<nlohmann::json> decisions;for(const auto& row:rows(path))if(row["kind"]=="decision")decisions.push_back(row);
            REQUIRE(decisions.size()==2);
            const auto& first=decisions[0]["blocker_census"];CHECK(first["available"]==true);CHECK(first["pooled"].is_null());CHECK(first["nearest_access_reservable"]==false);
            CHECK(first["direct"]["all_selected_goal_owners"]==18);CHECK(first["direct"]["owners"]==18);
            CHECK(first["direct"]["requester_owners"]==0);CHECK(first["direct"]["examples_excluded"]==2);
            CHECK(first["direct"]["exact_age_owners"]==17);CHECK(first["direct"]["lower_bound_age_owners"]==1);
            CHECK(first["direct"]["examples"].size()==16);
            CHECK(first["direct"]["known_team_owners"][0]==17);CHECK(first["direct"]["known_team_owners"][1]==1);
            checkBlockerHistograms(first["direct"]);
            const auto& firstHist=first["direct"]["histograms"];
            CHECK(firstHist[0]["potential_conflicting_owners"]==17);CHECK(firstHist[1]["potential_conflicting_owners"]==1);
            CHECK(firstHist[0]["exact_age_ticks"][1]==16);CHECK(firstHist[0]["lower_bound_age_ticks"][0]==1);
            CHECK(firstHist[1]["exact_age_ticks"][1]==1);CHECK(firstHist[2]["potential_conflicting_owners"]==0);
            CHECK(firstHist[0]["source_distance_tiles"][3]==8);CHECK(firstHist[0]["source_distance_tiles"][4]==8);
            CHECK(firstHist[0]["source_distance_tiles"][5]==1);CHECK(firstHist[1]["source_distance_tiles"][4]==1);
            CHECK(firstHist[0]["source_distance_tiles"]==firstHist[0]["access_distance_tiles"]);
            const auto& second=decisions[1]["blocker_census"]["direct"];
            CHECK(second["all_selected_goal_owners"]==18);CHECK(second["owners"]==18);CHECK(second["requester_owners"]==0);
            checkBlockerHistograms(second);
            CHECK(second["histograms"][0]["lower_bound_age_ticks"][1]==1);
            CHECK(second["histograms"][0]["exact_age_ticks"][1]==1);
            CHECK(second["histograms"][0]["exact_age_ticks"][2]==15);
            CHECK(second["histograms"][1]["exact_age_ticks"][2]==1);
            bool inherited=false,reacquired=false;
            for(const auto& example:second["examples"]) {
                CHECK(example["path_distance"].is_null());CHECK(example["route_state"].is_null());CHECK(example["team"]==0);
                if(example["owner"]==ResourceClaims::unitOwner(units[0]->gid,units[0]->scriptIdentity)) {
                    inherited=true;CHECK(example["claim_age_kind"]=="first-observed-lower-bound");CHECK(example["claim_age_ticks"]==10);
                }
                if(example["owner"]==ResourceClaims::unitOwner(units[2]->gid,units[2]->scriptIdentity)) {
                    reacquired=true;CHECK(example["claim_age_kind"]=="observed-acquisition-exact");CHECK(example["claim_age_ticks"]==5);
                }
            }
            CHECK(inherited);CHECK(reacquired);
        }
        for(auto* unit:units){unit->releaseActiveMaterialClaim();unit->attachedBuilding=nullptr;}
    }
}
TEST_CASE("pooled blocker census distinguishes reserve-only shortage from reserved physical sources") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame::Options options;options.discovered=true;options.clearImmobile=true;options.loadDefaultRace=true;options.header=true;
    options.experiments.set(ExperimentId::FarmAreas,true);
    glob2test::HeadlessGame fixture(options);auto& map=fixture.game.map;
    map.setMaterialDestinationsEnabled(true);map.setMaterialClaimPolicyConfig(ResourceClaims::ClaimPolicyConfig{});
    auto* consumer=fixture.addBuilding("inn",20,20);auto* first=worker(fixture,consumer,0);auto* second=worker(fixture,consumer,1);auto* query=worker(fixture,consumer,2);
    const auto target=Uint32(map.coordToIndex(12,8));
    for(int x:{12,13}){map.setResourceByIndex(x,8,WHEAT,1);map.setMaterialAmountSlot(map.coordToIndex(x,8),WHEAT,1);map.addFarmArea(x,8,0);}
    const auto path=(glob2test::artifactDir()/"pooled-blocker-census.jsonl.gz").string();std::filesystem::remove(path);
    ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=8;
    auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
    std::vector<Uint16> field(map.cellCount(),0);probe->decision(fixture.game,*query,field.data(),target);
    for(int x:{12,13})map.setMaterialAmountSlot(map.coordToIndex(x,8),WHEAT,2);
    REQUIRE(first->acquireMaterialClaimAt(target));REQUIRE(second->acquireMaterialClaimAt(target));
    fixture.game.stepCounter=4;probe->decision(fixture.game,*second,field.data(),target);probe->finish();
    std::vector<nlohmann::json> decisions;for(const auto& row:rows(path))if(row["kind"]=="decision")decisions.push_back(row);
    REQUIRE(decisions.size()==2);
    CHECK(decisions[0]["reason"]=="farm_reserve");CHECK(decisions[0]["blocker_census"]["pooled"]["owners"]==0);
    CHECK(decisions[0]["blocker_census"]["nearest_access_reservable"]==false);
    // The raw picked donor is full, but the requester's packet remains
    // available through this same pooled access under owner-aware semantics.
    CHECK(decisions[1]["blocker_census"]["nearest_access_reservable"]==true);
    CHECK(decisions[1]["blocker_census"]["nearest_access_static_reachable"]==true);
    CHECK(decisions[1]["blocker_census"]["pooled"]["all_selected_goal_owners"]==2);
    CHECK(decisions[1]["blocker_census"]["pooled"]["requester_owners"]==1);
    CHECK(decisions[1]["blocker_census"]["pooled"]["owners"]==1);
    CHECK(decisions[1]["blocker_census"]["direct"]["owners"]==1);
    for(const auto& decision:decisions) {
        checkBlockerHistograms(decision["blocker_census"]["direct"]);checkBlockerHistograms(decision["blocker_census"]["pooled"]);
    }
    CHECK(decisions[1]["blocker_census"]["pooled"]["histograms"][0]["potential_conflicting_owners"]==1);
    first->releaseActiveMaterialClaim();second->releaseActiveMaterialClaim();
    first->attachedBuilding=nullptr;second->attachedBuilding=nullptr;query->attachedBuilding=nullptr;
}

TEST_CASE("shadow competition reports authoritative blocker census unavailable") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=fixture.game.map;auto* consumer=fixture.addBuilding("inn",20,20);
    auto* claimant=worker(fixture,consumer,0);auto* query=worker(fixture,consumer,1);
    const auto target=Uint32(map.coordToIndex(12,8));map.setResourceByIndex(12,8,WHEAT,1);map.setMaterialAmountSlot(target,WHEAT,1);
    claimant->targetX=12;claimant->targetY=8;claimant->setValidTarget(true);map.setMaterialClaimTrackingEnabled(true);
    const auto path=(glob2test::artifactDir()/"shadow-blocker-census.jsonl.gz").string();std::filesystem::remove(path);
    ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=4;
    auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
    std::vector<Uint16> field(map.cellCount(),0);probe->decision(fixture.game,*query,field.data(),target);probe->finish();
    unsigned decisions=0;for(const auto& row:rows(path))if(row["kind"]=="decision") {
        ++decisions;CHECK(row["claimed"]==1);CHECK(row["blocker_census"]["available"]==false);
        CHECK(row["blocker_census"]["direct"].is_null());CHECK(row["blocker_census"]["pooled"].is_null());
        CHECK(row["blocker_census"]["nearest_access_reservable"].is_null());CHECK(row["blocker_census"]["nearest_access_static_reachable"].is_null());
    }
    CHECK(decisions==1);claimant->attachedBuilding=nullptr;query->attachedBuilding=nullptr;
}

TEST_CASE("blocker census distinguishes infinite sharing from exclusive deposit conflicts") {
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
    auto& map=fixture.game.map;auto definitions=nlohmann::json::parse(map.resourceRegistry().serialize());
    auto& food=definitions["resources"][WHEAT];food["yields"]["food"]["consumption"]="infinite";
    food["yields"]["wood"]={{"capacity",1},{"initial",1},{"consumption","all"},{"destroysDeposit",true}};
    map.installResourceDefinitions(definitions.dump());
    map.setMaterialDestinationsEnabled(true);map.setMaterialClaimPolicyConfig(ResourceClaims::ClaimPolicyConfig{});
    auto* consumer=fixture.addBuilding("inn",20,20);auto* query=worker(fixture,consumer);
    const auto target=Uint32(map.coordToIndex(12,8));map.setResourceByIndex(12,8,WHEAT,1);map.setMaterialAmountSlot(target,WHEAT,1);
    // Explicit diagnostic ledger fixture: compatibility classification must
    // remain truthful even if the imported ledger has stale/unresolved owners.
    // Normal enforced acquisition would never admit this conflicting pair.
    const auto sharing=ResourceClaims::unitOwner(Unit::GIDfrom(1000,0),99);
    const auto destructive=ResourceClaims::unitOwner(Unit::GIDfrom(1001,0),99);
    map.observeMaterialClaim(sharing,{target,target,WHEAT,map.getResource(target).type,false,0});
    map.observeMaterialClaim(destructive,{target,target,WOOD,map.getResource(target).type,true,0});
    const auto path=(glob2test::artifactDir()/"infinite-blocker-census.jsonl.gz").string();std::filesystem::remove(path);
    ResourceClaims::Probe::Config config;config.sampleEvery=1;config.maxSamples=4;
    auto probe=std::make_shared<ResourceClaims::Probe>(map.computeExecutor(),path,config);map.setMaterialClaimProbe(probe);
    std::vector<Uint16> field(map.cellCount(),0);probe->decision(fixture.game,*query,field.data(),target);probe->finish();
    unsigned decisions=0;for(const auto& row:rows(path))if(row["kind"]=="decision") {
        ++decisions;const auto& direct=row["blocker_census"]["direct"];
        CHECK(direct["owners"]==2);CHECK(direct["conflicting_owners"]==1);CHECK(direct["identity_unavailable"]==2);
        checkBlockerHistograms(direct);
        CHECK(direct["histograms"][2]["potential_conflicting_owners"]==1);
        CHECK(direct["histograms"][2]["source_distance_tiles"][10]==1);CHECK(direct["histograms"][2]["access_distance_tiles"][10]==1);
        CHECK(direct["histograms"][2]["age_unavailable"]==1);
        for(const auto& example:direct["examples"]) {
            CHECK(example["requested_yield_infinite"]==true);CHECK(example["team"].is_null());CHECK(example["task"].is_null());
            CHECK(example["claim_age_kind"]=="unavailable");CHECK(example["claim_age_ticks"].is_null());
            CHECK(example["conflicts_requested_material_or_exclusive"]==(example["owner"]==destructive));
        }
    }
    CHECK(decisions==1);map.releaseMaterialClaim(sharing);map.releaseMaterialClaim(destructive);query->attachedBuilding=nullptr;
}

}
