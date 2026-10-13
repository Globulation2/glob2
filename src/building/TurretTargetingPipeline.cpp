// SPDX-License-Identifier: GPL-3.0-or-later
#include "TurretTargeting.h"
#include "Game.h"
#include "Team.h"
#include "Building.h"
#include "ComputeExecutor.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <optional>
#include <tuple>

namespace TurretTargeting
{
namespace
{
struct IdentityLess { bool operator()(BuildingRef a,BuildingRef b) const { return std::tie(a.gid,a.generation)<std::tie(b.gid,b.generation); } };
constexpr std::size_t ShortlistLimit=32u*1024u*1024u;
bool compatible(const Source& a,const Building& b)
{
    if (a.identity!=Game::refOf(&b) || b.buildingState==Building::DEAD
        || a.x!=b.posX || a.y!=b.posY || a.width!=b.runtime->width || a.height!=b.runtime->height
        || a.range!=b.runtime->shootingRange || a.speed!=b.runtime->shootSpeed
        || a.buildingDamage!=b.runtime->projectileBuildingDamage
        || a.unitTeams!=(b.owner->attackableTeams() & ~b.owner->sharedVisionExchange)
        || a.buildingTeams!=b.owner->attackableTeams() || a.damage.size()!=b.runtime->unitCount) return false;
    for (unsigned i=0;i<a.damage.size();++i) if (a.damage[i]!=b.runtime->damage(i)) return false;
    return true;
}
bool due(const Game& game,unsigned team,const Configuration& rules)
{
    return game.stepCounter%rules.refresh==team%rules.refresh;
}
std::size_t recordBytes(const Source& source,std::size_t capacity)
{
    return sizeof(Source)+sizeof(std::vector<Entry>)+capacity*sizeof(Entry)+source.damage.capacity()*sizeof(int)+512;
}
}
struct Pipeline::State
{
    struct Record { Source source; std::vector<Entry> entries; Uint32 observed=0; };
    struct Job {
        const Observation* observation=nullptr;
        unsigned capacity=0,margin=0;
        std::vector<Source> sources;
        std::vector<Record> output;
        static void run(void* context,std::size_t) {
            auto& job=*static_cast<Job*>(context);
            job.output.reserve(job.sources.size());
            for (auto& source:job.sources) {
                auto result=plan(source,*job.observation,job.capacity,job.margin);
                job.output.push_back({std::move(source),std::move(result.entries),job.observation->world.tick});
            }
            // Moved sources no longer own damage allocations. All outputs are
            // private to this job until its batch is joined.
        }
    };
    struct Generation {
        Uint64 deadline=0;
        std::unique_ptr<Observation> observation;
        std::vector<std::unique_ptr<Job>> jobs;
        ComputeExecutor::Batch batch;
        std::exception_ptr error;
        std::size_t reserved=0;
    };
    ComputeExecutor& executor;
    Configuration rules;
    std::optional<Uint32> admitted;
    Observation indexCache; // Index only; never retains a snapshot or offense.
    std::deque<std::unique_ptr<Generation>> pending;
    std::map<BuildingRef,Record,IdentityLess> published;
    Statistics metrics;
    std::size_t reservations=0,publishedBytes=0;
    State(Game& game,Configuration configuration):executor(game.map.computeExecutor()),rules(configuration) {}
    void complete(Generation& generation) {
        if (!generation.batch.empty()) {
            const auto start=std::chrono::steady_clock::now();
            try { executor.join(generation.batch); } catch (...) { generation.error=std::current_exception(); }
            metrics.waitNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            generation.observation.reset(); // Completed jobs need no world lease.
        }
    }
    void account() { metrics.shortlistBytes=reservations+publishedBytes; }
};
Pipeline::Pipeline(Game& game,Configuration rules):state(std::make_unique<State>(game,rules))
{
    if ((rules.delay!=1 && rules.delay!=2 && rules.delay!=4)
        || (rules.refresh!=1 && rules.refresh!=2 && rules.refresh!=4)
        || rules.delay+rules.refresh-1>4
        || (rules.capacity!=4 && rules.capacity!=8 && rules.capacity!=16 && rules.capacity!=32) || rules.margin>2)
        throw std::invalid_argument("Invalid turret shadow rules");
    static_assert(ComputeExecutor::TurretHorizon>=4);
}
Pipeline::~Pipeline() { drain(); }
bool Pipeline::admissionPending(const Game& game,bool paused) const
{
    return !paused && state->admitted!=game.stepCounter;
}
SimulationSnapshot::Requirements Pipeline::pendingRequirements(const Game& game,bool paused) const
{
    const auto& s=*state;
    if (!admissionPending(game,paused)) return 0;
    for (int team=0;team<game.mapHeader.getNumberOfTeams();++team)
        if (due(game,team,s.rules)) for (const auto* turret:game.teams[team]->turrets)
            if (turret->buildingState!=Building::DEAD && turret->runtime->shootSpeed>=256) return requirements;
    return 0;
}
void Pipeline::admit(const Game& game,const SimulationSnapshot::Handle& captured,bool paused)
{
    auto& s=*state;
    if (paused || s.admitted==game.stepCounter) return;
    auto generation=std::make_unique<State::Generation>();
    generation->deadline=Uint64(game.stepCounter)+s.rules.delay;
    for (int team=0;team<game.mapHeader.getNumberOfTeams();++team) if (due(game,team,s.rules)) {
        auto job=std::make_unique<State::Job>();
        job->capacity=s.rules.capacity; job->margin=s.rules.margin;
        for (const auto* turret:game.teams[team]->turrets) {
            if (turret->buildingState==Building::DEAD || turret->runtime->shootSpeed<256) continue;
            job->sources.push_back(source(*turret));
        }
        std::sort(job->sources.begin(),job->sources.end(),[](const auto& a,const auto& b){return IdentityLess{}(a.identity,b.identity);});
        for (const auto& source:job->sources) generation->reserved+=recordBytes(source,s.rules.capacity);
        if (!job->sources.empty()) generation->jobs.push_back(std::move(job));
    }
    if (generation->jobs.empty()) { s.admitted=game.stepCounter; return; }
    if (generation->reserved>ShortlistLimit-s.reservations-s.publishedBytes)
        throw std::length_error("Turret shadow shortlist storage bound exceeded");
    const auto start=std::chrono::steady_clock::now();
    generation->observation=std::make_unique<Observation>(observe(game,captured,&s.indexCache));
    s.indexCache.index=generation->observation->index;
    s.indexCache.indexBudget=generation->observation->indexBudget;
    s.metrics.indexNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    s.metrics.indexBytes=retainedIndexBytes(s.indexCache);
    std::vector<ComputeExecutor::Group> groups;
    for (auto& job:generation->jobs) {
        job->observation=generation->observation.get();
        groups.push_back({1,{State::Job::run,job.get()}});
    }
    s.pending.push_back(std::move(generation));
    auto& scheduled=*s.pending.back();
    try { scheduled.batch=s.executor.submit(groups,ComputeExecutor::advanceDue(scheduled.deadline)); }
    catch (...) { s.pending.pop_back(); throw; }
    s.reservations+=scheduled.reserved;
    ++s.metrics.admissions; ++s.metrics.observations; s.metrics.teamJobs+=scheduled.jobs.size();
    s.admitted=game.stepCounter;
    s.metrics.pendingPeak=std::max(s.metrics.pendingPeak,s.pending.size()); s.account();
}
void Pipeline::publish(Game& game)
{
    auto& s=*state;
    while (!s.pending.empty() && s.pending.front()->deadline<=game.stepCounter) {
        auto& generation=*s.pending.front();
        s.complete(generation);
        if (generation.error) {
            auto error=generation.error; s.reservations-=generation.reserved; s.pending.pop_front(); s.account();
            ++s.metrics.failedGenerations;
            std::rethrow_exception(error); // Never silently drop a failed generation.
        }
        for (auto& job:generation.jobs) for (auto& output:job->output) {
            auto* live=game.resolveBuilding(output.source.identity);
            if (!live || !compatible(output.source,*live)) continue;
            const auto existing=s.published.find(output.source.identity);
            if (existing!=s.published.end()) s.publishedBytes-=recordBytes(existing->second.source,existing->second.entries.capacity());
            s.publishedBytes+=recordBytes(output.source,output.entries.capacity());
            s.published.insert_or_assign(output.source.identity,std::move(output));
        }
        s.reservations-=generation.reserved; s.pending.pop_front(); ++s.metrics.publications; s.account();
    }
    for (auto it=s.published.begin();it!=s.published.end();) {
        if (!game.resolveBuilding(it->first)) {
            s.publishedBytes-=recordBytes(it->second.source,it->second.entries.capacity()); it=s.published.erase(it);
        } else ++it;
    }
    s.metrics.indexBytes=retainedIndexBytes(s.indexCache); s.account();
}
void Pipeline::drain() { for (auto& generation:state->pending) state->complete(*generation); }
const std::vector<Entry>& Pipeline::entries(const Building& turret)
{
    auto& s=*state;
    const auto it=s.published.find(Game::refOf(&turret));
    if (it!=s.published.end()) {
        if (compatible(it->second.source,turret)) return it->second.entries;
        s.publishedBytes-=recordBytes(it->second.source,it->second.entries.capacity()); s.published.erase(it); s.account();
    }
    static const std::vector<Entry> empty;
    return empty;
}
const Pipeline::Statistics& Pipeline::statistics() const { return state->metrics; }
SimulationSnapshot::Handle Pipeline::pendingObservation() const
{
    for (const auto& generation:state->pending) if (generation->observation) return generation->observation->world;
    return {};
}
}
