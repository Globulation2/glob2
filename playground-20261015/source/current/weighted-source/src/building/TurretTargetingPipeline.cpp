// SPDX-License-Identifier: GPL-3.0-or-later
#include "TurretTargeting.h"
#include "FileFormatVersions.h"
#include "Game.h"
#include "Team.h"
#include "Building.h"
#include "Unit.h"
#include "ComputeExecutor.h"
#include "Stream.h"
#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <tuple>

namespace TurretTargeting
{
namespace
{
struct IdentityLess { bool operator()(BuildingRef a,BuildingRef b) const { return std::tie(a.gid,a.generation)<std::tie(b.gid,b.generation); } };
bool validRules(const Configuration& rules)
{
    return !((rules.delay!=1 && rules.delay!=2 && rules.delay!=4)
        || (rules.refresh!=1 && rules.refresh!=2 && rules.refresh!=4)
        || rules.delay+rules.refresh-1>4
        || (rules.capacity!=2 && rules.capacity!=3 && rules.capacity!=4 && rules.capacity!=8 && rules.capacity!=16 && rules.capacity!=32) || rules.margin>2);
}
bool due(const Game& game,unsigned team,const Configuration& rules)
{
    return game.stepCounter%rules.refresh==team%rules.refresh;
}
}
struct Pipeline::State
{
    // Lists are matched by building incarnation only. firstValid rechecks every
    // entry against the live turret, so upgrades and edits need no invalidation.
    struct Record { BuildingRef identity; std::vector<Entry> entries; };
    struct IndexPreparation {
        Observation* observation=nullptr;
        std::shared_ptr<IndexPreparation> previous;
        std::shared_ptr<const OccupiedIndex> result;
        std::once_flag indexReady;
        std::exception_ptr error;
        void prepare() {
            std::call_once(indexReady,[&] {
                try {
                    // Reuse the previous logical observation, never whichever
                    // worker happened to finish first. Only its index is needed.
                    if (previous) previous->prepare();
                    if (!observation) throw std::logic_error("Turret index input was released before preparation");
                    prepareIndex(*observation,previous ? previous->result : nullptr);
                    result=observation->index;
                } catch (...) { error=std::current_exception(); }
                previous.reset();
            });
            if (error) std::rethrow_exception(error);
        }
    };
    struct Job;
    struct Generation {
        Uint64 deadline=0;
        std::unique_ptr<Observation> observation;
        std::shared_ptr<IndexPreparation> preparation;
        std::vector<std::unique_ptr<Job>> jobs;
        ComputeExecutor::Batch batch;
        std::exception_ptr error;
    };
    struct Job {
        Generation* generation=nullptr;
        unsigned capacity=0,margin=0;
        std::vector<Source> sources;
        std::vector<Record> output;
        static void run(void* context,std::size_t) {
            auto& job=*static_cast<Job*>(context);
            job.generation->preparation->prepare();
            const auto& observation=*job.generation->observation;
            job.output.reserve(job.sources.size());
            for (auto& source:job.sources)
                job.output.push_back({source.identity,plan(source,observation,job.capacity,job.margin).entries});
            job.sources.clear();
        }
    };
    ComputeExecutor& executor;
    Configuration rules;
    std::optional<Uint32> admitted;
    std::shared_ptr<IndexPreparation> lastPreparation;
    std::deque<std::unique_ptr<Generation>> pending;
    // Ordered contiguous records avoid a node allocation and tree walk per turret.
    std::vector<Record> published;
    Statistics metrics;
    State(Game& game,Configuration configuration):executor(game.map.computeExecutor()),rules(configuration) {}
    auto findPublished(BuildingRef identity) {
        return std::lower_bound(published.begin(),published.end(),identity,[](const Record& record,BuildingRef key) {
            return IdentityLess{}(record.identity,key);
        });
    }
    void complete(Generation& generation) {
        if (generation.batch.empty()) return;
        try { executor.join(generation.batch); } catch (...) { generation.error=std::current_exception(); }
        generation.preparation->observation=nullptr;
        generation.observation.reset(); // Completed jobs need no world lease.
    }
};
Pipeline::Pipeline(Game& game,Configuration rules):state(std::make_unique<State>(game,rules))
{
    if (!validRules(rules)) throw std::invalid_argument("Invalid turret targeting rules");
    static_assert(ComputeExecutor::TurretHorizon>=4);
}
Pipeline::~Pipeline() { drain(); }
bool Pipeline::admissionPending(const Game& game,bool paused) const
{
    return !paused && state->admitted!=game.stepCounter;
}
void Pipeline::admit(const Game& game,const SimulationSnapshot::Handle& captured,bool paused)
{
    auto& s=*state;
    if (!admissionPending(game,paused)) return;
    // Turrets sharing a weapon table share one immutable damage row per boundary.
    std::map<const BuildingUnitInteraction*,std::shared_ptr<const std::vector<int>>> rows;
    std::unique_ptr<State::Generation> generation;
    for (int team=0;team<game.mapHeader.getNumberOfTeams();++team) if (due(game,team,s.rules)) {
        std::vector<const Building*> selected;
        for (const auto* turret:game.teams[team]->turrets) {
            if (turret->buildingState==Building::DEAD || turret->runtime->shootSpeed<256) continue;
            if (!refreshNeeded(turret->shootingCooldownRemaining(),turret->runtime->shootRhythm,s.rules.delay,s.rules.refresh)) { ++s.metrics.skipped; continue; }
            selected.push_back(turret);
        }
        if (selected.empty()) continue;
        std::sort(selected.begin(),selected.end(),[](const auto* a,const auto* b){return IdentityLess{}(Game::refOf(a),Game::refOf(b));});
        auto job=std::make_unique<State::Job>();
        job->capacity=s.rules.capacity; job->margin=s.rules.margin;
        job->sources.reserve(selected.size());
        if (!generation) {
            generation=std::make_unique<State::Generation>();
            generation->deadline=Uint64(game.stepCounter)+s.rules.delay;
        }
        for (const auto* turret:selected) {
            auto& row=rows[turret->runtime->interactions];
            if (!row) {
                auto values=std::make_shared<std::vector<int>>(); values->reserve(turret->runtime->unitCount);
                for (unsigned id=0;id<turret->runtime->unitCount;++id) values->push_back(turret->runtime->damage(id));
                row=std::move(values);
            }
            job->sources.push_back(source(*turret,row)); ++s.metrics.sources;
        }
        generation->jobs.push_back(std::move(job));
    }
    s.admitted=game.stepCounter;
    if (!generation) return;
    generation->observation=std::make_unique<Observation>(observe(game,captured,nullptr,true));
    generation->preparation=std::make_shared<State::IndexPreparation>();
    generation->preparation->observation=generation->observation.get();
    generation->preparation->previous=s.lastPreparation;
    std::vector<ComputeExecutor::Group> groups;
    for (auto& job:generation->jobs) {
        job->generation=generation.get();
        groups.push_back({1,{State::Job::run,job.get()},ComputeExecutor::NoLane,ComputeExecutor::JobClass::Turret});
    }
    s.pending.push_back(std::move(generation));
    auto& scheduled=*s.pending.back();
    try { scheduled.batch=s.executor.submit(groups,ComputeExecutor::advanceDue(scheduled.deadline)); }
    catch (...) { s.pending.pop_back(); throw; }
    s.lastPreparation=scheduled.preparation;
    ++s.metrics.admissions; s.metrics.teamJobs+=scheduled.jobs.size();
}
void Pipeline::publish(Game& game)
{
    auto& s=*state;
    while (!s.pending.empty() && s.pending.front()->deadline<=game.stepCounter) {
        auto& generation=*s.pending.front();
        s.complete(generation);
        if (generation.error) {
            auto error=generation.error; s.pending.pop_front();
            std::rethrow_exception(error); // Never silently drop a failed generation.
        }
        for (auto& job:generation.jobs) for (auto& output:job->output) {
            if (!game.resolveBuilding(output.identity)) continue;
            // One record per slot: a new incarnation replaces the old one.
            const auto existing=s.findPublished({output.identity.gid,0});
            if (existing!=s.published.end() && existing->identity.gid==output.identity.gid) *existing=std::move(output);
            else s.published.insert(existing,std::move(output));
        }
        s.pending.pop_front(); ++s.metrics.publications;
    }
}
void Pipeline::forget(BuildingRef identity)
{
    auto& s=*state; const auto it=s.findPublished(identity);
    if (it!=s.published.end() && it->identity==identity) s.published.erase(it);
}
void Pipeline::drain() { for (auto& generation:state->pending) state->complete(*generation); }
const std::vector<Entry>& Pipeline::entries(const Building& turret)
{
    auto& s=*state;
    const auto identity=Game::refOf(&turret);
    const auto it=s.findPublished(identity);
    if (it!=s.published.end() && it->identity==identity) return it->entries;
    static const std::vector<Entry> empty;
    return empty;
}
const Pipeline::Statistics& Pipeline::statistics() const { return state->metrics; }
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
    out->writeUint8(s.admitted.has_value(),"admitted"); if (s.admitted) out->writeUint32(*s.admitted,"admittedTick");
    const auto record=[&](const State::Record& r) {
        out->writeEnterSection("record");
        out->writeUint16(r.identity.gid,"gid"); out->writeUint32(r.identity.generation,"generation");
        out->writeUint32(r.entries.size(),"entryCount");
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
        save64(out,g.deadline-game.stepCounter);
        unsigned count=0; for (const auto& j:g.jobs) count+=j->output.size(); out->writeUint32(count,"recordCount");
        unsigned n=0; for (const auto& j:g.jobs) for (const auto& r:j->output) { out->writeEnterSection(n++); record(r); out->writeLeaveSection(); }
        out->writeLeaveSection();
    }
    out->writeLeaveSection();
}
bool Pipeline::load(const Game& game,GAGCore::InputStream* in,int versionMinor)
{
    // Older formats also carried source signatures, digests and two fixed flags.
    const bool legacy=versionMinor<FILE_FORMAT_VERSION_LEAN_TURRET_TARGETING;
    try {
        auto& s=*state;
        if (!s.pending.empty() || !s.published.empty()) return false;
        in->readEnterSection("turretTargeting");
        Configuration rules;
        rules.delay=in->readUint32("delay"); rules.refresh=in->readUint32("refresh");
        rules.capacity=in->readUint32("capacity"); rules.margin=in->readUint32("margin");
        if (legacy && (in->readUint8("forecast")!=1 || in->readUint8("reusePreparation")!=1)) return false;
        if (!validRules(rules)) return false;
        s.rules=rules;
        if (in->readUint8("admitted")) { s.admitted=in->readUint32("admittedTick"); if (*s.admitted>game.stepCounter) return false; }
        const auto record=[&]() {
            in->readEnterSection("record"); State::Record r;
            r.identity.gid=in->readUint16("gid"); r.identity.generation=in->readUint32("generation");
            if (r.identity.gid>=Building::MAX_COUNT*Team::MAX_COUNT || !r.identity.generation) throw std::runtime_error("Invalid saved turret source");
            if (legacy) {
                in->readUint32("epoch");
                for (const char* name : {"x","y","width","height","range","speed","rhythm"}) in->readSint32(name);
                in->readUint32("unitTeams"); in->readUint32("buildingTeams"); in->readSint32("buildingDamage");
                in->readUint32("damageCount"); load64(in); in->readUint32("observed");
            }
            const auto count=in->readUint32("entryCount");
            if (count>rules.capacity) throw std::runtime_error("Invalid saved turret observation");
            r.entries.reserve(count);
            for (unsigned i=0;i<count;++i) {
                in->readEnterSection(i); Entry e;
                e.kind=Kind(in->readUint8("kind")); e.gid=in->readUint16("gid"); e.incarnation=in->readUint32("incarnation");
                e.offsetX=in->readSint32("offsetX"); e.offsetY=in->readSint32("offsetY"); in->readLeaveSection();
                if ((e.kind!=Kind::Building && e.kind!=Kind::Unit) || !e.incarnation || e.offsetX<0 || e.offsetY<0
                    || e.gid>=(e.kind==Kind::Building ? Building::MAX_COUNT : Unit::MAX_COUNT)*Team::MAX_COUNT) throw std::runtime_error("Invalid saved turret entry");
                r.entries.push_back(e);
            }
            in->readLeaveSection(); return r;
        };
        const auto count=in->readUint32("publishedCount"); if (count>Building::MAX_COUNT*Team::MAX_COUNT) return false;
        s.published.reserve(count);
        for (unsigned i=0;i<count;++i) {
            in->readEnterSection(i); auto r=record(); in->readLeaveSection();
            if (!s.published.empty() && s.published.back().identity.gid>=r.identity.gid) return false;
            s.published.push_back(std::move(r));
        }
        const auto pending=in->readUint32("pendingCount"); if (pending>rules.delay+1) return false;
        for (unsigned i=0;i<pending;++i) {
            in->readEnterSection(i); auto g=std::make_unique<State::Generation>();
            const auto remaining=load64(in); if (remaining>rules.delay) return false;
            g->deadline=Uint64(game.stepCounter)+remaining;
            if (legacy) load64(in);
            if (!s.pending.empty() && s.pending.back()->deadline>=g->deadline) return false;
            const auto records=in->readUint32("recordCount"); if (records>Building::MAX_COUNT*Team::MAX_COUNT) return false;
            auto job=std::make_unique<State::Job>(); job->output.reserve(records);
            for (unsigned n=0;n<records;++n) {
                in->readEnterSection(n); auto r=record(); in->readLeaveSection();
                if (!job->output.empty() && !IdentityLess{}(job->output.back().identity,r.identity)) return false;
                job->output.push_back(std::move(r));
            }
            g->jobs.push_back(std::move(job)); s.pending.push_back(std::move(g)); in->readLeaveSection();
        }
        in->readLeaveSection();
        return true;
    } catch (const std::exception&) { return false; }
}

}
