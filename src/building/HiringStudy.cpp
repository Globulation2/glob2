// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiringStudy.h"
#include "HiringPlanner.h"
#include "HiringDiagnostics.h"
#include "ComputeExecutor.h"
#include "sim/snapshot/BufferPool.h"
#include <optional>
#include <type_traits>
#include <cassert>
#include "Game.h"
#include "Team.h"
#include "Building.h"
#include "SharedBuildingGradient.h"
#include "Unit.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <map>
#include <set>
#include <ostream>
#include <iostream>

namespace HiringStudy
{
namespace
{
using Json=nlohmann::json;
using Output=HiringPlanner::Output;
std::uint64_t key(UnitRef unit){return (std::uint64_t(unit.generation)<<16)|unit.gid;}
struct Config
{
    unsigned delay,refresh,capacity;
    bool split;
    std::uint64_t opportunities=0,hits=0,startupOpportunities=0,startupHits=0,examined=0;
    std::uint64_t agreements=0,startupAgreements=0,referenceHits=0,referenceAgreements=0;
    std::uint64_t missing=0,busy=0,replaced=0,qualification=0,alreadyUsed=0;
    unsigned overlayTick=unsigned(-1);
    std::array<std::set<std::uint64_t>,Team::MAX_COUNT> used;
};
struct Work
{
    HiringPlanner::Inputs inputs;
    HiringPlanner::Cache* cache=nullptr;
    unsigned team=0,capacity=0,worker=0;
    bool split=true;
    HiringPlanner::Options options;
    std::size_t outputBound=0;
    std::shared_ptr<const Output> output;
    std::exception_ptr error;
    std::size_t cacheBytes=0;
    static void run(void* context,std::size_t) noexcept
    {
        auto& work=*static_cast<Work*>(context);
        work.worker=unsigned(ComputeExecutor::workerSlot());
        try {
            auto output=work.cache->rank(work.inputs,work.team,work.capacity,work.split?HiringPlanner::Pools::Split:HiringPlanner::Pools::Combined,work.options);
            work.output=std::make_shared<Output>(std::move(output));work.cacheBytes=work.cache->bytes();
        }
        catch(...) {work.error=std::current_exception();}
        work.inputs={}; // Snapshot lease ends with computation, not publication.
    }
};
struct Batch
{
    ComputeExecutor* executor=nullptr;
    ComputeExecutor::Batch ticket;
    unsigned tick=0,due=0;
    std::vector<std::unique_ptr<Work>> jobs;
    void join(){if(executor)executor->join(ticket);}
};
struct Session
{
    const Game* game;
    unsigned last=unsigned(-1),firstTick=0;
    HiringPlanner::Metrics plannerMetrics;
    const std::uint64_t ownerWaitStart=SharedBuildingGradient::ownerWaitNs.load();
    const std::uint64_t ownerContentionStart=SharedBuildingGradient::ownerContentions.load();
    std::uint64_t observations=0,planningNs=0,supplementNs=0,visits=0,peakBytes=0;
    std::array<std::unique_ptr<HiringPlanner::Cache>,Team::MAX_COUNT> caches;
    std::array<std::deque<std::shared_ptr<const Output>>,Team::MAX_COUNT> history;
    std::vector<Config> matrix;
    std::optional<Schedule> schedule;
    std::deque<std::unique_ptr<Batch>> pending;
    std::uint64_t submitted=0,publishedCount=0,waitNs=0,ownerJobs=0,workerJobs=0,maxPending=0;
    std::uint64_t outputDigest=1469598103934665603ull;
    std::size_t cacheReservation=0;
    void account()
    {
        if(!schedule)return;
        std::size_t bytes=cacheReservation+1024*1024+SharedBuildingGradient::peakRetiredBytes.load();
        for(const auto& outputs:history)for(const auto& output:outputs)bytes+=output->retainedBytes;
        for(const auto& batch:pending)for(const auto& job:batch->jobs)bytes+=job->outputBound;
        peakBytes=std::max<std::uint64_t>(peakBytes,bytes);
        if(bytes>160ull*1024*1024)throw std::length_error("Hiring scheduled output reservations exceed match memory bound");
    }
    void digest(std::uint64_t value){outputDigest^=value;outputDigest*=1099511628211ull;}
    void retain(std::shared_ptr<const Output> output)
    {
        planningNs+=output->ns;visits+=output->candidateVisits;plannerMetrics.add(output->metrics);
        digest(output->tick);digest(output->team);
        for(const auto& b:output->buildings){digest(b.building.gid);digest(b.building.generation);digest(b.configuration);
            for(const auto& list:b.jobs){digest(unsigned(list.key.job));digest(list.key.material);
                for(const auto* pool:{&list.combined,&list.available,&list.busy}){digest(pool->size());
                    for(const auto& c:*pool){digest(c.unit.gid);digest(c.unit.generation);digest(c.level);digest(c.value);digest(c.available);}}}}
        auto& previous=history[output->team];previous.push_back(std::move(output));
        if(schedule)while(previous.size()>1)previous.pop_front();
    }
    void joinDue(unsigned tick)
    {
        while(!pending.empty() && pending.front()->due<=tick)
        {
            auto& batch=*pending.front();const auto start=std::chrono::steady_clock::now();batch.join();
            waitNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            // Validate the entire generation before making any result visible.
            for(const auto& job:batch.jobs){if(job->error)std::rethrow_exception(job->error);if(!job->output)throw std::logic_error("Hiring batch lost its output");}
            for(auto& job:batch.jobs){ownerJobs+=job->worker==0;workerJobs+=job->worker!=0;retain(std::move(job->output));++publishedCount;}
            pending.pop_front();account();
        }
    }
    void drain()
    {
        std::exception_ptr error;
        for(auto& batch:pending){batch->join();for(const auto& job:batch->jobs)if(job->error&&!error)error=job->error;}
        if(error)std::rethrow_exception(error);
    }
    ~Session(){try{drain();}catch(...){/* Required barriers already surface errors on the owner. */}}
    explicit Session(const Game& game,std::optional<Schedule> selected):game(&game),firstTick(game.stepCounter),schedule(selected)
    {
        for(unsigned k:{0u,1u,2u,4u,8u})for(unsigned r:{1u,2u,4u,8u})for(unsigned l:{8u,16u,32u,64u,128u})for(bool split:{false,true})
            matrix.push_back({k,r,l,split});
        if(schedule){matrix.clear();matrix.push_back({schedule->delay,schedule->refresh,schedule->capacity,schedule->split});}
        const auto budget=std::min<std::size_t>(HiringPlanner::Cache::MaxBytes,64ull*1024*1024/game.teamsCount());
        cacheReservation=budget*game.teamsCount();
        for(int t=0;t<game.teamsCount();++t)caches[t]=std::make_unique<HiringPlanner::Cache>(budget);
    }
    const HiringPlanner::BuildingLists* published(unsigned team,const HiringDiagnostics::Opportunity& event,const Config& config) const
    {
        for(auto it=history[team].rbegin();it!=history[team].rend();++it)
        {
            const auto& output=**it;
            if(output.tick>event.tick || event.tick-output.tick<config.delay || output.tick%config.refresh!=team%config.refresh)continue;
            for(const auto& b:output.buildings) if(b.building==event.building && b.type==event.type && b.configuration==event.configuration) return &b;
            // The newest admissible publication replaces the whole team output.
            return nullptr;
        }
        return nullptr;
    }
    void opportunity(const HiringDiagnostics::Opportunity& event)
    {
        const unsigned team=Building::GIDtoTeam(event.building.gid);
        std::set<std::uint64_t> eligible;
        for(const auto& q:event.qualified)if(q.key==event.key)eligible.insert(key(q.unit));
        // Every oracle winner must have been reported by its qualification pass.
        if(!eligible.contains(key(event.winner)))throw std::logic_error("Hiring oracle omitted its winner");
        for(auto& config:matrix)
        {
            if(config.overlayTick!=event.tick){for(auto& used:config.used)used.clear();config.overlayTick=event.tick;}
            const auto* b=published(team,event,config);
            const HiringPlanner::List* list=nullptr;
            if(b)for(const auto& job:b->jobs)if(job.key==event.key){list=&job;break;}
            bool hit=false,agreement=false,referenceHit=false,referenceAgreement=false;
            const auto examine=[&](const std::vector<HiringPlanner::Candidate>& candidates,unsigned limit)
            {
                for(unsigned i=0;i<std::min<unsigned>(limit,candidates.size()) && !hit;++i)
                {
                    const auto& c=candidates[i];++config.examined;
                    // Independent prediction uses exactly the reference attempt's
                    // eligibility. The overlay below additionally excludes workers
                    // hypothetically selected earlier in this shadow pass.
                    if(!referenceHit && eligible.contains(key(c.unit))) {
                        const auto* live=game->resolveUnit(c.unit);
                        if(live && live->owner->teamNumber==int(team) && live->activity==Unit::ACT_RANDOM && live->medical==Unit::MED_FREE) {
                            referenceHit=true;referenceAgreement=c.unit==event.winner;
                        }
                    }
                    if(config.used[team].contains(key(c.unit))){++config.alreadyUsed;continue;}
                    const auto* live=game->resolveUnit(c.unit);
                    if(!live || live->owner->teamNumber!=int(team)){++config.replaced;continue;}
                    if(live->activity!=Unit::ACT_RANDOM || live->medical!=Unit::MED_FREE){++config.busy;continue;}
                    if(!eligible.contains(key(c.unit))){++config.qualification;continue;}
                    config.used[team].insert(key(c.unit));hit=true;agreement=c.unit==event.winner;
                }
            };
            if(list) {
                if(config.split && !(schedule && schedule->availableOnly)){examine(list->available,config.capacity-HiringPlanner::busyCapacity(config.capacity));examine(list->busy,HiringPlanner::busyCapacity(config.capacity));}
                else examine(list->combined,config.capacity);
            } else ++config.missing;
            // Startup is a separate policy cost: do not hide the expected
            // absence of a publication before K, or confuse it with steady coverage.
            if(event.tick-firstTick<16){++config.startupOpportunities;config.startupHits+=hit;config.startupAgreements+=agreement;}
            else {++config.opportunities;config.hits+=hit;config.agreements+=agreement;config.referenceHits+=referenceHit;config.referenceAgreements+=referenceAgreement;}
        }
    }
};
thread_local bool collectingStudy=false;
thread_local std::optional<Schedule> selectedSchedule;
thread_local std::unique_ptr<Session> session;
void onOpportunity(const HiringDiagnostics::Opportunity& event,void* context)
{ static_cast<Session*>(context)->opportunity(event); }
}
bool enabled(){return collectingStudy;}
void configure(bool shadow)
{
    HiringDiagnostics::setObserver(nullptr,nullptr);session.reset();selectedSchedule.reset();collectingStudy=shadow;
}
void configureScheduled(Schedule selected)
{
    if(selected.delay>8 || !selected.refresh || selected.refresh>8 || !selected.capacity || selected.capacity>128 || selected.delay+selected.refresh-1>8 || selected.extras < -1 || selected.extras>4 || (selected.coordinate&&(!selected.availableOnly||selected.extras<0)))
        throw std::invalid_argument("Hiring study schedule exceeds the eight-tick trial contract");
    configure(false);selectedSchedule=selected;collectingStudy=true;
}
std::uint32_t requirements(const Game& game,bool paused)
{
    if(!collectingStudy || paused)return 0;
    if(selectedSchedule){bool admit=false;for(int t=0;t<game.teamsCount();++t)admit|=game.stepCounter%selectedSchedule->refresh==unsigned(t)%selectedSchedule->refresh;
        if(!admit)return 0;}
    return SimulationSnapshot::Simulation & ~SimulationSnapshot::bit(SimulationSnapshot::Component::Growth);
}
void advance(const Game& game)
{ if(session && session->game==&game && session->schedule)session->joinDue(game.stepCounter); }
void drain(const Game& game)
{ if(session && session->game==&game)session->drain(); }
void observe(const Game& game,const SimulationSnapshot::Handle& world,bool paused)
{
    if(!collectingStudy || paused)return;
    if(!session || session->game!=&game){session=std::make_unique<Session>(game,selectedSchedule);if(!selectedSchedule || selectedSchedule->score)HiringDiagnostics::setObserver(onOpportunity,session.get());}
    if(session->last==world.tick)return;
    if(session->last!=unsigned(-1) && world.tick<session->last)throw std::logic_error("Hiring shadow boundary moved backwards");
    session->last=world.tick;
    if(session->schedule)
    {
        const auto& config=*session->schedule;
        auto batch=std::make_unique<Batch>();batch->tick=world.tick;batch->due=world.tick+config.delay;
        std::optional<HiringPlanner::Inputs> inputs;
        std::vector<ComputeExecutor::Group> groups;
        for(int team=0;team<game.teamsCount();++team)if(world.tick%config.refresh==unsigned(team)%config.refresh)
        {
            if(!inputs){const auto start=std::chrono::steady_clock::now();inputs=HiringPlanner::capture(game,world);
                session->supplementNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();}
            auto work=std::make_unique<Work>();work->inputs=*inputs;work->cache=session->caches[team].get();work->team=team;work->capacity=config.capacity;work->split=config.split;work->options={config.extras,config.availableOnly,true,true,config.coordinate};
            std::erase_if(work->inputs.routes,[&](const auto& r){return Building::GIDtoTeam(r.building.gid)!=team;});
            std::erase_if(work->inputs.supplies,[&](const auto& s){return Building::GIDtoTeam(s.building.gid)!=team;});
            work->inputs.routeBytes=0;for(const auto& route:work->inputs.routes)work->inputs.routeBytes+=route.bytes;
            work->outputBound=sizeof(Output)+work->inputs.routeBytes+work->inputs.supplies.capacity()*sizeof(HiringPlanner::Inputs::Supply);
            for(const auto& b:world.entities->buildings)if(b.team==team && HiringPlanner::needsCandidates(b)) {
                const auto& type=world.catalogs->buildings->at(b.typeNum).resolvedType;
                unsigned jobs=0;for(unsigned r=0;r<MaterialCount;++r)if(type.maxMaterial[r]>0 || b.wishedMaterials[r]>0)jobs+=2;
                for(unsigned role=0;role<3;++role)jobs+=type.attractsRole(role);
                work->outputBound+=2*sizeof(HiringPlanner::BuildingLists)+2*jobs*sizeof(HiringPlanner::List)+jobs*config.capacity*sizeof(HiringPlanner::Candidate);
            }
            groups.push_back({1,{Work::run,work.get()},ComputeExecutor::HiringLaneBase+unsigned(team)});batch->jobs.push_back(std::move(work));
        }
        if(batch->jobs.empty())return;
        static_assert(8<=ComputeExecutor::HiringHorizon);
        static_assert(SimulationSnapshot::BufferPool<int>::Limit>=ComputeExecutor::HiringHorizon+2);
        static_assert(ComputeExecutor::HiringLaneBase+Team::MAX_COUNT<=ComputeExecutor::Lanes);
        if(session->pending.size()>config.delay)throw std::logic_error("Hiring pending generations exceeded their horizon");
        if(config.parallel){batch->executor=&const_cast<Game&>(game).map.computeExecutor();batch->ticket=batch->executor->submit(groups,ComputeExecutor::advanceDue(batch->due));}
        else for(auto& job:batch->jobs)Work::run(job.get(),0);
        session->submitted+=batch->jobs.size();session->pending.push_back(std::move(batch));
        session->maxPending=std::max<std::uint64_t>(session->maxPending,session->pending.size());++session->observations;session->account();
        if(!config.delay)session->joinDue(world.tick);
        return;
    }
    const auto start=std::chrono::steady_clock::now();
    auto inputs=HiringPlanner::capture(game,world);
    session->supplementNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    std::uint64_t bytes=0;
    for(int team=0;team<game.teamsCount();++team)
    {
        auto output=std::make_shared<Output>(session->caches[team]->rank(inputs,team));
        session->retain(std::move(output));
        auto& history=session->history[team];
        while(!history.empty() && world.tick-history.front()->tick>15)history.pop_front();
        bytes+=session->caches[team]->bytes();for(const auto& previous:history)bytes+=previous->retainedBytes;
    }
    ++session->observations;
    if(world.tick && !(world.tick%128))std::cerr<<"HIRING_SHADOW_PROGRESS tick="<<world.tick<<" planning_ns="<<session->planningNs<<" bytes="<<bytes<<'\n';
    session->peakBytes=std::max(session->peakBytes,bytes);
    if(bytes>160ull*1024*1024)throw std::length_error("Hiring shadow matrix exceeds match memory bound");
}
void clear(const Game& game)
{
    if(session && session->game==&game) {
        // Teardown must release every lease even after a required deadline or
        // export already surfaced a failure. Destruction cannot throw twice.
        try{session->drain();}catch(const std::exception& error){std::cerr<<"Hiring study teardown: "<<error.what()<<'\n';}
        catch(...){std::cerr<<"Hiring study teardown: unknown worker failure\n";}
        HiringDiagnostics::setObserver(nullptr,nullptr);session.reset();
    }
}
void write(std::ostream& out,bool finish)
{
    Json report={{"schema_version",1},{"mode","authoritative_shadow"},{"coverage_semantics","conservative reference-pass coverage; live qualifications include prior authoritative assignments"},{"agreement_semantics","same worker incarnation for the reference building/job attempt; not full counterfactual job selection"},{"delay_gate_established",false},{"persistent_exclusion_gate_established",false},{"startup_ticks",16},{"gate_coverage",0.95},{"minimum_opportunities",200},{"configurations",Json::array()}};
    report["planning_policy"]="observed_job_demand";
    if(session) {
        if(finish)session->drain();
        // Draining finishes future outputs without publishing them early.
        report["startup_tick_origin"]=session->firstTick;report["scheduled"]=bool(session->schedule);report["memory_accounting"]=session->schedule?"reserved_cache_and_output_bound":"retained_matrix_bytes";report["output_digest"]=session->outputDigest;
        report["submitted"]=session->submitted;report["published"]=session->publishedCount;
        report["deadline_wait_ns"]=session->waitNs;report["owner_jobs"]=session->ownerJobs;report["worker_jobs"]=session->workerJobs;report["max_pending_batches"]=session->maxPending;
        if(session->schedule){const auto& c=*session->schedule;report["schedule"]={{"K",c.delay},{"R",c.refresh},{"L",c.capacity},{"policy",c.split?"split":"combined"},{"execution",c.parallel?"pool":"owner"},{"extras",c.extras},{"available_only",c.availableOnly},{"coordinate",c.coordinate}};}
        report["shared_route_peak_retired_bytes"]=SharedBuildingGradient::peakRetiredBytes.load();
        report["shared_route_owner_wait_ns"]=SharedBuildingGradient::ownerWaitNs.load()-session->ownerWaitStart;
        report["shared_route_owner_contentions"]=SharedBuildingGradient::ownerContentions.load()-session->ownerContentionStart;
        report["planner_metrics_scope"]="retained/published refreshes, including startup; excludes drained future outputs";
#define HIRING_REPORT_METRIC(name) report["planner_metrics"][#name]=session->plannerMetrics.name;
        HIRING_PLANNER_METRICS(HIRING_REPORT_METRIC)
#undef HIRING_REPORT_METRIC
        report["observations"]=session->observations;report["planning_ns"]=session->planningNs;
        report["supplement_ns"]=session->supplementNs;report["candidate_visits"]=session->visits;report["peak_accounted_bytes"]=session->peakBytes;
        for(const auto& c:session->matrix) report["configurations"].push_back({{"K",c.delay},{"R",c.refresh},{"L",c.capacity},{"policy",c.split?"split":"combined"},
            {"trial_age_eligible",c.delay+c.refresh-1<=8},{"opportunities",c.opportunities},{"hits",c.hits},
            {"coverage",c.opportunities?double(c.hits)/c.opportunities:0.0},
            {"independent_coverage",c.opportunities?double(c.referenceHits)/c.opportunities:0.0},
            {"independent_exact_worker_agreements",c.referenceAgreements},
            {"independent_exact_worker_agreement",c.opportunities?double(c.referenceAgreements)/c.opportunities:0.0},
            {"independent_agreement_given_covered",c.referenceHits?double(c.referenceAgreements)/c.referenceHits:0.0},
            {"exact_worker_agreements",c.agreements},{"exact_worker_agreement",c.opportunities?double(c.agreements)/c.opportunities:0.0},
            {"agreement_given_covered",c.hits?double(c.agreements)/c.hits:0.0},{"startup_agreements",c.startupAgreements},{"startup_opportunities",c.startupOpportunities},{"startup_hits",c.startupHits},
            {"examined",c.examined},{"missing_lists",c.missing},{"busy",c.busy},{"replaced",c.replaced},{"qualification",c.qualification},{"already_used",c.alreadyUsed},
            {"coverage_gate_passed",c.opportunities>=200 && c.hits*100>=c.opportunities*95 && c.delay+c.refresh-1<=8}});
    }
    out<<report.dump(2)<<'\n';
}
}
