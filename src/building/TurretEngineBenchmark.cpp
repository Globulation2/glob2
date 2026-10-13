// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "TurretTargeting.h"
#include "AI.h"
#include "Player.h"
#include "Order.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <map>
#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#else
#define CALLGRIND_START_INSTRUMENTATION ((void)0)
#define CALLGRIND_STOP_INSTRUMENTATION ((void)0)
#define CALLGRIND_ZERO_STATS ((void)0)
#define CALLGRIND_DUMP_STATS_AT(label) ((void)0)
#endif

namespace
{
enum Mode { Original,Shadow,Cached };
struct Selection {
    Mode mode=Original;
    unsigned attempts=0,found=0,shadowValid=0;
    bool diagnostics=false;
    Uint32 networkDigest=0;
    Uint64 scanCpu=0,consumptionCpu=0,boundaryCpu=0,syncCpu=0,syncWall=0;
    struct Window { TurretTargeting::Entry reference; unsigned start=0; };
    std::map<std::pair<Uint16,Uint32>,Window> windows;
    unsigned opportunities=0,agreement=0,missed=0;
    std::vector<unsigned> delays;
    TurretTargeting::Metrics rejections;
};
thread_local Selection* selection=nullptr;
unsigned capacity=8;
bool forecast=true,reusePreparation=true,profileCpu=false,opportunities=false;
bool profileOwner=false;
bool callgrindRegion=false;
const char* variant="combined";
void configure() {
    if (const auto* value=std::getenv("TURRET_VARIANT")) variant=value;
    forecast=std::string(variant)=="forecast" || std::string(variant)=="combined";
    reusePreparation=std::string(variant)=="preparation" || std::string(variant)=="combined";
    if (const auto* value=std::getenv("TURRET_CAPACITY")) capacity=std::stoul(value);
    profileCpu=std::getenv("TURRET_PROFILE_CPU")!=nullptr;
    profileOwner=profileCpu || std::getenv("TURRET_PROFILE_OWNER")!=nullptr;
    opportunities=std::getenv("TURRET_OPPORTUNITIES")!=nullptr;
}
constexpr std::array<unsigned,2> seats{0,1};
const char* modeName(Mode mode) { return mode==Original ? "original" : mode==Shadow ? "shadow" : "cached"; }
struct Run {
    Uint64 wall=0,cpu=0,ownerCpu=0;
    Selection targeting;
    Uint64 shots=0,units=0,turrets=0,captures=0,indexBytes=0,listBytes=0;
    TurretTargeting::Pipeline::Statistics pipeline;
    std::vector<Uint32> checksums;
    std::vector<std::string> orders;
    std::vector<char> save;
};
void followWindow(void*,const Building& b)
{
    if (!selection || !opportunities || selection->mode!=Shadow) return;
    const auto key=std::pair{b.gid,b.scriptIdentity};
    const auto it=selection->windows.find(key);
    if (it==selection->windows.end()) return;
    const auto reference=TurretTargeting::firstValid(b,{it->second.reference});
    if (!reference.found()) { ++selection->missed; selection->windows.erase(it); return; }
    if (TurretTargeting::firstValid(b,b.owner->game->turretShadow()->entries(b)).found()) {
        selection->delays.push_back(b.owner->game->stepCounter-it->second.start);
        selection->windows.erase(it);
    }
}
void setup(glob2test::HeadlessGame& world,unsigned fixture,unsigned seed,unsigned participants,Mode mode)
{
    auto& game=world.game;
    GameHeader header; header.setNumberOfPlayers(2); header.setRandomSeed(seed); header.setAIOrderDelay(2);
    header.getBasePlayer(0)=BasePlayer(0,"Numbi",0,BasePlayer::playerTypeFromImplementationID(AI::NUMBI));
    header.getBasePlayer(1)=BasePlayer(1,"Castor",1,BasePlayer::playerTypeFromImplementationID(AI::CASTOR));
    game.setGameHeader(header,true); game.setWaitingOnMask(0);
    game.map.configureCompute(participants);
    REQUIRE(game.map.computeExecutor().threadCount()==participants);
    game.map.configureGradientPipeline(participants,8);
    for (int team=0;team<2;++team) {
        auto* owner=game.teams[team]; const int home=team ? 148 : 8;
        owner->enemies=1u<<(1-team); owner->startPosX=home; owner->startPosY=8; owner->startPosSet=Team::START_POS_FROM_UNIT;
        for (const char* kind : {"swarm","inn"}) {
            auto* b=world.addBuilding(kind,home,kind==std::string("swarm") ? 8 : 14,0,team);
            b->materials[WHEAT]=b->type->maxMaterial[WHEAT]; b->update();
        }
        for (int n=0;n<12;++n) world.addUnit(WORKER,home+n%4,20+n/4,team);
        for (int y=24;y<32;++y) for (int x=home;x<home+12;++x) game.map.setResourceByIndex(x,y,WHEAT,1);
    }
    const unsigned count=fixture==2 ? 4 : 100;
    for (unsigned n=0;n<count;++n) {
        const unsigned team=fixture==1 ? n%2 : n/(count/2);
        const int x=fixture==1 ? 16+(n%10)*10 : (team ? 140 : 8)+int(n%(count/2)%5)*12;
        const int y=fixture==1 ? 40+(n/10)*9 : 48+int(n%(count/2)/5)*12;
        auto* b=world.addBuilding("defencetower",x,y,0,team);
        REQUIRE(b); b->bullets=b->type->maxBullets; b->materials[STONE]=b->type->maxMaterial[STONE]; b->update();
        if (fixture==1) { auto* u=world.addUnit(WARRIOR,x+3,y,1-team); REQUIRE(u); }
    }
    for (int team=0;team<2;++team) {
        for (auto* b:game.teams[team]->liveBuildings.entries()) if (b) game.teams[team]->addToStaticAbilitiesLists(b);
        game.teams[team]->stats.step(game.teams[team]);
    }
    if (mode!=Original) game.configureTurretTargetingShadow(1,1,capacity,1,forecast,reusePreparation,profileCpu);
}
void advance(glob2test::HeadlessGame& fixture,Run* evidence)
{
    auto& game=fixture.game;
    const auto boundaryStart=profileOwner ? TurretTargeting::threadCpuNs() : 0;
    const auto world=game.captureReadBoundary(seats,false);
    if (profileOwner && selection) selection->boundaryCpu+=TurretTargeting::threadCpuNs()-boundaryStart;
    for (auto [seat,order]:game.prepareAIOrders(seats,false,{},&world)) {
        order=game.validateAIOrder(order,seat); order->sender=seat;
        if (evidence) {
            std::string wire(1,char(order->getOrderType()));
            if (order->getDataLength()) wire.append(reinterpret_cast<const char*>(order->getData()),order->getDataLength());
            evidence->orders.push_back(wire);
        }
        const auto accepted=game.executeOrder(order,seat); game.settleAIOrder(order,accepted);
    }
    if (selection) selection->networkDigest^=game.checkSum(nullptr,nullptr,nullptr,false);
    const auto syncStart=profileOwner ? TurretTargeting::threadCpuNs() : 0;
    const auto syncWallStart=profileOwner ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    game.syncStep(0,Game::PreparationCompletion::Deferred);
    if (profileOwner && selection) {
        selection->syncCpu+=TurretTargeting::threadCpuNs()-syncStart;
        selection->syncWall+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-syncWallStart).count();
    }
    fixture.gui.consumeClientEvents();
    const auto completedStart=profileOwner ? TurretTargeting::threadCpuNs() : 0;
    game.captureReadBoundary(seats,false); // Engine completed boundary, before pacing/next orders.
    if (profileOwner && selection) selection->boundaryCpu+=TurretTargeting::threadCpuNs()-completedStart;
    if (evidence) evidence->checksums.push_back(game.checkSum(nullptr,nullptr,nullptr,true));
}
Run run(unsigned fixture,unsigned seed,unsigned participants,Mode mode,bool correctness,unsigned ticks=256)
{
    setSyncRandSeed(seed);
    glob2test::HeadlessGame world({.wDec=8,.hDec=8,.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true,.seed=seed});
    setup(world,fixture,seed,participants,mode);
    Run result; result.targeting.mode=mode; result.targeting.diagnostics=correctness; selection=&result.targeting;
    struct Reset { ~Reset(){selection=nullptr;} } reset;
    TurretTargeting::ScopedAttemptObserver observer(opportunities ? followWindow : nullptr,nullptr);
    if (callgrindRegion) { CALLGRIND_START_INSTRUMENTATION; CALLGRIND_ZERO_STATS; }
    const auto start=std::chrono::steady_clock::now(); const auto cpu=std::clock();
    const auto ownerStart=profileOwner ? TurretTargeting::threadCpuNs() : 0;
    const auto firstBoundary=profileOwner ? TurretTargeting::threadCpuNs() : 0;
    world.game.captureReadBoundary(seats,false);
    if (profileOwner) result.targeting.boundaryCpu+=TurretTargeting::threadCpuNs()-firstBoundary;
    for (unsigned t=0;t<ticks;++t) advance(world,correctness ? &result : nullptr);
    world.game.drainAI(); // Include all admitted computation, never publish early.
    world.game.map.finishGradientPipeline(); world.game.map.finishResourceGrowth();
    if (profileOwner) result.ownerCpu=TurretTargeting::threadCpuNs()-ownerStart;
    result.cpu=Uint64(double(std::clock()-cpu)*1e9/CLOCKS_PER_SEC);
    result.wall=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    if (callgrindRegion) {
        CALLGRIND_DUMP_STATS_AT("turret-producer-whole-ticks");
        CALLGRIND_STOP_INSTRUMENTATION;
    }
    for (int team=0;team<2;++team) {
        result.shots+=world.game.teams[team]->stats.measurements.shots[GameplayMeasurements::TOWER];
        for (const auto* u:world.game.teams[team]->liveUnits.entries()) if (u && !u->isDead) ++result.units; result.turrets+=world.game.teams[team]->turrets.size();
    }
    result.captures=world.game.snapshots().metrics.captures;
    if (const auto* pipeline=world.game.turretShadow()) { result.pipeline=pipeline->statistics(); result.indexBytes=pipeline->statistics().indexBytes; result.listBytes=pipeline->statistics().shortlistBytes; }
    if (correctness) {
        auto* memory=new GAGCore::MemoryStreamBackend; GAGCore::BinaryOutputStream output(memory);
        world.game.save(&output,false,"turret engine comparison"); output.flush(); const auto bytes=memory->takeContents(); result.save.assign(bytes.begin(),bytes.end());
    }
    return result;
}

Run resume(unsigned fixture,unsigned participants,Mode mode,const std::vector<char>& bytes,unsigned ticks)
{
    glob2test::HeadlessGame world({.wDec=8,.hDec=8,.teams=2,.loadDefaultRace=true,.header=true});
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    REQUIRE(world.game.load(&input));
    world.game.setWaitingOnMask(0); world.game.map.configureCompute(participants);
    REQUIRE(world.game.map.computeExecutor().threadCount()==participants);
    world.game.map.configureGradientPipeline(participants,8);
    if (mode!=Original) world.game.configureTurretTargetingShadow(1,1,capacity,1,forecast,reusePreparation,profileCpu);
    Run result; result.targeting.mode=mode; result.targeting.diagnostics=true; selection=&result.targeting;
    struct Reset { ~Reset(){selection=nullptr;} } reset;
    for (unsigned t=0;t<ticks;++t) advance(world,&result);
    return result;
}
}
// Referenced only by the benchmark-specific TypeSteps object.
Building::TurretTarget turretEngineBenchmarkTarget(const Building& b)
{
    if (!selection) return b.findBestTarget();
    ++selection->attempts;
    auto* pipeline=b.owner->game->turretShadow();
    if (selection->mode==Cached) {
        const auto cpuStart=profileCpu ? TurretTargeting::threadCpuNs() : 0;
        const auto target=TurretTargeting::firstValid(b,pipeline->entries(b),selection->diagnostics ? &selection->rejections : nullptr);
        if (profileCpu) selection->consumptionCpu+=TurretTargeting::threadCpuNs()-cpuStart;
        selection->found+=target.found();
        Building::TurretTarget result;
        if (target.found()) { result.type=target.entry.kind==TurretTargeting::Kind::Building ? Building::TARGETTYPE_BUILDING : Building::TARGETTYPE_GENERAL;
            result.x=target.x; result.y=target.y; result.ticks=target.ticks; }
        return result;
    }
    const auto cpuStart=profileCpu ? TurretTargeting::threadCpuNs() : 0;
    const auto target=b.findBestTarget();
    if (profileCpu) selection->scanCpu+=TurretTargeting::threadCpuNs()-cpuStart;
    selection->found+=target.found();
    if (selection->mode==Shadow) {
        const auto candidate=TurretTargeting::firstValid(b,pipeline->entries(b),selection->diagnostics ? &selection->rejections : nullptr);
        selection->shadowValid+=candidate.found();
        if (opportunities && target.found() && b.computeFiringSolution(target.x,target.y).ticksLeft<target.ticks) {
            const auto reference=TurretTargeting::plan(b,0,1).preferred;
            ++selection->opportunities;
            selection->agreement+=candidate.found() && candidate.entry==reference.entry && candidate.x==reference.x && candidate.y==reference.y;
            const auto key=std::pair{b.gid,b.scriptIdentity};
            if (selection->windows.contains(key)) ++selection->missed;
            if (candidate.found()) { selection->delays.push_back(0); selection->windows.erase(key); }
            else selection->windows.insert_or_assign(key,Selection::Window{reference.entry,b.owner->game->stepCounter});
        }
    }
    return target;
}
TEST_SUITE("TurretEngine")
{
    TEST_CASE("turret producer callgrind regions [benchmark][callgrind]")
    {
        configure();
        glob2test::HeadlessGlobals globals;
        const unsigned fixture=std::getenv("TURRET_PROFILE_CASE") ? std::stoul(std::getenv("TURRET_PROFILE_CASE")) : 1;
        const unsigned participants=std::getenv("TURRET_PARTICIPANTS") ? std::stoul(std::getenv("TURRET_PARTICIPANTS")) : 1;
        REQUIRE(fixture<3);
        const auto mode=std::getenv("TURRET_PROFILE_ORIGINAL") ? Original : Cached;
        (void)run(fixture,713,participants,mode,false);
        callgrindRegion=true;
        const auto result=run(fixture,713,participants,mode,false);
        callgrindRegion=false;
        std::printf("turret_callgrind,%u,%u,%u,%s,%llu,%llu\n",fixture,participants,capacity,modeName(mode),
            (unsigned long long)result.shots,(unsigned long long)result.pipeline.sources);
    }
    TEST_CASE("reference trajectory measures cached opportunities [artifacts]")
    {
        configure(); opportunities=true;
        glob2test::HeadlessGlobals globals;
        for (unsigned fixture=0;fixture<3;++fixture) for (unsigned seed : {713,1919,7919,104729,15485863,32452843,49979687,67867967,86028121,123456}) {
            auto result=run(fixture,seed,4,Shadow,false);
            auto& target=result.targeting; std::sort(target.delays.begin(),target.delays.end());
            const unsigned p95=target.delays.empty() ? 0 : target.delays[(target.delays.size()*95+99)/100-1];
            std::printf("turret_opportunity,%u,%u,%u,%u,%u,%zu,%u\n",fixture,seed,target.opportunities,target.agreement,target.missed,target.windows.size(),p95);
            CHECK(target.missed==0);
            CHECK(p95<=4);
        }
    }
    TEST_CASE("whole headless ticks compare original shadow and cached firing [benchmark]")
    {
        configure();
        glob2test::HeadlessGlobals globals;
        constexpr std::array<unsigned,10> seeds{713,1919,7919,104729,15485863,32452843,49979687,67867967,86028121,123456};
        std::printf("variant,%s,capacity,%u,profile,%u\n",variant,capacity,profileCpu);
        std::printf("turret_engine,fixture,participants,repeat,seed,mode,ticks,wall_ns,cpu_ns,attempts,found,shots,units,turrets,captures,index_bytes,list_bytes\n");
        const unsigned requestedParticipants=std::getenv("TURRET_PARTICIPANTS") ? std::stoul(std::getenv("TURRET_PARTICIPANTS")) : 0;
        const auto* requestedRepeat=std::getenv("TURRET_REPEAT");
        for (unsigned participants : {1,2,4,8}) for (unsigned fixture=0;fixture<3;++fixture) {
            if (requestedParticipants && participants!=requestedParticipants) continue;
            for (unsigned mode=0;mode<3;++mode) (void)run(fixture,713,participants,Mode(mode),false);
            for (unsigned repeat=0;repeat<10;++repeat) for (unsigned ordinal=0;ordinal<3;++ordinal) {
                if (requestedRepeat && repeat!=std::stoul(requestedRepeat)) continue;
                const auto mode=Mode((ordinal+repeat)%3); const auto r=run(fixture,seeds[repeat],participants,mode,false);
                std::printf("turret_engine,%s,%u,%u,%u,%s,256,%llu,%llu,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu\n",
                    fixture==0 ? "empty100" : fixture==1 ? "combat100" : "ordinary4",participants,repeat,seeds[repeat],modeName(mode),
                    (unsigned long long)r.wall,(unsigned long long)r.cpu,r.targeting.attempts,r.targeting.found,
                    (unsigned long long)r.shots,(unsigned long long)r.units,(unsigned long long)r.turrets,(unsigned long long)r.captures,
                    (unsigned long long)r.indexBytes,(unsigned long long)r.listBytes);
                if (profileCpu) std::printf("turret_cpu,%s,%u,%u,%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                    fixture==0 ? "empty100" : fixture==1 ? "combat100" : "ordinary4",participants,repeat,modeName(mode),
                    (unsigned long long)r.targeting.scanCpu,(unsigned long long)r.targeting.boundaryCpu-r.pipeline.admissionCpu,
                    (unsigned long long)r.pipeline.sourceCpu,(unsigned long long)r.pipeline.indexCpu,
                    (unsigned long long)r.pipeline.planningCpu,(unsigned long long)r.pipeline.publicationCpu,
                    (unsigned long long)r.targeting.consumptionCpu,(unsigned long long)r.pipeline.sources,(unsigned long long)r.pipeline.skipped,
                    (unsigned long long)(r.pipeline.admissionCpu-r.pipeline.sourceCpu-r.pipeline.indexCpu),
                    (unsigned long long)r.pipeline.waitNs);
                if (profileOwner) std::printf("turret_owner,%s,%u,%u,%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                    fixture==0 ? "empty100" : fixture==1 ? "combat100" : "ordinary4",participants,repeat,modeName(mode),
                    (unsigned long long)r.ownerCpu,(unsigned long long)r.targeting.syncCpu,(unsigned long long)r.targeting.syncWall,
                    (unsigned long long)r.targeting.boundaryCpu,(unsigned long long)r.pipeline.admissionCpu,
                    (unsigned long long)r.pipeline.joinCpu,(unsigned long long)r.pipeline.ownerPlanningCpu,
                    (unsigned long long)(r.pipeline.planningCpu-r.pipeline.ownerPlanningCpu));
                if (profileCpu) std::printf("turret_index,%s,%u,%u,%s,%llu,%llu\n",
                    fixture==0 ? "empty100" : fixture==1 ? "combat100" : "ordinary4",participants,repeat,modeName(mode),
                    (unsigned long long)r.pipeline.backgroundIndexCpu,(unsigned long long)r.pipeline.ownerBackgroundIndexCpu);
            }
        }
    }
    TEST_CASE("whole engine cached traces agree across participants and shadow preserves reference [artifacts]")
    {
        configure();
        glob2test::HeadlessGlobals globals;
        for (unsigned fixture=0;fixture<3;++fixture) {
            const auto reference=run(fixture,713,1,Original,true);
            const auto cached=run(fixture,713,1,Cached,true);
            if (fixture!=1) CHECK(cached.checksums==reference.checksums);
            if (fixture==1) REQUIRE(reference.targeting.found*10>=reference.targeting.attempts);
            for (unsigned participants : {1,2,4,8}) {
                const auto shadow=run(fixture,713,participants,Shadow,true);
                CHECK(shadow.checksums==reference.checksums); CHECK(shadow.orders==reference.orders); CHECK(shadow.save==reference.save);
                const auto parallel=run(fixture,713,participants,Cached,true);
                CHECK(parallel.checksums==cached.checksums); CHECK(parallel.orders==cached.orders); CHECK(parallel.save==cached.save);
            }
            std::ofstream trace(glob2test::artifactDir()/("fixture-"+std::to_string(fixture)+"-checksums.csv"));
            trace << "tick,reference,cached\n";
            for (unsigned t=0;t<cached.checksums.size();++t) trace << t+1 << ',' << reference.checksums[t] << ',' << cached.checksums[t] << '\n';
            for (const auto& [name,bytes]:std::array<std::pair<const char*,const std::vector<char>*>,2>{{{"original",&reference.save},{"cached",&cached.save}}}) {
                std::ofstream saved(glob2test::artifactDir()/("fixture-"+std::to_string(fixture)+"-"+name+".game"),std::ios::binary);
                saved.write(bytes->data(),bytes->size());
            }
        }
    }
    TEST_CASE("whole engine save continuation distinguishes persistent simulation from research cache [artifacts]")
    {
        configure();
        glob2test::HeadlessGlobals globals;
        std::ofstream report(glob2test::artifactDir()/"continuation.csv");
        report << "mode,cut,remaining,checksums_equal\n";
        for (Mode mode : {Original,Shadow,Cached}) {
            const auto continuous=run(1,713,2,mode,true,64);
            for (unsigned cut : {1,2,4,8,16,38,39,40,41}) {
                const auto prefix=run(1,713,2,mode,true,cut);
                const auto continued=resume(1,2,mode,prefix.save,64-cut);
                const std::vector<Uint32> expected(continuous.checksums.begin()+cut,continuous.checksums.end());
                const bool equal=continued.checksums==expected;
                report << modeName(mode) << ',' << cut << ',' << 64-cut << ',' << equal << '\n';
                if (mode!=Cached) CHECK(equal);
                // Cached mismatch is a live-readiness gate: research lists are
                // deliberately not serialized, and cannot claim continuation.
            }
        }
    }

}
