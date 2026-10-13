// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "HiringPlanner.h"
#include "HiringDiagnostics.h"
#include "HiringStudy.h"
#include <sstream>
#include "SharedBuildingGradient.h"
#include <thread>
#include <set>
#include <nlohmann/json.hpp>

TEST_SUITE("HiringPlanner")
{
TEST_CASE("snapshot planning retains busy workers and never changes the game")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true});
    auto* inn=world.addBuilding("inn",8,8);
    REQUIRE(inn);inn->desiredMaxUnitWorking=2;
    auto* free=world.addUnit(WORKER,12,12);
    auto* busy=world.addUnit(WORKER,13,12);
    REQUIRE(free);REQUIRE(busy);
    for(auto* u:{free,busy}){u->carriedMaterial=WHEAT;u->medical=Unit::MED_FREE;}
    free->activity=Unit::ACT_RANDOM;busy->activity=Unit::ACT_FILLING;
    for(int swim=0;swim<SWIM_CLASS_COUNT;++swim)world.game.map.buildingGradient(inn,swim);
    const auto checksum=world.checksum();
    const auto snapshot=world.game.captureReadBoundary({},true,
        SimulationSnapshot::Simulation & ~SimulationSnapshot::bit(SimulationSnapshot::Component::Growth));
    auto inputs=HiringPlanner::capture(world.game,snapshot);
    HiringPlanner::Cache cache;
    auto result=cache.rank(inputs,0,8);
    CHECK(world.checksum()==checksum);
    CHECK(cache.bytes()<=HiringPlanner::Cache::MaxBytes);
    bool found=false;
    for(const auto& b:result.buildings)if(b.building==Game::refOf(inn))
        for(const auto& job:b.jobs)if(job.key==Hiring::Key{Hiring::Job::Delivery,WHEAT}) {
            found=true;CHECK(job.combined.size()==2);CHECK(job.available.size()==1);CHECK(job.busy.size()==1);
            CHECK(job.available.front().unit==Game::refOf(free));
            CHECK(job.busy.front().unit==Game::refOf(busy));
        }
    CHECK(found);
    const auto repeated=cache.rank(inputs,0,8);
    CHECK(repeated.candidateVisits==result.candidateVisits);
    CHECK(result.metrics.sharedQueries>0);
    CHECK(repeated.metrics.fieldMisses==0);
    CHECK(repeated.metrics.sharedQueries>0);
    CHECK(repeated.metrics.geographyChanges==0);
    CHECK(repeated.metrics.rankedCandidates==result.metrics.rankedCandidates);
    CHECK(world.checksum()==checksum);
    CHECK_THROWS_AS(cache.rank(inputs,0,0),std::invalid_argument);
    CHECK_THROWS_AS(cache.rank(inputs,0,129),std::invalid_argument);
}
TEST_CASE("planning skips zero-demand and staffed buildings and resumes on vacancies")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true});
    auto* inn=world.addBuilding("inn",8,8);auto* worker=world.addUnit(WORKER,12,12);
    REQUIRE(inn);REQUIRE(worker);worker->carriedMaterial=WHEAT;
    world.game.map.buildingGradient(inn,worker->swimClass());
    HiringPlanner::Cache cache;
    const auto plan=[&] {
        world.game.snapshotStore().invalidateBoundary(); // Announce these owner-side fixture edits.
        auto snapshot=world.game.captureReadBoundary({},true,SimulationSnapshot::Simulation);
        auto inputs=HiringPlanner::capture(world.game,snapshot);
        auto output=cache.rank(inputs,0,8);
        return std::pair{std::move(inputs),std::move(output)};
    };
    inn->desiredMaxUnitWorking=0;
    auto [zeroInputs,zero]=plan();
    CHECK(zeroInputs.routes.empty());CHECK(zeroInputs.supplies.empty());CHECK(zeroInputs.attack.empty());
    CHECK(zero.buildings.empty());CHECK(zero.candidateVisits==0);CHECK(zero.metrics.buildingsSkippedNoDemand==1);
    inn->desiredMaxUnitWorking=1;inn->unitsWorking.push_back(worker);
    auto [staffedInputs,staffed]=plan();
    CHECK(staffedInputs.routes.empty());CHECK(staffed.buildings.empty());CHECK(staffed.candidateVisits==0);
    inn->unitsWorking.clear();
    auto [vacancyInputs,vacancy]=plan();
    REQUIRE(vacancy.buildings.size()==1);CHECK(vacancy.candidateVisits>0);
    CHECK_FALSE(vacancyInputs.routes.empty());CHECK(vacancyInputs.attack.empty());
    CHECK(vacancy.metrics.buildingsNeedingHiring==1);
    inn->desiredMaxUnitWorking=0;
    auto [finishedInputs,finished]=plan();
    CHECK(finished.buildings.empty());CHECK(finished.candidateVisits==0);
}
TEST_CASE("job quotas skip supplied materials and distance reuse keeps exact rankings")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true});
    auto* inn=world.addBuilding("inn",8,8,1);REQUIRE(inn);inn->desiredMaxUnitWorking=2;
    for(int i=0;i<16;++i) {
        auto* u=world.addUnit(WORKER,12+i%4,12+i/4,0,1);REQUIRE(u);
        u->activity=i%2?Unit::ACT_FILLING:Unit::ACT_RANDOM;u->medical=Unit::MED_FREE;
        if(i%3==0)u->carriedMaterial=WHEAT;
    }
    world.game.map.incResourceByIndex(20,20,WHEAT,0);
    for(int swim=0;swim<SWIM_CLASS_COUNT;++swim){world.game.map.buildingGradient(inn,swim);world.game.map.getMaterialGradientSlot(0,WHEAT,swim,false,inn);}
    auto snapshot=world.game.captureReadBoundary({},true,SimulationSnapshot::Simulation);
    auto inputs=HiringPlanner::capture(world.game,snapshot);
    HiringPlanner::Cache cache;
    const auto uncached=cache.rank(inputs,0,128,HiringPlanner::Pools::All,{.reuseDistances=false,.pruneFetch=false});
    const auto cached=cache.rank(inputs,0,128);
    CHECK(cached.metrics.distanceReuses>0);CHECK(cached.metrics.sharedQueries<uncached.metrics.sharedQueries);
    REQUIRE(cached.buildings.size()==uncached.buildings.size());
    const auto same=[](const auto& a,const auto& b){return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y){return x.unit==y.unit&&x.level==y.level&&x.value==y.value&&x.available==y.available;});};
    for(unsigned j=0;j<cached.buildings[0].jobs.size();++j) {
        const auto& a=cached.buildings[0].jobs[j];const auto& b=uncached.buildings[0].jobs[j];
        CHECK(same(a.combined,b.combined));CHECK(same(a.available,b.available));CHECK(same(a.busy,b.busy));
    }
    for(int extras:{0,2,4}) {
        const auto small=cache.rank(inputs,0,128,HiringPlanner::Pools::Split,{.extras=extras});
        for(const auto& list:small.buildings[0].jobs)CHECK(list.available.size()+list.busy.size()<=2+unsigned(extras));
        const auto free=cache.rank(inputs,0,128,HiringPlanner::Pools::Split,{.extras=extras,.availableOnly=true});
        CHECK(free.metrics.busyEvaluations==0);CHECK(free.candidateVisits<small.candidateVisits);
        for(const auto& list:free.buildings[0].jobs)for(const auto& c:list.combined)CHECK(c.available);
    }
    for(auto pools:{HiringPlanner::Pools::All,HiringPlanner::Pools::Combined,HiringPlanner::Pools::Split})for(int extras:{0,2,4}) {
        const auto exact=cache.rank(inputs,0,128,pools,{.extras=extras,.pruneFetch=false});
        const auto pruned=cache.rank(inputs,0,128,pools,{.extras=extras,.pruneFetch=true});
        for(unsigned j=0;j<exact.buildings[0].jobs.size();++j) {
            const auto& a=exact.buildings[0].jobs[j];const auto& b=pruned.buildings[0].jobs[j];
            CHECK(same(a.combined,b.combined));CHECK(same(a.available,b.available));CHECK(same(a.busy,b.busy));
        }
    }
    inn->materials[WHEAT]=inn->type->maxMaterial[WHEAT];
    world.game.snapshotStore().invalidateBoundary();snapshot=world.game.captureReadBoundary({},true,SimulationSnapshot::Simulation);
    const auto full=cache.rank(HiringPlanner::capture(world.game,snapshot),0,128);
    for(const auto& list:full.buildings[0].jobs)CHECK(list.key.material!=WHEAT);
    CHECK(full.metrics.jobsSkippedNoQuota>=2);
}
TEST_CASE("diverse material offers cover peers without reserving live workers")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true});
    auto* a=world.addBuilding("inn",8,8);auto* b=world.addBuilding("inn",16,8);
    REQUIRE(a);REQUIRE(b);a->desiredMaxUnitWorking=b->desiredMaxUnitWorking=2;
    for(int i=0;i<16;++i){auto* u=world.addUnit(WORKER,12+i%4,12+i/4);REQUIRE(u);u->medical=Unit::MED_FREE;u->activity=Unit::ACT_RANDOM;}
    world.game.map.incResourceByIndex(24,24,WHEAT,0);
    for(auto* building:{a,b})for(int swim=0;swim<SWIM_CLASS_COUNT;++swim)world.game.map.buildingGradient(building,swim);
    for(int swim=0;swim<SWIM_CLASS_COUNT;++swim)world.game.map.getMaterialGradientSlot(0,WHEAT,swim);
    auto snapshot=world.game.captureReadBoundary({},true,SimulationSnapshot::Simulation);
    auto inputs=HiringPlanner::capture(world.game,snapshot);const auto checksum=world.checksum();
    HiringPlanner::Cache cache;
    auto output=cache.rank(inputs,0,128,HiringPlanner::Pools::Split,{.extras=2,.availableOnly=true,.coordinate=true});
    CHECK(output.metrics.coordinatedTeams==1);CHECK(world.checksum()==checksum);
    std::set<std::pair<Uint16,Uint32>> primary;
    for(const auto& building:output.buildings)for(const auto& list:building.jobs)if(list.key==Hiring::Key{Hiring::Job::Fetch,WHEAT}) {
        REQUIRE(list.combined.size()>=2);CHECK(list.combined.size()<=4);
        for(unsigned i=0;i<2;++i)CHECK(primary.insert({list.combined[i].unit.gid,list.combined[i].unit.generation}).second);
    }
    CHECK(primary.size()==4);
    // A narrow ceiling cannot hold the private team ranking buffer. Falling
    // back must retain the same independent vacancy-sized prefixes.
    const auto fallback=cache.rank(inputs,0,4,HiringPlanner::Pools::Split,{.extras=2,.availableOnly=true,.coordinate=true});
    const auto independent=cache.rank(inputs,0,4,HiringPlanner::Pools::Split,{.extras=2,.availableOnly=true});
    CHECK(fallback.metrics.coordinationSkipped==1);
    for(unsigned bi=0;bi<fallback.buildings.size();++bi)for(unsigned ji=0;ji<fallback.buildings[bi].jobs.size();++ji) {
        const auto& actual=fallback.buildings[bi].jobs[ji].combined;
        const auto& expected=independent.buildings[bi].jobs[ji].combined;
        REQUIRE(actual.size()==expected.size());
        for(unsigned n=0;n<actual.size();++n)CHECK(actual[n].unit==expected[n].unit);
    }
    CHECK_THROWS_AS(cache.rank(inputs,0,128,HiringPlanner::Pools::Split,{.extras=2,.coordinate=true}),std::invalid_argument);
}
TEST_CASE("shared field survives replacement and proposals preserve owner demand")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true});
    auto* inn=world.addBuilding("inn",8,8);REQUIRE(inn);
    int distance=0;REQUIRE(world.game.map.buildingAvailable(inn,0,12,12,&distance));
    auto lease=inn->leaseGradient(inn->routeSlot(0,BuildingRoute::Automatic));REQUIRE(lease);
    REQUIRE(lease->search);
    const int required=lease->search->requiredCost();
    std::uint64_t wait=0,query=0,expansions=0;
    const auto oldDistance=lease->distance(20,20,wait,query,expansions);
    CHECK(oldDistance!=INT_MAX);CHECK(lease->search->requiredCost()==required);
    CHECK(expansions>0);
    std::thread worker([&]{for(int i=0;i<1000;++i)lease->distance(24+i%4,24,wait,query,expansions);});
    for(int i=0;i<1000;++i)CHECK(world.game.map.buildingAvailable(inn,0,12+i%4,12,&distance));
    inn->resetPathfindGradients();
    REQUIRE(world.game.map.buildingAvailable(inn,0,20,20,&distance));
    worker.join();
    CHECK(distance==oldDistance);
    CHECK(lease->values!=inn->globalGradient[0]);
    CHECK(lease->distance(20,20,wait,query,expansions)==oldDistance);
}
TEST_CASE("missing building fields never trigger private construction")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true});
    auto* inn=world.addBuilding("inn",8,8);REQUIRE(inn);inn->desiredMaxUnitWorking=1;
    auto* unit=world.addUnit(WORKER,12,12);REQUIRE(unit);unit->carriedMaterial=WHEAT;
    auto snapshot=world.game.captureReadBoundary({},true,SimulationSnapshot::Simulation);
    HiringPlanner::Cache cache;
    const auto output=cache.rank(HiringPlanner::capture(world.game,snapshot),0,8);
    CHECK(output.metrics.sharedQueries==0);CHECK(output.metrics.missingBuildingFields>0);
    CHECK(output.metrics.fieldMisses==0);CHECK(inn->globalGradient[0]==nullptr);
}
TEST_CASE("ranking tie break follows unit identity")
{
    HiringPlanner::Candidate a{{7,3},10,20,true},b{{8,1},10,20,false};
    CHECK(HiringPlanner::better(a,b,Hiring::Job::Fetch));
    CHECK_FALSE(HiringPlanner::better(b,a,Hiring::Job::Fetch));
}
TEST_CASE("bounded shortlists match fully sorted prefixes for every job and pool")
{
    for(unsigned size:{0u,1u,7u,33u,257u})for(unsigned capacity:{1u,8u,16u,32u,64u,128u})
        for(auto job:{Hiring::Job::Delivery,Hiring::Job::Fetch,Hiring::Job::Clear,Hiring::Job::Explore,Hiring::Job::Defend}) {
            std::vector<HiringPlanner::Candidate> candidates;
            for(unsigned i=0;i<size;++i)candidates.push_back({{Uint16(size-i),Uint32(i%3+1)},int(i%7)-3,int(i%13)-6,i%4!=0});
            auto sorted=candidates;
            std::sort(sorted.begin(),sorted.end(),[&](const auto& a,const auto& b){return HiringPlanner::better(a,b,job);});
            for(auto policy:{HiringPlanner::Pools::All,HiringPlanner::Pools::Combined,HiringPlanner::Pools::Split}) {
                const auto actual=HiringPlanner::shortlist({job,-1},candidates,capacity,policy);
                std::vector<HiringPlanner::Candidate> combined,available,busy;
                for(const auto& c:sorted) {
                    if(policy!=HiringPlanner::Pools::Split&&combined.size()<capacity)combined.push_back(c);
                    if(policy!=HiringPlanner::Pools::Combined) {
                        auto& pool=c.available?available:busy;
                        if(pool.size()<(c.available?capacity-HiringPlanner::busyCapacity(capacity):HiringPlanner::busyCapacity(capacity)))pool.push_back(c);
                    }
                }
                const auto same=[](const auto& a,const auto& b){
                    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y){return x.unit==y.unit&&x.level==y.level&&x.value==y.value&&x.available==y.available;});
                };
                CHECK(same(actual.combined,combined));CHECK(same(actual.available,available));CHECK(same(actual.busy,busy));
            }
        }
}
TEST_CASE("fetch permission without suppliers reuses the existing natural plane")
{
    glob2test::HeadlessGlobals globals;
    glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true});
    auto* inn=world.addBuilding("inn",8,8);auto* worker=world.addUnit(WORKER,12,12);
    REQUIRE(inn);REQUIRE(worker);inn->desiredMaxUnitWorking=1;
    world.game.map.incResourceByIndex(16,16,WHEAT,0);
    const int swim=worker->swimClass();
    world.game.map.buildingGradient(inn,swim);
    world.game.map.getMaterialGradientSlot(0,WHEAT,swim,false,inn);
    const auto checksum=world.checksum();
    auto snapshot=world.game.captureReadBoundary({},true,SimulationSnapshot::Simulation);
    auto inputs=HiringPlanner::capture(world.game,snapshot);
    REQUIRE(inputs.supplies.size()==1);
    CHECK(inputs.supplies.front().modes[WHEAT]==0);
    CHECK((inn->runtime->fetchesDirectStockMask&(1u<<WHEAT))!=0);
    HiringPlanner::Cache cache;
    const auto output=cache.rank(inputs,0,8,HiringPlanner::Pools::Split);
    CHECK(output.metrics.publishedResourceReads>0);
    CHECK(output.metrics.privateResourceReads==0);CHECK(output.metrics.fieldMisses==0);
    CHECK(output.metrics.geographyNs==0);
    CHECK(world.checksum()==checksum);
}
TEST_CASE("private fetch fields include suppliers without natural material")
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;
    options.discovered=true;options.loadDefaultRace=true;options.header=true;
    options.experiments.set(ExperimentId::MarketsV2);
    glob2test::HeadlessGame world(options);
    auto* inn=world.addBuilding("inn",6,6,1);
    auto* market=world.addBuilding("market",24,24);
    auto* worker=world.addUnit(WORKER,20,20,0,1);
    REQUIRE(inn);REQUIRE(market);REQUIRE(worker);inn->desiredMaxUnitWorking=1;
    market->materials[CHERRY]=10;
    for(int swim=0;swim<SWIM_CLASS_COUNT;++swim)world.game.map.buildingGradient(inn,swim);
    worker->activity=Unit::ACT_RANDOM;worker->medical=Unit::MED_FREE;
    auto snapshot=world.game.captureReadBoundary({},true,
        SimulationSnapshot::Simulation & ~SimulationSnapshot::bit(SimulationSnapshot::Component::Growth));
    auto inputs=HiringPlanner::capture(world.game,snapshot);
    const auto supply=std::find_if(inputs.supplies.begin(),inputs.supplies.end(),[&](const auto& s){return s.building==Game::refOf(inn);});
    REQUIRE(supply!=inputs.supplies.end());
    CHECK(supply->modes[CHERRY]==world.game.map.materialSupplyModesSlot(inn,CHERRY));
    CHECK(supply->modes[CHERRY]!=0);
    HiringPlanner::Cache cache;
    const auto output=cache.rank(inputs,0,8);
    bool found=false;
    for(const auto& building:output.buildings)if(building.building==Game::refOf(inn))
        for(const auto& list:building.jobs)if(list.key==Hiring::Key{Hiring::Job::Fetch,CHERRY}) {
            found=true;REQUIRE(list.combined.size()==1);
            CHECK(list.combined[0].unit==Game::refOf(worker));
        }
    CHECK(found);
    // Unified market fields are shared only when the consumer does not
    // exclude itself. The catalog folds redundant direct suppliers into mode 1.
    REQUIRE(supply->modes[CHERRY]==1);
    CHECK_FALSE(supply->consumerSpecific[CHERRY]);
    world.game.map.getMaterialGradientSlot(0,CHERRY,worker->swimClass(),false,inn);
    auto published=world.game.captureReadBoundary({},true,SimulationSnapshot::Simulation);
    const auto shared=cache.rank(HiringPlanner::capture(world.game,published),0,8);
    CHECK(shared.metrics.publishedResourceReads>0);
    CHECK(shared.metrics.privateResourceReads==0);
    CHECK(shared.metrics.fieldMisses==0);
}
}

TEST_SUITE("HiringStudy")
{
TEST_CASE("scheduled pool and owner publish the same immutable outputs at fixed deadlines")
{
    glob2test::HeadlessGlobals globals;
    const auto run=[](bool parallel,unsigned participants,unsigned delay) {
        HiringStudy::configure(false);
        glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true,.header=true});
        REQUIRE(world.addBuilding("inn",8,8));
        auto* worker=world.addUnit(WORKER,12,12);REQUIRE(worker);
        worker->carriedMaterial=WHEAT;
        world.game.map.configureGradientPipeline(2,8);
        world.game.map.configureCompute(participants);
        HiringStudy::configureScheduled({.delay=delay,.refresh=1,.capacity=8,.split=true,.parallel=parallel});
        world.game.captureReadBoundary({},false);
        const auto initial=world.checksum();
        world.game.captureReadBoundary({},true); // Pausing neither admits nor publishes.
        CHECK(world.checksum()==initial);
        world.step(12);
        std::ostringstream out;HiringStudy::write(out);auto report=nlohmann::json::parse(out.str());
        CHECK(report["max_pending_batches"].get<unsigned>()<=delay+1);
        CHECK(report["submitted"].get<unsigned>()==13);
        CHECK(report["published"].get<unsigned>()==(delay?12-delay:13));
        if(parallel && participants>1)CHECK(report["worker_jobs"].get<unsigned>()>0);
        else CHECK(report["worker_jobs"].get<unsigned>()==0);
        HiringStudy::clear(world.game);HiringStudy::configure(false);
        return report;
    };
    for(unsigned delay:{0u,2u,8u}) {
        const auto owner=run(false,1,delay),fallback=run(true,1,delay),parallel=run(true,4,delay);
        CHECK(owner["output_digest"]==fallback["output_digest"]);
        CHECK(owner["output_digest"]==parallel["output_digest"]);
        CHECK(owner["published"]==parallel["published"]);
    }
}
TEST_CASE("a vacancy after a skipped refresh publishes on the next fixed deadline")
{
    glob2test::HeadlessGlobals globals;
    for(bool parallel:{false,true}) {
        HiringStudy::configure(false);
        glob2test::HeadlessGame world({.discovered=true,.loadDefaultRace=true,.header=true});
        auto* inn=world.addBuilding("inn",8,8);auto* worker=world.addUnit(WORKER,12,12);
        REQUIRE(inn);REQUIRE(worker);worker->carriedMaterial=WHEAT;
        inn->desiredMaxUnitWorking=1;inn->unitsWorking.push_back(worker);
        world.game.map.buildingGradient(inn,worker->swimClass());
        world.game.map.configureCompute(4);
        HiringStudy::configureScheduled({.delay=4,.refresh=4,.capacity=8,.parallel=parallel});
        const auto boundary=[&](unsigned tick) {
            world.game.stepCounter=tick;
            world.game.captureReadBoundary({},false);
            HiringStudy::advance(world.game);
        };
        const auto metric=[&](const char* key) {
            std::ostringstream out;HiringStudy::write(out,false);
            return nlohmann::json::parse(out.str())["planner_metrics"].value(key,0ull);
        };
        boundary(0);inn->unitsWorking.clear();boundary(1);
        boundary(4);CHECK(metric("buildingsNeedingHiring")==0);
        boundary(7);CHECK(metric("buildingsNeedingHiring")==0);
        boundary(8);CHECK(metric("buildingsNeedingHiring")==1);
        CHECK(metric("rankedCandidates")>0);
        // Vacancy at boundary 1 reaches publication at 8: seven ticks.
        HiringStudy::clear(world.game);HiringStudy::configure(false);
    }
}
TEST_CASE("worker failure aborts publication and teardown still drains [slow]")
{
    glob2test::HeadlessGlobals globals;
    glob2test::GameOptions options;options.wDec=10;options.hDec=10;options.teams=16;
    options.discovered=true;options.loadDefaultRace=true;options.header=true;
    glob2test::HeadlessGame world(options);
    auto* inn=world.addBuilding("inn",8,8);REQUIRE(inn);inn->desiredMaxUnitWorking=2;
    auto* land=world.addUnit(WORKER,12,12);auto* swimmer=world.addUnit(WORKER,13,12);REQUIRE(land);REQUIRE(swimmer);
    land->performance[SWIM]=0;swimmer->performance[SWIM]=swimmer->performance[WALK];
    world.game.map.buildingGradient(inn,Map::swimClass(land->performance[WALK],land->performance[SWIM]));
    world.game.map.buildingGradient(inn,Map::swimClass(swimmer->performance[WALK],swimmer->performance[SWIM]));
    world.game.map.incResourceByIndex(16,16,WHEAT,0);
    world.game.map.configureGradientPipeline(2,8);world.game.map.configureCompute(2);
    // Sixteen teams receive 4 MiB each. Two swim-class resource fields plus
    // queue storage cannot fit that bound in one active job; this is a deterministic planner
    // failure, with other successful team outputs in the same generation.
    HiringStudy::configureScheduled({.delay=1,.refresh=1,.capacity=8});
    world.game.captureReadBoundary({},false);
    world.game.stepCounter=1;
    const auto before=world.checksum();
    CHECK_THROWS_AS(HiringStudy::advance(world.game),std::length_error);
    CHECK(world.checksum()==before);
    std::ostringstream state;HiringStudy::write(state,false);
    CHECK(nlohmann::json::parse(state.str())["published"].get<unsigned>()==0);
    CHECK_NOTHROW(HiringStudy::clear(world.game));
    HiringStudy::configure(false);
}
TEST_CASE("invalid scheduling cannot expand the eight tick contract")
{
    CHECK_THROWS_AS(HiringStudy::configureScheduled({.delay=8,.refresh=2}),std::invalid_argument);
    CHECK_THROWS_AS(HiringStudy::configureScheduled({.refresh=0}),std::invalid_argument);
    CHECK_THROWS_AS(HiringStudy::configureScheduled({.capacity=129}),std::invalid_argument);
    HiringStudy::configure(false);
}
}
