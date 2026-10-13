// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "MovementPlanner.h"
#include "MovementShadow.h"
#include <StreamBackend.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <TextStream.h>
#include <limits>
#include <memory>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <set>

using namespace MovementPlanning;
namespace
{
std::string save(Game& game)
{
    auto* backend=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(backend);
    game.save(&output,false,"movement shadow continuity");output.flush();return backend->takeContents();
}
Request request(const SimulationSnapshot::Handle& world,Mode mode,int x=5,int y=5,int tx=10,int ty=5)
{
    Request result;result.task.unit={7,1};result.task.team=0;result.task.x=x;result.task.y=y;
    result.task.targetX=tx;result.task.targetY=ty;result.task.walk=16;result.mode=mode;
    result.observationTick=world.tick;result.publicationTick=world.tick+2;result.sequence=1;
    return result;
}
}
TEST_SUITE("MovementPlanning")
{
    TEST_CASE("agreement classifies all directions and stays without hiding exclusions")
    {
        Shadow::Metrics m;
        for(unsigned proposed=0;proposed<9;++proposed)for(unsigned actual=0;actual<9;++actual)
            Shadow::recordAgreement(m,proposed,actual);
        CHECK(m.compared==81);CHECK(m.directionAgree==9);CHECK(m.moveStayAgree==65);
        CHECK(m.bothMove==64);CHECK(m.movingDirectionAgree==8);CHECK(m.bothStay==1);
        CHECK(m.proposalMoveReferenceStay==8);CHECK(m.proposalStayReferenceMove==8);
        for(const auto& row:m.confusion)for(auto count:row)CHECK(count==1);
        CHECK_THROWS(Shadow::recordAgreement(m,9,0));CHECK(m.compared==81);
    }
    TEST_CASE("dense agreement sweep stays diagnostic and publishes zero at its boundary")
    {
        glob2test::HeadlessGlobals globals;
        std::vector<Uint32> baseline;std::string baselineSave;
        for(unsigned delay=0;delay<=9;++delay) {
            glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true,.seed=71});
            auto* unit=fixture.addUnit(WORKER,8,8);REQUIRE(unit);
            if(delay<=8)fixture.game.configureMovementShadow(delay,true);
            if(delay==0) {
                fixture.game.captureReadBoundary({},false);
                const auto stats=fixture.game.movementMetrics();
                const auto published=std::find_if(stats.begin(),stats.end(),[](const auto& p){return p.first=="team_0_idle_published";});
                REQUIRE(published!=stats.end());CHECK(published->second==1);
            }
            std::vector<Uint32> checks;
            for(int tick=0;tick<128;++tick){fixture.step();checks.push_back(fixture.checksum());}
            fixture.game.drainMovementShadow();const auto bytes=save(fixture.game);
            if(baseline.empty()){baseline=checks;baselineSave=bytes;}else{CHECK(checks==baseline);CHECK(bytes==baselineSave);}
            if(delay<=8) {
                const auto stats=fixture.game.movementMetrics();
                const auto compared=std::find_if(stats.begin(),stats.end(),[](const auto& p){return p.first=="team_0_idle_agreement_compared";});
                REQUIRE(compared!=stats.end());CHECK(compared->second>0);
            }
        }
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        CHECK_THROWS(fixture.game.configureMovementShadow(9,true));
        CHECK_THROWS(fixture.game.configureMovementShadow(3));
        fixture.game.configureMovementShadow(0);CHECK_FALSE(fixture.game.movementDiagnostics()->planningEnabled());
    }
    TEST_CASE("completion prediction agrees with the integer action clock")
    {
        UnitState u{};u.core.displacement=UnitState::DIS_RANDOM;
        for(auto action:{WALK,SWIM,FLY,STOP_WALK})for(int speed:{0,1,16,255,300})for(int delta:{0,12,254,255})for(int diagonal:{0,1}) {
            u.core.action=action;u.core.speed=speed;u.core.delta=delta;u.core.dx=1;u.core.dy=diagonal;
            const int advance=unitActionStepSpeed(speed,action,1,diagonal);
            const auto predicted=completionTicks(u.core);
            if(!advance){CHECK_FALSE(predicted);continue;}
            int actual=1,phase=delta;
            while(phase<=UNIT_DELTA_MAX-advance){phase+=advance;++actual;}
            REQUIRE(predicted);CHECK(*predicted==actual);
        }
    }
    TEST_CASE("request randomness isolates incarnation and full sequence")
    {
        Request r;r.task.unit={12,23};r.sequence=1;
        auto a=requestRandom(17,r),b=requestRandom(17,r);
        for(int i=0;i<64;++i)CHECK(a.nextU32()==b.nextU32());
        auto original=requestRandom(17,r).exportState();
        r.sequence+=Uint64(1)<<32;CHECK(requestRandom(17,r).exportState()!=original);
        r.sequence=1;r.task.unit.generation++;CHECK(requestRandom(17,r).exportState()!=original);
    }
    TEST_CASE("snapshot planner ranks routes across the torus and never mutates inputs")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        auto& game=fixture.game;
        const auto checksum=game.checkSum();const auto random=game.syncRandom;
        const auto world=game.captureReadBoundary({},false,Shadow::requirements);
        Planner planner;
        auto r=request(world,Mode::Building,0,5,31,5);
        const auto result=planner.plan(world,r,1);
        CHECK(validPermutation(result));CHECK(result.directions[0]==7);CHECK(result.preferred>0);
        CHECK(result.disposition==Disposition::Travel);
        CHECK(game.checkSum()==checksum);CHECK(game.syncRandom==random);
        const auto again=planner.plan(world,r,1);
        CHECK(again.directions==result.directions);CHECK(planner.metrics.cacheHits==1);
        auto malformed=result;malformed.directions[1]=malformed.directions[0];CHECK_FALSE(validPermutation(malformed));
        malformed=result;malformed.preferred=9;CHECK_FALSE(validPermutation(malformed));
    }
    TEST_CASE("mobile occupancy is deferred while structural obstacles remain route inputs")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        REQUIRE(fixture.addUnit(WORKER,6,5));
        const auto world=fixture.game.captureReadBoundary({},false,Shadow::requirements);
        auto r=request(world,Mode::Building);
        CHECK(legal(world,r.task,6,5,false));CHECK_FALSE(legal(world,r.task,6,5,true));
        Planner planner;CHECK(planner.plan(world,r,1).directions[0]==3);
    }
    TEST_CASE("compatible building goals share fields without sharing caller task identity")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        auto* goal=fixture.addBuilding("inn",10,10);REQUIRE(goal);
        const auto world=fixture.game.captureReadBoundary({},false,Shadow::requirements);
        auto first=request(world,Mode::Building);
        first.task.target=Game::refOf(goal);
        Planner shared;
        shared.plan(world,first,1);
        auto second=first;
        second.task.unit={9,3};second.task.x=6;second.task.y=7;
        second.task.attached={15,4};second.task.targetX=20;second.task.targetY=21;
        second.task.material=1;second.task.capabilities=123;
        second.task.displacement=UnitState::DIS_GOING_TO_BUILDING;
        const auto reused=shared.plan(world,second,1);
        Planner fresh;const auto expected=fresh.plan(world,second,1);
        CHECK(shared.metrics.fields==1);CHECK(shared.metrics.cacheHits==1);
        CHECK(reused.request.task==second.task);
        CHECK(reused.directions==expected.directions);CHECK(reused.preferred==expected.preferred);
        CHECK(reused.disposition==expected.disposition);
        REQUIRE(reused.destination);REQUIRE(expected.destination);
        CHECK(reused.destination->x==expected.destination->x);
        CHECK(reused.destination->y==expected.destination->y);
        CHECK(reused.destination->supplier==expected.destination->supplier);
        // An unresolved target uses the requested coordinates instead.
        second.task.target={};second.task.targetX=2;second.task.targetY=2;
        shared.plan(world,second,1);CHECK(shared.metrics.fields==2);
    }
    TEST_CASE("captured tower hazards match the live range and projectile policy")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::GameOptions options{.teams=2,.loadDefaultRace=true,.header=true};
        options.experiments.set(ExperimentId::RidgeTerrain);
        glob2test::HeadlessGame fixture(options);
        auto* unit=fixture.addUnit(EXPLORER,4,4);REQUIRE(unit);
        REQUIRE(fixture.addBuilding("defencetower",8,8,0,1));
        fixture.game.teams[0]->enemies=2;
        fixture.game.map.paintCell(7,8,RIDGE_ROCK);
        const auto world=fixture.game.captureReadBoundary({},false,Shadow::requirements);
        auto r=request(world,Mode::Explore);r.task.fly=unit->performance(FLY);
        for(int y=0;y<32;++y)for(int x=0;x<32;++x)
            CHECK(towerThreat(world,r.task,x,y)==unit->locationIsInEnemyGuardTowerRange(x,y));
    }
    TEST_CASE("bounded search reports unknown without claiming unreachable")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.wDec=6,.hDec=6,.loadDefaultRace=true,.header=true});
        const auto world=fixture.game.captureReadBoundary({},false,Shadow::requirements);
        Planner planner;auto r=request(world,Mode::Pursuit,0,0,25,25);
        CHECK(planner.plan(world,r,1).disposition==Disposition::NoRoute);
        CHECK(planner.metrics.unknown==1);
        Planner tiny(128);CHECK_THROWS_AS(tiny.plan(world,r,1),std::invalid_argument);
    }
    TEST_CASE("exploration capture tracks refresh decay initialization and independent leases")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        auto& game=fixture.game;auto& map=game.map;
        const auto requirement=SimulationSnapshot::bit(SimulationSnapshot::Component::Exploration);
        const auto first=game.captureReadBoundary({},false,requirement);
        map.setMapExploredByUnit(1,1,1,1,0);
        const auto next=game.captureReadBoundary({},false,requirement);
        const auto index=map.coordToIndex(1,1);
        CHECK(next.exploration!=first.exploration);CHECK(next.exploration->teams[0][index]==255);
        CHECK_NOTHROW(SimulationSnapshot::verifyCapture(game,next));
        const auto projected=next.project(requirement);CHECK_FALSE(projected.entities);CHECK_FALSE(projected.terrain);
        const auto unchanged=game.captureReadBoundary({},false,requirement);CHECK(unchanged.exploration==next.exploration);
        // Public stepping decays team zero on tick zero.
        fixture.step();
        const auto decayed=game.captureReadBoundary({},false,requirement);
        CHECK_NOTHROW(SimulationSnapshot::verifyCapture(game,decayed));
        CHECK(next.exploration->teams[0][index]==255);
    }
    TEST_CASE("all delays and participant counts preserve reference checksums and save bytes")
    {
        glob2test::HeadlessGlobals globals;
        std::vector<Uint32> baseline;std::string baselineSave;
        std::map<unsigned,std::vector<std::pair<std::string,Uint64>>> shadowStatistics;
        for(unsigned participants:{1u,2u,4u,8u})for(unsigned delay:{99u,0u,1u,2u,4u,8u}) {
            glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true,.seed=71});
            REQUIRE(fixture.addUnit(WORKER,8,8));REQUIRE(fixture.addUnit(WORKER,12,12));
            fixture.game.map.configureCompute(participants);
            if(delay!=99)fixture.game.configureMovementShadow(delay);
            std::vector<Uint32> checksums;
            for(int tick=0;tick<128;++tick){fixture.step();checksums.push_back(fixture.checksum());}
            fixture.game.drainMovementShadow();
            const auto contents=save(fixture.game);
            if(baseline.empty()){baseline=checksums;baselineSave=contents;}
            else {CHECK(checksums==baseline);CHECK(contents==baselineSave);}
            if(delay==99)continue;
            auto metrics=fixture.game.movementMetrics();
            std::vector<std::pair<std::string,Uint64>> statistics;
            for(const auto& metric:metrics)
                if(metric.first.starts_with("team_") && metric.first.find("reference_")==std::string::npos)
                    statistics.push_back(metric);
            if(participants==1)shadowStatistics[delay]=statistics;
            else CHECK(statistics==shadowStatistics.at(delay));
            auto found=std::find_if(metrics.begin(),metrics.end(),[](auto& pair){return pair.first=="maximum_batches";});
            REQUIRE(found!=metrics.end());CHECK(found->second<=delay+1);
        }
    }
    TEST_CASE("paused and repeated observations admit at most once and never publish early")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        REQUIRE(fixture.addUnit(WORKER,8,8));fixture.game.configureMovementShadow(4);
        const auto value=[&](const char* name){for(const auto& [key,value]:fixture.game.movementMetrics())if(key==name)return value;return Uint64(0);};
        fixture.game.captureReadBoundary({},true);CHECK(value("maximum_batches")==0);
        fixture.game.captureReadBoundary({},false);fixture.game.captureReadBoundary({},false);
        CHECK(value("maximum_batches")==1);fixture.game.drainMovementShadow();
        CHECK(value("team_0_idle_published")==0);
        CHECK_THROWS(fixture.game.configureMovementShadow(3));
    }
    TEST_CASE("order acknowledgement invalidation does not recapture an admitted movement tick")
    {
        glob2test::HeadlessGlobals globals;
        glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
        REQUIRE(fixture.addUnit(WORKER,8,8));
        auto& game=fixture.game;
        game.configureMovementShadow(4);
        game.captureReadBoundary({},false);
        const auto captures=game.snapshots().metrics.captures;
        // The engine can invalidate presentation/session inputs after admission.
        game.snapshots().invalidateBoundary();
        game.syncStep(0,Game::PreparationCompletion::Deferred);
        CHECK(game.snapshots().metrics.captures==captures);
        CHECK(game.movementDiagnostics()->needsBoundary(game.stepCounter));
        game.captureReadBoundary({},false);
        CHECK_FALSE(game.movementDiagnostics()->needsBoundary(game.stepCounter));
    }
}

TEST_SUITE("MovementPlanningFixtures")
{
    TEST_CASE("retain deterministic colony and contention inputs [benchmark]")
    {
        // Export explicitly; ordinary test runs need no filesystem artifacts.
        const char* destination=std::getenv("GLOB2_MOVEMENT_FIXTURE_DIR");
        if(!destination)return;
        namespace fs=std::filesystem;
        const fs::path directory=destination;
        fs::create_directories(directory);
        glob2test::HeadlessGlobals globals;
        for(const char* name:{"ordinary-control","large-colony","contention","team-imbalance"}) {
            const bool ordinary=std::string(name)=="ordinary-control";
            const bool multi=std::string(name)=="team-imbalance";
            const bool narrow=std::string(name)=="contention";
            const int teams=multi?4:1, dimension=ordinary?64:128;
            glob2test::HeadlessGame fixture({.wDec=ordinary?6:7,.hDec=ordinary?6:7,.teams=teams,
                .clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=713});
            fixture.game.gameHeader.getWinningConditions().clear(); // fixed-duration compute fixtures
            // Distributed inns and renewable wheat provide normal hunger and
            // service work. Long-running sustainability remains a qualification
            // check, not an assumption implied by this layout.
            for(int team=0;team<teams;++team) {
                const int x0=multi?(team%2)*64:0,y0=multi?(team/2)*64:0;
                const int extent=multi?64:dimension;
                for(int y=y0+4;y<y0+extent-8;y+=16)for(int x=x0+4;x<x0+extent-8;x+=16) {
                    auto* inn=fixture.addBuilding("inn",x,y,0,team);REQUIRE(inn);
                    inn->maxUnitWorking=inn->maxUnitWorkingPreferred=inn->desiredMaxUnitWorking=8;
                    for(int ry=y+6;ry<y+10;++ry)for(int rx=x;rx<x+4;++rx) {
                        fixture.game.map.setResourceByIndex(rx,ry,WHEAT,0);
                        fixture.game.map.setResourcesGrow(rx,ry,1);
                        fixture.game.map.setFertility(rx,ry,65535);
                    }
                }
            }
            if(narrow)for(int y=0;y<dimension;++y)if(y<60 || y>67)
                fixture.game.map.setAreaMask(fixture.game.map.coordToIndex(64,y),&Tile::forbidden,1);
            const int total=ordinary?32:1024;
            int added=0;
            for(int candidate=0;candidate<dimension*dimension && added<total;++candidate) {
                const int cell=(candidate*13007)&(dimension*dimension-1);
                const int x=cell%dimension,y=cell/dimension;
                const int team=multi?(x>=64)+2*(y>=64):0;
                if(!fixture.game.map.isFreeForGroundUnit(x,y,false,1u<<team))continue;
                if(multi && fixture.game.teams[team]->liveUnits.size()>=256)continue;
                REQUIRE(fixture.addUnit(WORKER,x,y,team));++added;
            }
            REQUIRE(added==total);
            auto header=fixture.game.gameHeader;
            for(int team=0;team<teams;++team)header.getBasePlayer(team).type=BasePlayer::P_AI; // existing no-op AI, fixed null orders
            fixture.game.setGameHeader(header,true);
            std::ofstream output(directory/(std::string(name)+".game"),std::ios::binary);
            auto* backend=new GAGCore::MemoryStreamBackend;
            GAGCore::BinaryOutputStream stream(backend);
            fixture.gui.save(&stream,std::string("movement fixture ")+name);stream.flush();
            const auto bytes=backend->takeContents();output.write(bytes.data(),bytes.size());REQUIRE(output.good());
        }
    }
}

TEST_CASE("pending movement leases omit full unit details" * doctest::test_suite("MovementPlanning"))
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame fixture({.loadDefaultRace=true,.header=true});
    REQUIRE(fixture.addUnit(WORKER,8,8));
    std::weak_ptr<const SimulationSnapshot::UnitDetails> details;
    Shadow shadow(fixture.game.map.computeExecutor(),4,1,fixture.game.map.getW(),fixture.game.map.getH());
    {
        auto world=SimulationSnapshot::capture(fixture.game,SimulationSnapshot::captureCatalog(fixture.game));
        details=world.unitDetails;
        shadow.boundary(fixture.game,world);
    }
    CHECK(details.expired());
    shadow.drain();
}
