// SPDX-License-Identifier: GPL-3.0-or-later
#include "TurretTargeting.h"
#include "Game.h"
#include "Team.h"
#include "Building.h"
#include "Unit.h"
#include "ComputeExecutor.h"
#include "Stream.h"
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
struct SourceSignature {
    BuildingRef identity; Uint32 epoch=0;
    int x=0,y=0,width=0,height=0,range=0,speed=0;
    Uint32 unitTeams=0,buildingTeams=0,damageCount=0;
    int buildingDamage=0,rhythm=0; Uint64 damageDigest=0;
};
Uint64 damageDigest(const Building& b) {
    Uint64 hash=SimulationSnapshot::DigestSeed;
    for (unsigned i=0;i<b.runtime->unitCount;++i) hash=SimulationSnapshot::digestWord(hash,Uint32(b.runtime->damage(i)));
    return hash;
}
SourceSignature signature(const Source& s) {
    SourceSignature result{s.identity,s.epoch,s.x,s.y,s.width,s.height,s.range,s.speed,s.unitTeams,s.buildingTeams,Uint32(s.damageCount()),s.buildingDamage};
    result.rhythm=s.rhythm; result.damageDigest=s.damageDigest;
    return result;
}
bool compatible(const SourceSignature& a,const Building& b,bool inspectDamage=false)
{
    if (a.identity!=Game::refOf(&b) || a.epoch!=b.targetingEpoch || b.buildingState==Building::DEAD
        || a.x!=b.posX || a.y!=b.posY || a.width!=b.runtime->width || a.height!=b.runtime->height
        || a.range!=b.runtime->shootingRange || a.speed!=b.runtime->shootSpeed || a.rhythm!=b.runtime->shootRhythm
        || a.buildingDamage!=b.runtime->projectileBuildingDamage
        || a.damageCount!=b.runtime->unitCount) return false;
    return !inspectDamage || a.damageDigest==damageDigest(b);
}
bool due(const Game& game,unsigned team,const Configuration& rules)
{
    return game.stepCounter%rules.refresh==team%rules.refresh;
}
Uint64 sourceDigest(const SourceSignature& source)
{
    Uint64 hash=SimulationSnapshot::DigestSeed;
    for (Uint32 value : {Uint32(source.identity.gid),source.identity.generation,source.epoch,Uint32(source.x),Uint32(source.y),Uint32(source.width),Uint32(source.height),Uint32(source.range),Uint32(source.speed),source.unitTeams,source.buildingTeams,Uint32(source.buildingDamage),Uint32(source.rhythm),source.damageCount})
        hash=SimulationSnapshot::digestWord(hash,value);
    return SimulationSnapshot::digestWord(hash,source.damageDigest);
}
Uint64 recordDigest(const SourceSignature& source,const std::vector<Entry>& entries,Uint32 observed)
{
    Uint64 hash=SimulationSnapshot::digestWord(sourceDigest(source),observed);
    hash=SimulationSnapshot::digestWord(hash,entries.size());
    for (const auto& e:entries) for (auto word : {Uint32(e.kind),Uint32(e.gid),e.incarnation,Uint32(e.offsetX),Uint32(e.offsetY)})
        hash=SimulationSnapshot::digestWord(hash,word);
    return hash;
}
std::size_t recordBytes(std::size_t capacity) { return 768+capacity*16; }

}
// The allocation-ablation control grows three entries to a four-entry vector.
// Reserve its worst output capacity even before workers finish.
static unsigned outputReservation(const Configuration& rules)
{ return rules.capacity==3 && !rules.reusePreparation ? 4 : rules.capacity; }

struct Pipeline::State
{
    struct Record { SourceSignature source; std::vector<Entry> entries; Uint32 observed=0; Uint64 digest=0; };
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
        Uint64 deadline=0,inputDigest=0;
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
                job.output.push_back({signature(source),std::move(result.entries),observation.world.tick});
                auto& output=job.output.back(); output.digest=recordDigest(output.source,output.entries,output.observed);
            }
            job.sources.clear(); // Published/pending output retains signatures, never damage rows.
            if (job.profile) job.cpu=threadCpuNs()-cpuStart;
            // Outputs contain only source signatures and tagged aim references.
            // They remain private until the batch is joined.
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
    Uint64 publishedDigest=0;
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
        // recordBytes reserves at least 512 bytes per live record for container
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
            publishedBytes-=recordBytes(record.entries.capacity());
            publishedDigest^=record.digest;
            return true;
        });
    }
};
Pipeline::Pipeline(Game& game,Configuration rules):state(std::make_unique<State>(game,rules))
{
    if ((rules.delay!=1 && rules.delay!=2 && rules.delay!=4)
        || (rules.refresh!=1 && rules.refresh!=2 && rules.refresh!=4)
        || rules.delay+rules.refresh-1>4
        || (rules.capacity!=2 && rules.capacity!=3 && rules.capacity!=4 && rules.capacity!=8 && rules.capacity!=16 && rules.capacity!=32) || rules.margin>2)
        throw std::invalid_argument("Invalid turret targeting rules");
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
    std::map<const BuildingUnitInteraction*,std::pair<std::shared_ptr<const std::vector<int>>,Uint64>> rows;
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
            generation->reserved=512;
            generation->deadline=Uint64(game.stepCounter)+s.rules.delay;
        }
        for (const auto* turret:selected) {
            std::shared_ptr<const std::vector<int>> damage; Uint64 rowDigest=0;
            if (s.rules.reusePreparation) {
                auto [it,inserted]=rows.try_emplace(turret->runtime->interactions);
                if (inserted) {
                    auto row=std::make_shared<std::vector<int>>(); row->reserve(turret->runtime->unitCount);
                    rowDigest=SimulationSnapshot::DigestSeed;
                    for (unsigned id=0;id<turret->runtime->unitCount;++id) {
                        const auto value=turret->runtime->damage(id); row->push_back(value);
                        rowDigest=SimulationSnapshot::digestWord(rowDigest,Uint32(value));
                    }
                    it->second={std::move(row),rowDigest};
                }
                damage=it->second.first; rowDigest=it->second.second;
            }
            job->sources.push_back(source(*turret,std::move(damage),rowDigest)); ++s.metrics.sources;
        }
        generation->reserved+=job->sources.size()*recordBytes(outputReservation(s.rules));
        if (!job->sources.empty()) generation->jobs.push_back(std::move(job));
    }
    if (s.rules.profileCpu) s.metrics.sourceCpu+=threadCpuNs()-sourceStart;
    if (!generation) { if (s.rules.profileCpu) s.metrics.admissionCpu+=threadCpuNs()-admissionStart; s.admitted=game.stepCounter; return; }
    // Dead sources cannot fire. Reclaim their storage only when needed rather
    // than resolving the entire published table on every simulation tick.
    if (generation->reserved>ShortlistLimit-s.reservations-s.publishedStorage()) { s.reclaimDead(game); s.published.shrink_to_fit(); }
    if (generation->reserved>ShortlistLimit-s.reservations-s.publishedStorage())
        throw std::length_error("Turret shadow shortlist storage bound exceeded");
    generation->observation=std::make_unique<Observation>(observe(game,captured,&s.indexCache,s.rules.reusePreparation,s.rules.profileCpu,true));
    generation->inputDigest=generation->observation->logicalDigest;
    for (const auto& job:generation->jobs) for (const auto& source:job->sources)
        generation->inputDigest=SimulationSnapshot::digestWord(generation->inputDigest,sourceDigest(signature(source)));
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
            if (replacing) { s.publishedBytes-=recordBytes(existing->entries.capacity()); s.publishedDigest^=existing->digest; }
            s.publishedBytes+=recordBytes(output.entries.capacity());
            s.publishedDigest^=output.digest;
            if (replacing) *existing=std::move(output);
            else s.published.insert(existing,std::move(output));
        }
        s.reservations-=generation.reserved; s.pending.pop_front(); ++s.metrics.publications; s.account();
    }
    s.metrics.indexBytes=retainedIndexBytes(s.indexCache); s.account();
    if (s.rules.profileCpu) s.metrics.publicationCpu+=threadCpuNs()-publicationStart-(s.metrics.joinCpu-joinBefore);
}
void Pipeline::invalidateChangedSources(Game& game)
{
    // Catalog changes are lifecycle barriers, not a per-tick publication scan.
    // Finish private outputs so an epoch change also supersedes pending sources.
    drain();
    const auto invalidate=[&](const State::Record& record) {
        auto* b=game.resolveBuilding(record.source.identity);
        if (b && b->targetingEpoch==record.source.epoch && !compatible(record.source,*b,true))
            if (++b->targetingEpoch==0) b->targetingEpoch=1;
    };
    for (const auto& record:state->published) invalidate(record);
    for (const auto& generation:state->pending) for (const auto& job:generation->jobs)
        for (const auto& record:job->output) invalidate(record);
}
void Pipeline::forget(BuildingRef identity)
{
    auto& s=*state; const auto it=s.findPublished(identity);
    if (it!=s.published.end() && it->source.identity==identity) {
        s.publishedBytes-=recordBytes(it->entries.capacity()); s.publishedDigest^=it->digest;
        s.published.erase(it); s.account();
    }
}
void Pipeline::drain() { for (auto& generation:state->pending) state->complete(*generation); }
const std::vector<Entry>& Pipeline::entries(const Building& turret)
{
    auto& s=*state;
    const auto identity=Game::refOf(&turret);
    const auto it=s.findPublished(identity);
    if (it!=s.published.end() && it->source.identity==identity) {
        if (compatible(it->source,turret)) return it->entries;
        s.publishedBytes-=recordBytes(it->entries.capacity()); s.publishedDigest^=it->digest; s.published.erase(it); s.account();
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
namespace {
void save64(GAGCore::OutputStream* out,Uint64 value) { out->writeUint32(Uint32(value),"low"); out->writeUint32(Uint32(value>>32),"high"); }
Uint64 load64(GAGCore::InputStream* in) { const auto low=in->readUint32("low"); return low|(Uint64(in->readUint32("high"))<<32); }
}
void Pipeline::save(const Game& game,GAGCore::OutputStream* out)
{
    drain(); // Finish outputs without advancing any publication deadline.
    const auto& s=*state;
    out->writeEnterSection("turretTargeting");
    out->writeUint32(s.rules.delay,"delay"); out->writeUint32(s.rules.refresh,"refresh");
    out->writeUint32(s.rules.capacity,"capacity"); out->writeUint32(s.rules.margin,"margin");
    out->writeUint8(s.rules.forecast,"forecast"); out->writeUint8(s.rules.reusePreparation,"reusePreparation");
    out->writeUint8(s.admitted.has_value(),"admitted"); if (s.admitted) out->writeUint32(*s.admitted,"admittedTick");
    const auto record=[&](const State::Record& r) {
        out->writeEnterSection("record");
        const auto& a=r.source;
        out->writeUint16(a.identity.gid,"gid"); out->writeUint32(a.identity.generation,"generation"); out->writeUint32(a.epoch,"epoch");
        out->writeSint32(a.x,"x"); out->writeSint32(a.y,"y"); out->writeSint32(a.width,"width"); out->writeSint32(a.height,"height");
        out->writeSint32(a.range,"range"); out->writeSint32(a.speed,"speed"); out->writeSint32(a.rhythm,"rhythm");
        out->writeUint32(a.unitTeams,"unitTeams"); out->writeUint32(a.buildingTeams,"buildingTeams"); out->writeSint32(a.buildingDamage,"buildingDamage");
        out->writeUint32(a.damageCount,"damageCount"); save64(out,a.damageDigest);
        out->writeUint32(r.observed,"observed"); out->writeUint32(r.entries.size(),"entryCount");
        for (unsigned i=0;i<r.entries.size();++i) {
            out->writeEnterSection(i); const auto& e=r.entries[i];
            out->writeUint8(Uint8(e.kind),"kind"); out->writeUint16(e.gid,"gid"); out->writeUint32(e.incarnation,"incarnation");
            out->writeSint32(e.offsetX,"offsetX"); out->writeSint32(e.offsetY,"offsetY"); out->writeLeaveSection();
        }
        out->writeLeaveSection();
    };
    out->writeUint32(s.published.size(),"publishedCount");
    for (unsigned i=0;i<s.published.size();++i) { out->writeEnterSection(i); record(s.published[i]); out->writeLeaveSection(); }
    out->writeUint32(s.pending.size(),"pendingCount");
    for (unsigned i=0;i<s.pending.size();++i) {
        const auto& g=*s.pending[i]; if (g.error) std::rethrow_exception(g.error);
        out->writeEnterSection(i);
        save64(out,g.deadline-game.stepCounter); save64(out,g.inputDigest);
        unsigned count=0; for (const auto& j:g.jobs) count+=j->output.size(); out->writeUint32(count,"recordCount");
        unsigned n=0; for (const auto& j:g.jobs) for (const auto& r:j->output) { out->writeEnterSection(n++); record(r); out->writeLeaveSection(); }
        out->writeLeaveSection();
    }
    out->writeLeaveSection();
}
bool Pipeline::load(const Game& game,GAGCore::InputStream* in)
{
    try {
        auto& s=*state;
        if (!s.pending.empty() || !s.published.empty()) return false;
        in->readEnterSection("turretTargeting");
        Configuration rules;
        rules.delay=in->readUint32("delay"); rules.refresh=in->readUint32("refresh");
        rules.capacity=in->readUint32("capacity"); rules.margin=in->readUint32("margin");
        rules.forecast=in->readUint8("forecast"); rules.reusePreparation=in->readUint8("reusePreparation");
        Pipeline validation(const_cast<Game&>(game),rules); // Same bounded rule validation as admission.
        s.rules=rules;
        if (in->readUint8("admitted")) { s.admitted=in->readUint32("admittedTick"); if (*s.admitted>game.stepCounter) return false; }
        std::size_t loaded=256;
        const auto record=[&]() {
            in->readEnterSection("record"); State::Record r; auto& a=r.source;
            a.identity.gid=in->readUint16("gid"); a.identity.generation=in->readUint32("generation"); a.epoch=in->readUint32("epoch");
            a.x=in->readSint32("x"); a.y=in->readSint32("y"); a.width=in->readSint32("width"); a.height=in->readSint32("height");
            a.range=in->readSint32("range"); a.speed=in->readSint32("speed"); a.rhythm=in->readSint32("rhythm");
            a.unitTeams=in->readUint32("unitTeams"); a.buildingTeams=in->readUint32("buildingTeams"); a.buildingDamage=in->readSint32("buildingDamage");
            a.damageCount=in->readUint32("damageCount"); a.damageDigest=load64(in);
            if (a.identity.gid>=Building::MAX_COUNT*Team::MAX_COUNT || !a.identity.generation
                || a.x<0 || a.x>=game.map.getW() || a.y<0 || a.y>=game.map.getH()
                || a.width<=0 || a.height<=0 || a.range<0 || a.speed<256 || a.rhythm<0 || a.damageCount>65536) throw std::runtime_error("Invalid saved turret source");
            if (loaded+recordBytes(rules.capacity)>ShortlistLimit) throw std::length_error("Saved turret lists exceed storage bound");
            r.observed=in->readUint32("observed"); const auto count=in->readUint32("entryCount");
            if (r.observed>game.stepCounter || count>rules.capacity) throw std::runtime_error("Invalid saved turret observation");
            r.entries.reserve(count);
            for (unsigned i=0;i<count;++i) {
                in->readEnterSection(i); Entry e;
                e.kind=Kind(in->readUint8("kind")); e.gid=in->readUint16("gid"); e.incarnation=in->readUint32("incarnation");
                e.offsetX=in->readSint32("offsetX"); e.offsetY=in->readSint32("offsetY"); in->readLeaveSection();
                if ((e.kind!=Kind::Building && e.kind!=Kind::Unit) || !e.incarnation || e.offsetX<0 || e.offsetY<0
                    || e.gid>=(e.kind==Kind::Building ? Building::MAX_COUNT : Unit::MAX_COUNT)*Team::MAX_COUNT || (e.kind==Kind::Unit && (e.offsetX || e.offsetY))) throw std::runtime_error("Invalid saved turret entry");
                r.entries.push_back(e);
            }
            in->readLeaveSection(); loaded+=recordBytes(r.entries.capacity()); r.digest=recordDigest(a,r.entries,r.observed); return r;
        };
        const auto count=in->readUint32("publishedCount"); if (count>Building::MAX_COUNT*Team::MAX_COUNT) return false;
        s.published.reserve(count);
        for (unsigned i=0;i<count;++i) {
            in->readEnterSection(i); auto r=record(); in->readLeaveSection();
            if (!s.published.empty() && s.published.back().source.identity.gid>=r.source.identity.gid) return false;
            s.publishedBytes+=recordBytes(r.entries.capacity()); s.publishedDigest^=r.digest; s.published.push_back(std::move(r));
        }
        const auto pending=in->readUint32("pendingCount"); if (pending>rules.delay+1) return false;
        for (unsigned i=0;i<pending;++i) {
            in->readEnterSection(i); auto g=std::make_unique<State::Generation>();
            const auto remaining=load64(in); if (remaining>rules.delay) return false;
            g->deadline=Uint64(game.stepCounter)+remaining; g->inputDigest=load64(in);
            if (!s.pending.empty() && s.pending.back()->deadline>=g->deadline) return false;
            const auto records=in->readUint32("recordCount"); if (records>Building::MAX_COUNT*Team::MAX_COUNT) return false;
            auto job=std::make_unique<State::Job>(); job->output.reserve(records); g->reserved=512;
            for (unsigned n=0;n<records;++n) {
                in->readEnterSection(n); auto r=record(); in->readLeaveSection();
                if (!job->output.empty() && !IdentityLess{}(job->output.back().source.identity,r.source.identity)) return false;
                g->reserved+=recordBytes(outputReservation(rules)); job->output.push_back(std::move(r));
            }
            g->jobs.push_back(std::move(job)); s.reservations+=g->reserved; s.pending.push_back(std::move(g)); in->readLeaveSection();
        }
        in->readLeaveSection(); s.account();
        return s.metrics.shortlistBytes<=ShortlistLimit;
    } catch (const std::exception&) { return false; }
}
Uint32 Pipeline::checkSum(const Game& game) const
{
    const auto& s=*state; Uint64 hash=SimulationSnapshot::DigestSeed;
    const auto add=[&](Uint64 value){ hash=SimulationSnapshot::digestWord(hash,value); };
    add(s.rules.delay); add(s.rules.refresh); add(s.rules.capacity); add(s.rules.margin); add(s.rules.forecast); add(s.rules.reusePreparation);
    add(s.admitted.has_value()); if (s.admitted) add(*s.admitted);
    add(s.publishedDigest);
    add(s.pending.size());
    for (const auto& g:s.pending) { add(g->deadline); add(g->inputDigest); }
    return Uint32(hash)^Uint32(hash>>32);
}

}
