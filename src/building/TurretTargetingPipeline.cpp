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
#include <thread>
#include <mutex>

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
        || a.buildingTeams!=b.owner->attackableTeams() || a.damageCount()!=b.runtime->unitCount) return false;
    for (unsigned i=0;i<a.damageCount();++i) if (a.damageAt(i)!=b.runtime->damage(i)) return false;
    return true;
}
bool due(const Game& game,unsigned team,const Configuration& rules)
{
    return game.stepCounter%rules.refresh==team%rules.refresh;
}
std::size_t recordBytes(const Source& source,std::size_t capacity)
{
    return sizeof(Source)+sizeof(std::vector<Entry>)+capacity*sizeof(Entry)+(source.damage.capacity()+(source.sharedDamage ? source.sharedDamage->capacity() : 0))*sizeof(int)+512;
}
}
struct Pipeline::State
{
    struct Record { Source source; std::vector<Entry> entries; Uint32 observed=0; };
    struct Job;
    struct IndexPreparation {
        Observation* observation=nullptr;
        std::shared_ptr<IndexPreparation> previous;
        std::shared_ptr<const OccupiedIndex> result;
        std::once_flag indexReady;
        bool profile=false,preparedOnOwner=false;
        std::thread::id ownerThread;
        Uint64 indexNs=0;
        std::exception_ptr error;
        void prepare() {
            std::call_once(indexReady,[&] {
                try {
                    // Reuse the previous logical observation, never whichever
                    // worker happened to finish first. Only its index is needed.
                    if (previous) previous->prepare();
                    if (!observation) throw std::logic_error("Turret index input was released before preparation");
                    const auto start=std::chrono::steady_clock::now();
                    prepareIndex(*observation,previous ? previous->result : nullptr,profile);
                    indexNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
                    if (profile) preparedOnOwner=std::this_thread::get_id()==ownerThread;
                    result=observation->index;
                } catch (...) { error=std::current_exception(); }
                previous.reset();
            });
            if (error) std::rethrow_exception(error);
        }
    };
    static_assert(sizeof(IndexPreparation)+128<=256);
    struct Generation {
        Uint64 deadline=0;
        std::unique_ptr<Observation> observation;
        std::shared_ptr<IndexPreparation> preparation;
        std::vector<std::unique_ptr<Job>> jobs;
        ComputeExecutor::Batch batch;
        std::exception_ptr error;
        std::size_t reserved=0;
    };
    struct Job {
        Generation* generation=nullptr;
        unsigned capacity=0,margin=0;
        bool profile=false; Uint64 cpu=0;
        bool ranOnOwner=false;
        std::vector<Source> sources;
        std::vector<Record> output;
        static void run(void* context,std::size_t) {
            auto& job=*static_cast<Job*>(context);
            job.generation->preparation->prepare();
            const auto& observation=*job.generation->observation;
            if (job.profile) job.ranOnOwner=std::this_thread::get_id()==job.generation->preparation->ownerThread;
            const auto cpuStart=job.profile ? threadCpuNs() : 0;
            job.output.reserve(job.sources.size());
            for (auto& source:job.sources) {
                auto result=plan(source,observation,job.capacity,job.margin);
                job.output.push_back({std::move(source),std::move(result.entries),observation.world.tick});
            }
            if (job.profile) job.cpu=threadCpuNs()-cpuStart;
            // Moved sources no longer own damage allocations. All outputs are
            // private to this job until its batch is joined.
        }
    };
    ComputeExecutor& executor;
    Configuration rules;
    std::optional<Uint32> admitted;
    Observation indexCache; // Shared budget only; no snapshot, offense or index.
    std::shared_ptr<IndexPreparation> lastPreparation;
    std::deque<std::unique_ptr<Generation>> pending;
    // Ordered contiguous records avoid a node allocation and tree walk per
    // turret. References returned by entries are consumed before publication.
    std::vector<Record> published;
    Statistics metrics;
    std::size_t reservations=0,publishedBytes=0;
    static_assert(sizeof(Record)*2<=512);
    State(Game& game,Configuration configuration):executor(game.map.computeExecutor()),rules(configuration) {}
    auto findPublished(BuildingRef identity) {
        return std::lower_bound(published.begin(),published.end(),identity,[](const Record& record,BuildingRef key) {
            return IdentityLess{}(record.source.identity,key);
        });
    }
    void complete(Generation& generation) {
        if (!generation.batch.empty()) {
            const auto joinStart=rules.profileCpu ? threadCpuNs() : 0;
            const auto start=std::chrono::steady_clock::now();
            try { executor.join(generation.batch); } catch (...) { generation.error=std::current_exception(); }
            if (rules.profileCpu) metrics.joinCpu+=threadCpuNs()-joinStart;
            metrics.waitNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            for (const auto& job:generation.jobs) {
                metrics.planningCpu+=job->cpu;
                if (job->ranOnOwner) metrics.ownerPlanningCpu+=job->cpu;
            }
            if (!generation.error) {
                metrics.backgroundIndexCpu+=generation.observation->indexCpu;
                if (generation.preparation->preparedOnOwner) metrics.ownerBackgroundIndexCpu+=generation.observation->indexCpu;
                metrics.indexNs+=generation.preparation->indexNs;
                metrics.rebuiltChunks+=generation.observation->rebuiltChunks;
                metrics.sharedChunks+=generation.observation->sharedChunks;
            }
            generation.preparation->observation=nullptr;
            generation.observation.reset(); // Completed jobs need no world lease.
        }
    }
    std::size_t publishedStorage() const {
        // recordBytes already reserves 512 bytes per live record for container
        // storage. Charge any excess retained vector capacity after removals too.
        const auto storage=published.capacity()*sizeof(Record), covered=published.size()*512;
        // Pending preparation metadata is charged with its generation. One
        // completed preparation can survive as the reuse baseline.
        return publishedBytes+(storage>covered ? storage-covered : 0)+256;
    }
    void account() { metrics.shortlistBytes=reservations+publishedStorage(); }
    void reclaimDead(const Game& game) {
        std::erase_if(published,[&](const Record& record) {
            if (game.resolveBuilding(record.source.identity)) return false;
            publishedBytes-=recordBytes(record.source,record.entries.capacity());
            return true;
        });
    }
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
            if (turret->buildingState!=Building::DEAD && turret->runtime->shootSpeed>=256
                && (!s.rules.forecast || refreshNeeded(turret->shootingCooldownRemaining(),turret->runtime->shootRhythm,s.rules.delay,s.rules.refresh))) return s.rules.reusePreparation ? targetingRequirements : requirements;
    return 0;
}
void Pipeline::admit(const Game& game,const SimulationSnapshot::Handle& captured,bool paused)
{
    auto& s=*state;
    if (paused || s.admitted==game.stepCounter) return;
    const auto admissionStart=s.rules.profileCpu ? threadCpuNs() : 0;
    const auto sourceStart=admissionStart;
    // Boundary-local preparation table. Values are immutable and job-owned;
    // no mutable query cache survives admission.
    std::map<const BuildingUnitInteraction*,std::shared_ptr<const std::vector<int>>> rows;
    std::unique_ptr<State::Generation> generation;
    for (int team=0;team<game.mapHeader.getNumberOfTeams();++team) if (due(game,team,s.rules)) {
        std::vector<const Building*> selected;
        for (const auto* turret:game.teams[team]->turrets) {
            if (turret->buildingState==Building::DEAD || turret->runtime->shootSpeed<256) continue;
            if (s.rules.forecast && !refreshNeeded(turret->shootingCooldownRemaining(),turret->runtime->shootRhythm,s.rules.delay,s.rules.refresh)) { ++s.metrics.skipped; continue; }
            selected.push_back(turret);
        }
        if (selected.empty()) continue;
        // Sort small references before constructing sources. Cooling teams need
        // neither a job nor source/damage storage, and the admitted vector grows once.
        std::sort(selected.begin(),selected.end(),[](const auto* a,const auto* b){return IdentityLess{}(Game::refOf(a),Game::refOf(b));});
        auto job=std::make_unique<State::Job>();
        job->capacity=s.rules.capacity; job->margin=s.rules.margin; job->profile=s.rules.profileCpu;
        job->sources.reserve(selected.size());
        if (!generation) {
            generation=std::make_unique<State::Generation>();
            generation->reserved=sizeof(State::Generation)+sizeof(State::IndexPreparation)+128;
            generation->deadline=Uint64(game.stepCounter)+s.rules.delay;
        }
        for (const auto* turret:selected) {
            std::shared_ptr<const std::vector<int>> damage;
            if (s.rules.reusePreparation) {
                auto [it,inserted]=rows.try_emplace(turret->runtime->interactions);
                if (inserted) {
                    auto row=std::make_shared<std::vector<int>>(); row->reserve(turret->runtime->unitCount);
                    for (unsigned id=0;id<turret->runtime->unitCount;++id) row->push_back(turret->runtime->damage(id));
                    it->second=std::move(row);
                }
                damage=it->second;
            }
            job->sources.push_back(source(*turret,std::move(damage))); ++s.metrics.sources;
        }
        for (const auto& source:job->sources) generation->reserved+=recordBytes(source,s.rules.capacity);
        if (!job->sources.empty()) generation->jobs.push_back(std::move(job));
    }
    if (s.rules.profileCpu) s.metrics.sourceCpu+=threadCpuNs()-sourceStart;
    if (!generation) { if (s.rules.profileCpu) s.metrics.admissionCpu+=threadCpuNs()-admissionStart; s.admitted=game.stepCounter; return; }
    // Dead sources cannot fire. Reclaim their storage only when needed rather
    // than resolving the entire published table on every simulation tick.
    if (generation->reserved>ShortlistLimit-s.reservations-s.publishedStorage()) s.reclaimDead(game);
    if (generation->reserved>ShortlistLimit-s.reservations-s.publishedStorage())
        throw std::length_error("Turret shadow shortlist storage bound exceeded");
    generation->observation=std::make_unique<Observation>(observe(game,captured,&s.indexCache,s.rules.reusePreparation,s.rules.profileCpu,true));
    generation->preparation=std::make_shared<State::IndexPreparation>();
    generation->preparation->observation=generation->observation.get();
    generation->preparation->previous=s.lastPreparation;
    generation->preparation->profile=s.rules.profileCpu;
    if (s.rules.profileCpu) generation->preparation->ownerThread=std::this_thread::get_id();
    s.metrics.sourceCpu+=generation->observation->factsCpu;
    s.indexCache.indexBudget=generation->observation->indexBudget;
    s.metrics.indexBytes=retainedIndexBytes(s.indexCache);
    std::vector<ComputeExecutor::Group> groups;
    for (auto& job:generation->jobs) {
        job->generation=generation.get();
        groups.push_back({1,{State::Job::run,job.get()}});
    }
    s.pending.push_back(std::move(generation));
    auto& scheduled=*s.pending.back();
    try { scheduled.batch=s.executor.submit(groups,ComputeExecutor::advanceDue(scheduled.deadline)); }
    catch (...) { s.pending.pop_back(); throw; }
    s.lastPreparation=scheduled.preparation;
    s.reservations+=scheduled.reserved;
    ++s.metrics.admissions; ++s.metrics.observations; s.metrics.teamJobs+=scheduled.jobs.size();
    s.admitted=game.stepCounter;
    if (s.rules.profileCpu) s.metrics.admissionCpu+=threadCpuNs()-admissionStart;
    s.metrics.pendingPeak=std::max(s.metrics.pendingPeak,s.pending.size()); s.account();
}
void Pipeline::publish(Game& game)
{
    auto& s=*state;
    const auto publicationStart=s.rules.profileCpu ? threadCpuNs() : 0;
    const auto joinBefore=s.metrics.joinCpu;
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
            // One record per slot. A valid new incarnation replaces any old
            // incarnation in that slot; old pending outputs still fail resolution.
            const auto existing=s.findPublished({output.source.identity.gid,0});
            const bool replacing=existing!=s.published.end() && existing->source.identity.gid==output.source.identity.gid;
            if (replacing) s.publishedBytes-=recordBytes(existing->source,existing->entries.capacity());
            s.publishedBytes+=recordBytes(output.source,output.entries.capacity());
            if (replacing) *existing=std::move(output);
            else s.published.insert(existing,std::move(output));
        }
        s.reservations-=generation.reserved; s.pending.pop_front(); ++s.metrics.publications; s.account();
    }
    s.metrics.indexBytes=retainedIndexBytes(s.indexCache); s.account();
    if (s.rules.profileCpu) s.metrics.publicationCpu+=threadCpuNs()-publicationStart-(s.metrics.joinCpu-joinBefore);
}
void Pipeline::drain() { for (auto& generation:state->pending) state->complete(*generation); }
const std::vector<Entry>& Pipeline::entries(const Building& turret)
{
    auto& s=*state;
    const auto identity=Game::refOf(&turret);
    const auto it=s.findPublished(identity);
    if (it!=s.published.end() && it->source.identity==identity) {
        if (compatible(it->source,turret)) return it->entries;
        s.publishedBytes-=recordBytes(it->source,it->entries.capacity()); s.published.erase(it); s.account();
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
