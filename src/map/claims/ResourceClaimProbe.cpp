// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceClaimProbe.h"
#include "ClaimPolicyConfig.h"
#include "ClaimRouteSearch.h"
#include "ClaimRoutePipeline.h"
#include "ResourceClaimRouteView.h"
#include "Game.h"
#include "Map.h"
#include "Unit.h"
#include "Team.h"
#include "online/Sha256.h"
#include "engine/sim/snapshot/SnapshotStorage.h"
#include <nlohmann/json.hpp>
#include <zlib.h>
#include <chrono>
#include <algorithm>
#include <map>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ResourceClaims
{
namespace {
Uint64 mix(Uint64 value)
{
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ull;
    value=(value^(value>>27))*0x94d049bb133111ebull;
    return value^(value>>31);
}
using Json=nlohmann::json;
}
struct Probe::Impl
{
    // Fixed material/phase/counter dimensions; no per-owner retention.
    enum OutcomeCounter { Eligible, Attempted, Acquired, HeldSame, RetargetAccess, RetargetDonor,
        NoCandidate, NoAttempt, Cooldown, MissingField,
        Ready, Pending, Failed, Arrived, Wait, Roam, Release, CooldownWait, CounterCount };
    std::array<std::array<std::array<Uint64,CounterCount>,2>,MaterialCount> outcomes{};
    Json outcomeSummary() const {
        return {{"version",2},{"material_phase_counters",outcomes},{"phase_names",{"movement","pickup"}},
            {"counter_names",{"eligible","attempted","acquired","held_same","retarget_access","retarget_donor_same_access",
                "no_candidate","not_attempted","cooldown_not_attempted","missing_field_not_attempted",
                "route_ready","route_pending","route_failed","route_arrived","fallback_wait","fallback_roam","fallback_release","cooldown_wait"}},
            {"scope","active natural-resource movement calls and complete adjacent pickup scans; attempted means selector/prepare invoked, even during cooldown adjacent search; no_candidate means attempted without successful selection and may retain a held claim; cooldown_wait records suppressed fallback independently; counters overlap; no delivery causality"}};
    }
    struct Baseline {
        SimulationSnapshot::Handle world;
        RouteProfile profile;
        Uint32 origin=0,target=0;Uint64 due=0;
        RouteStatus status=RouteStatus::Unreachable;
        Uint64 cost=0,expansions=0,elapsedNs=0;
        ComputeExecutor::Batch batch;
        std::exception_ptr error;
        bool published=false;
    };
    static void runBaseline(void* context,std::size_t) noexcept {
        auto& job=*static_cast<Baseline*>(context);
        try {
            const auto begin=std::chrono::steady_clock::now();const auto view=job.world.view();
            const field::Grid grid(view.width,view.height);const int tx=job.target%view.width,ty=job.target/view.width;
            thread_local RouteSearch search;
            const auto route=search.find(grid,job.origin,UINT32_MAX,
                [&](Uint32 cell){return routePassable(view,cell,job.profile);},
                [&](Uint32 cell,bool diagonal){return routeEntryCost(view,cell,job.profile,diagonal);},
                [&](Uint32 cell){const int dx=std::abs(int(cell%view.width)-tx),dy=std::abs(int(cell/view.width)-ty);
                    return std::min(dx,view.width-dx)<=1 && std::min(dy,view.height-dy)<=1;},
                [](Uint32){return Uint64(0);});
            job.status=route.status;job.cost=route.cost;job.expansions=route.expansions;
            job.elapsedNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
        } catch(...) {job.error=std::current_exception();}
        job.world={};
    }
    struct Journey {
        Uint64 task=0;Uint32 acquired=0,origin=0,source=0,access=0,material=0;
        Uint64 steps=0,cost=0;
        bool harvesting=false;
        std::unique_ptr<Baseline> baseline;
        std::string policyDigest;
        std::array<Uint64,unsigned(LiveEvent::Count)> events{};
        std::optional<ReleaseReason> releaseReason;
    };
    std::optional<ReleaseReason> pendingReleaseReason;
    LiveMetrics live;
    MovementMetrics movement;
    std::optional<Uint32> movementEndTick;
    std::optional<Uint32> lastTransportEventTick;
    std::map<Owner,Journey> journeys;
    struct ClosedJourney {Owner owner;Journey journey;const char* terminal;Uint32 tick;};
    std::vector<ClosedJourney> closedJourneys;
    Uint32 latestLiveTick=0;
    std::array<Uint64,16> journeyEligiblePhase{},journeySampledPhase{},journeyAdmittedPhase{},journeyExcludedPhase{};
    Uint64 baselineUnpublishedChecks=0,baselineUnpublishedAtFinish=0,baselineBlockingFinishJoins=0,baselineLatePublications=0,baselineMaxPublishDelay=0;
    Uint64 censoredJourneyOutcomes=0;

    static constexpr Uint32 JourneyTimeout=65536;
    static constexpr unsigned MaximumJourneys=8,MaximumJourneyAdmissions=512;
    void writeJourney(Owner owner,const Journey& j,const char* terminal,Uint32 tick) {
        const auto* baseline=j.baseline.get();
        const bool ready=baseline && baseline->published;
        const char* status=!ready?"not_ready":baseline->status==RouteStatus::Found?"reached":baseline->status==RouteStatus::Unreachable?"unreachable":"limit";
        write(Json{{"kind","journey"},{"schema_version",1},{"owner",owner},{"task",j.task},
            {"acquired_tick",j.acquired},{"end_tick",tick},{"origin",j.origin},{"source",j.source},{"access",j.access},{"material",j.material},
            {"terminal",terminal},{"steps",j.steps},{"live_cost",j.cost},{"events",j.events},
            {"baseline_cost",ready && baseline->status==RouteStatus::Found?Json(baseline->cost):Json(nullptr)},
            {"baseline_status",status},{"baseline_expansions",ready?Json(baseline->expansions):Json(nullptr)},
            {"baseline_elapsed_ns",ready?Json(baseline->elapsedNs):Json(nullptr)},
            {"baseline_contract","acquisition static occupancy; exact held-access 3x3 stance; not a dynamic-world optimum"},
            {"policy_digest",j.policyDigest},
            {"release_reason",j.releaseReason?Json(ReleaseReasonNames[unsigned(*j.releaseReason)]):Json(nullptr)}}.dump()+"\n");
        ++live.journeyCompleted;
        if(std::string_view(terminal)=="timeout" || std::string_view(terminal)=="end_of_segment")++censoredJourneyOutcomes;
    }
    void closeJourney(std::map<Owner,Journey>::iterator at,const char* terminal,Uint32 tick) {
        if(at->second.baseline && !at->second.baseline->published)
            closedJourneys.push_back({at->first,std::move(at->second),terminal,tick});
        else writeJourney(at->first,at->second,terminal,tick);
        journeys.erase(at);
    }
    void publishBaselines(Uint32 tick,bool finish=false) {
        const auto publish=[&](Journey& journey) {
            auto* baseline=journey.baseline.get();
            if(!baseline || baseline->published || (!finish && baseline->due>tick))return;
            // Diagnostic work has no simulation deadline: only retire completed
            // batches here. Explicit finish is the sole blocking observation join.
            const bool finished=executor.finished(baseline->batch);
            if(!finish && !finished){++baselineUnpublishedChecks;return;}
            if(finish){++baselineUnpublishedAtFinish;baselineBlockingFinishJoins+=!finished;}
            else if(tick>baseline->due){++baselineLatePublications;baselineMaxPublishDelay=std::max(baselineMaxPublishDelay,Uint64(tick)-baseline->due);}
            executor.join(baseline->batch,{finish?JoinReason::Save:JoinReason::TickDeadline,tick});
            if(baseline->error)std::rethrow_exception(baseline->error);
            baseline->published=true;
        };
        for(auto& [owner,journey]:journeys)publish(journey);
        for(auto at=closedJourneys.begin();at!=closedJourneys.end();) {
            publish(at->journey);
            if(!at->journey.baseline || at->journey.baseline->published) {
                writeJourney(at->owner,at->journey,at->terminal,at->tick);at=closedJourneys.erase(at);
            } else ++at;
        }
    }
    void expireJourneys(Uint32 tick) {
        latestLiveTick=std::max(latestLiveTick,tick);
        for(auto at=journeys.begin();at!=journeys.end();) {
            if(Uint64(tick)>=Uint64(at->second.acquired)+JourneyTimeout) {auto expired=at++;closeJourney(expired,"timeout",tick);}
            else ++at;
        }
    }
    Json liveSummary() const {
        return {{"version",1},{"event_counts",live.events},{"actual_moves",live.moves},{"actual_weighted_cost",live.liveCost},
            {"scope","held-material-claim-pursuit-only"},{"unclaimed_movement_available",false},
            {"movement_availability","actual_moves and actual_weighted_cost count committed moves only while a claim is held; disabled claims do not observe ordinary pursuit movement; zero is not zero total travel"},
            {"inherited_claim_coverage","held-claim moves counted from segment start; acquisition baseline and full journey unavailable until a new sampled acquisition"},
            {"journey_eligible",live.journeyEligible},{"journey_sampled",live.journeySampled},{"journey_admitted",live.journeyAdmitted},
            {"journey_completed",live.journeyCompleted},{"excluded_capacity",live.excludedCapacity},{"excluded_limit",live.excludedLimit},
            {"untracked_claim_moves",live.untrackedMoves},{"sample_every",config.sampleEvery},
            {"route_completed",live.routeCompleted},{"route_canceled",live.routeCanceled},{"route_expansions",live.routeExpansions},
            {"route_worker_elapsed_ns",live.routeElapsedNs},{"route_unobserved_work",live.routeUnobserved},{"route_restored_outputs",live.routeRestored},
            {"route_status_counts",live.routeStatuses},
            {"release_reason_version",1},{"release_reason_names",ReleaseReasonNames},{"release_reason_counts",live.releaseReasons},
            {"selection_filter_counts",{{"terrain_prefilter_rejections",live.terrainPrefilterRejections},
                {"refresh_margin_rejections",live.refreshMarginRejections},{"refresh_margin_kept",live.refreshMarginKept}}},
            {"policy_extension_version",1},{"policy_extension_counts",{{"pending_gradient_steps",live.policyExtensions[0]},
                {"traffic_sidesteps",live.policyExtensions[1]},{"traffic_wait_limit_repairs",live.policyExtensions[2]},
                {"distance_factor_rejections",live.policyExtensions[3]},{"fallback_gradient_decisions",live.policyExtensions[4]},
                {"claim_gradient_signals",live.policyExtensions[5]}}},
            {"contention_version",1},{"contention_counts",{{"ordinary_decisions",live.contention[0]},{"contention_tests",live.contention[1]},
                {"contention_triggers",live.contention[2]},{"claim_mode_decisions",live.contention[3]}}},{"route_status_names",Json::array({"found","unreachable","cost_limit","expansion_limit"})},
            {"max_active",MaximumJourneys},{"max_admissions",std::min<unsigned>(MaximumJourneyAdmissions,config.maxSamples)},
            {"timeout_ticks",JourneyTimeout},{"continuation_policy","new-segment; inherited claims are not sampled acquisitions"},
            {"sampling_censored",bool(live.excludedCapacity || live.excludedLimit)},
            {"admission_uncensored",!live.excludedCapacity && !live.excludedLimit},
            {"censored_journey_outcomes",censoredJourneyOutcomes},
            {"sampling_warning","admission under capacity pressure is workload-dependent; cluster by game, never treat journeys as independent games"},
            {"phase_bin_ticks",4096},{"phase_last_bin_starts",61440},
            {"eligible_by_phase",journeyEligiblePhase},{"sampled_by_phase",journeySampledPhase},
            {"admitted_by_phase",journeyAdmittedPhase},{"excluded_by_phase",journeyExcludedPhase},
            {"baseline_unpublished_checks",baselineUnpublishedChecks},{"baseline_unpublished_at_finish",baselineUnpublishedAtFinish},
            {"baseline_blocking_finish_joins",baselineBlockingFinishJoins},{"baseline_late_publications",baselineLatePublications},
            {"baseline_max_publish_delay_ticks",baselineMaxPublishDelay},
            {"baseline_contract","sampled off-owner Dijkstra at acquisition; static held-access stance"}};
    }
    ComputeExecutor& executor;
    Config config;
    Metrics metrics;
    std::string path;
    gzFile file=nullptr;
    SimulationSnapshot::Storage storage;
    bool finished=false;
    std::optional<ClaimPolicyConfig> cachedPolicy;
    std::string cachedPolicyDigest;

    Impl(ComputeExecutor& executor,std::string path,Config config):executor(executor),config(config),path(std::move(path))
    {
        if (!config.sampleEvery || !config.maxSamples || !config.maxOutputBytes)
            throw std::invalid_argument("invalid resource claim probe limits");
        if (std::filesystem::exists(this->path)) throw std::runtime_error("resource claim probe output already exists");
        if (std::filesystem::space(std::filesystem::path(this->path).parent_path()).available<16ull*1024*1024)
            throw std::runtime_error("insufficient free space for resource claim probe");
        file=gzopen(this->path.c_str(),"wb6");
        if (!file) throw std::runtime_error("cannot open resource claim probe output");
    }
    ~Impl()
    {
        // Explicit finish reports errors; teardown still waits for every borrower.
        const auto joinBaseline=[&](Journey& journey){if(journey.baseline)try{executor.join(journey.baseline->batch,{JoinReason::Teardown,ComputeTelemetry::NoTick});}catch(...) {}};
        for(auto& [owner,journey]:journeys)joinBaseline(journey);
        for(auto& closed:closedJourneys)joinBaseline(closed.journey);
        if(file) gzclose(file);
    }
    void write(const std::string& text)
    {
        const auto allowance=Uint64(compressBound(text.size()))+64;
        if (allowance>config.maxOutputBytes || metrics.outputBytes>config.maxOutputBytes-allowance)
            throw std::runtime_error("resource claim probe byte budget exhausted; dataset is incomplete");
        if (std::filesystem::space(std::filesystem::path(path).parent_path()).available<allowance+16ull*1024*1024)
            throw std::runtime_error("resource claim probe free-space reserve exhausted; dataset is incomplete");
        if (text.size()>std::numeric_limits<unsigned>::max() || gzwrite(file,text.data(),unsigned(text.size()))!=int(text.size())
            || gzflush(file,Z_SYNC_FLUSH)!=Z_OK)
            throw std::runtime_error("resource claim probe write failed; dataset is incomplete");
        const auto bytes=gzoffset(file);
        if (bytes<0) throw std::runtime_error("resource claim probe cannot measure output size");
        metrics.outputBytes=Uint64(bytes);
    }};

Probe::Probe(ComputeExecutor& executor,std::string path,Config config):impl(std::make_unique<Impl>(executor,std::move(path),config))
{
    impl->write(Json{{"kind","schema"},{"version",2},{"timing_clock","worker-steady-clock-elapsed"},
        {"sample_every",config.sampleEvery},{"max_samples",config.maxSamples},{"max_output_bytes",config.maxOutputBytes},
        {"movement_audit_version",1},{"movement_only",config.movementOnly},
        {"live_journey_version",1},{"live_event_names",Json::array({"acquired","released","retargeted","harvest_begin","harvested","harvest_failed",
            "goal_refresh_same","goal_refresh_changed","goal_refresh_failed","fallback_wait","fallback_roam","fallback_release",
            "route_initial","route_refresh","route_repair","route_probe_fallback","pending_path","traffic_wait","geometry_contradiction",
            "reanchor_joined","reanchor_failed","probe_tail_failed","obsolete_probe"})},
        {"decision_outcome_version",2},
        {"continuation_policy","new-segment; journeys and counters restart after load"}}.dump()+"\n");
}
Probe::~Probe()=default;
const Probe::Metrics& Probe::metrics() const {return impl->metrics;}
void Probe::publish(Uint32 tick) {
    if(!impl->finished) {
        impl->publishBaselines(tick);impl->expireJourneys(tick);
        // Publications inside a decision can precede later moves at the same
        // tick. Only a later boundary covers those events exclusively.
        if(!impl->lastTransportEventTick || tick>*impl->lastTransportEventTick)impl->movementEndTick=tick;
        else impl->movementEndTick.reset();
    }
}
void Probe::finish()
{
    if(impl->finished)return;
    impl->publishBaselines(UINT32_MAX,true);
    while(!impl->journeys.empty())impl->closeJourney(impl->journeys.begin(),"end_of_segment",impl->latestLiveTick);
    impl->write(Json{{"kind","summary"},{"complete",true},{"capture_bytes",impl->metrics.captureBytes},
        {"live",impl->liveSummary()},{"decision_outcomes",impl->outcomeSummary()},
        {"movement_audit",{{"version",1},{"scope","all-transport-committed-grid-moves"},
            {"coverage_start",impl->config.movementStartTick?Json(*impl->config.movementStartTick):Json(nullptr)},
            {"coverage_end",impl->movementEndTick?Json(*impl->movementEndTick):Json(nullptr)},
            {"coverage_end_exclusive",true},
            {"coverage_known",impl->config.movementStartTick && impl->movementEndTick && *impl->config.movementStartTick<=*impl->movementEndTick},
            {"kind_names",{"medical","claimed_resource","unclaimed_resource","return_to_job","idle","other"}},
            {"moves",impl->movement.moves},{"weighted_cost",impl->movement.weightedCost},
            {"total_moves",impl->movement.totalMoves},{"total_weighted_cost",impl->movement.totalCost},
            {"excluded_non_neighbor_moves",impl->movement.excludedNonNeighbor},
            {"unclaimed_source_separation_available",false},
            {"unclaimed_resource_scope","natural and supplier pursuit are combined; source kind unavailable"},
            {"continuation_scope","segment totals only; prior travel and journey origins unavailable"}}}}.dump()+"\n");
    const auto file=impl->file; impl->file=nullptr;
    if(gzclose(file)!=Z_OK) throw std::runtime_error("resource claim probe close failed; dataset is incomplete");
    impl->metrics.outputBytes=std::filesystem::file_size(impl->path);
    impl->finished=true;
}

const Probe::LiveMetrics& Probe::liveMetrics() const {return impl->live;}
const Probe::MovementMetrics& Probe::movementMetrics() const {return impl->movement;}
void Probe::liveEvent(const Unit& unit,LiveEvent event) const
{
    if(impl->finished || impl->config.movementOnly)return;
    const auto tick=unit.owner->game->stepCounter;impl->expireJourneys(tick);
    const auto owner=unitOwner(unit.gid,unit.scriptIdentity);
    ++impl->live.events.at(unsigned(event));
    auto at=impl->journeys.find(owner);
    if(event==LiveEvent::Acquired) {
        if(at!=impl->journeys.end())impl->closeJourney(at,"retargeted",tick);
        ++impl->live.journeyEligible;
        const auto phase=std::min<unsigned>(tick/4096,15);++impl->journeyEligiblePhase[phase];
        if(!unit.activeMaterialClaim)return;
        const auto priority=mix(0x4A4F55524E455931ull ^ mix(owner) ^ mix(unit.materialClaimTask) ^ mix(unit.owner->game->gameHeader.getRandomSeed()));
        if(priority%impl->config.sampleEvery)return;
        ++impl->live.journeySampled;++impl->journeySampledPhase[phase];
        if(impl->live.journeyAdmitted>=std::min<unsigned>(Impl::MaximumJourneyAdmissions,impl->config.maxSamples)){++impl->live.excludedLimit;++impl->journeyExcludedPhase[phase];return;}
        if(impl->journeys.size()+impl->closedJourneys.size()>=Impl::MaximumJourneys){++impl->live.excludedCapacity;++impl->journeyExcludedPhase[phase];return;}
        const auto& claim=*unit.activeMaterialClaim;Impl::Journey j;
        j.task=unit.materialClaimTask;j.acquired=tick;j.origin=unit.owner->map->coordToIndex(unit.posX,unit.posY);
        j.source=claim.source;j.access=claim.access;j.material=claim.material;
        if(unit.owner->map->materialClaimPolicy()==ActiveClaimPolicy::Experimental) {
            const auto& config=unit.owner->map->materialClaimPolicyConfig();
            if(!impl->cachedPolicy || *impl->cachedPolicy!=config) {
                impl->cachedPolicy=config;impl->cachedPolicyDigest=Online::Sha256::hex(config.canonicalJson());
            }
            j.policyDigest=impl->cachedPolicyDigest;
        }
        auto baseline=std::make_unique<Impl::Baseline>();
        using C=SimulationSnapshot::Component;
        const auto mask=SimulationSnapshot::bit(C::Catalogs)|SimulationSnapshot::bit(C::Terrain)|SimulationSnapshot::bit(C::Resources)
            |SimulationSnapshot::bit(C::Occupancy)|SimulationSnapshot::bit(C::Areas);
        const auto bytes=impl->storage.bytesCopied;
        baseline->world=SimulationSnapshot::capture(*unit.owner->game,SimulationSnapshot::captureCatalog(*unit.owner->game),mask,nullptr,&impl->storage,false);
        impl->metrics.captureBytes+=impl->storage.bytesCopied-bytes;
        baseline->profile={accessClass(unit.movementAccess()),unit.owner->me,unit.gid,OccupancyPolicy::Static};
        baseline->origin=j.origin;baseline->target=j.access;baseline->due=Uint64(tick)+1;
        auto* raw=baseline.get();j.baseline=std::move(baseline);j.events[unsigned(event)]=1;
        const auto inserted=impl->journeys.emplace(owner,std::move(j)).first;
        const ComputeExecutor::Group group{1,{Impl::runBaseline,raw},ComputeExecutor::NoLane};
        try {raw->batch=impl->executor.submit(std::span(&group,1),ComputeExecutor::boundaryDue(Uint64(tick)+1),
            {ComputeProducer::Other,tick,Uint64(tick)+1});}
        catch(...) {impl->journeys.erase(inserted);throw;}
        ++impl->live.journeyAdmitted;++impl->journeyAdmittedPhase[phase];return;
    }
    if(at==impl->journeys.end())return;
    ++at->second.events[unsigned(event)];
    if(event==LiveEvent::HarvestBegin)at->second.harvesting=true;
    else if(event==LiveEvent::Harvested)impl->closeJourney(at,"harvested",tick);
    else if(event==LiveEvent::HarvestFailed)impl->closeJourney(at,"harvest_failed",tick);
    else if(event==LiveEvent::Retargeted)impl->closeJourney(at,"retargeted",tick);
    else if(event==LiveEvent::Released && !at->second.harvesting) {
        at->second.releaseReason=impl->pendingReleaseReason;impl->closeJourney(at,"released",tick);
    }
}
void Probe::liveRelease(const Unit& unit,ReleaseReason reason) const
{
    if(impl->finished || impl->config.movementOnly)return;
    ++impl->live.releaseReasons.at(unsigned(reason));
    impl->pendingReleaseReason=reason;liveEvent(unit,LiveEvent::Released);impl->pendingReleaseReason.reset();
}
void Probe::selectionFilter(SelectionFilter filter,Uint64 count) const
{
    if(impl->finished || impl->config.movementOnly)return;
    auto& live=impl->live;
    (filter==SelectionFilter::TerrainComponent?live.terrainPrefilterRejections:
        filter==SelectionFilter::RefreshMargin?live.refreshMarginRejections:live.refreshMarginKept)+=count;
}
void Probe::policyExtension(PolicyExtension kind,Uint64 count) const
{
    if(impl->finished || impl->config.movementOnly)return;
    impl->live.policyExtensions.at(unsigned(kind))+=count;
}
void Probe::contentionEvent(ContentionEvent event) const
{
    if(impl->finished || impl->config.movementOnly)return;
    ++impl->live.contention.at(unsigned(event));
}
void Probe::routeWork(const RouteDiagnostics& value) const
{
    if(impl->finished || impl->config.movementOnly)return;
    auto& live=impl->live;
    live.routeCompleted+=value.completed;live.routeCanceled+=value.canceled;
    live.routeExpansions+=value.expansions;live.routeElapsedNs+=value.elapsedNs;
    live.routeRestored+=value.restored;live.routeUnobserved=value.unobserved;
    for(unsigned i=0;i<live.routeStatuses.size();++i)live.routeStatuses[i]+=value.statuses[i];
}
void Probe::routeEvent(Uint64 owner,Uint64 task,Uint32 tick,LiveEvent event) const
{
    if(impl->finished || impl->config.movementOnly)return;
    impl->expireJourneys(tick);++impl->live.events.at(unsigned(event));
    const auto at=impl->journeys.find(owner);
    if(at!=impl->journeys.end() && at->second.task==task)++at->second.events[unsigned(event)];
}
void Probe::moved(const Unit& unit,int oldX,int oldY) const
{
    if(impl->finished || (oldX==unit.posX && oldY==unit.posY))return;
    bool transport=!unit.isDead && unit.hasCapability(UnitRuntimeTraits::Transport);
    if(!transport && !unit.activeMaterialClaim)return;
    impl->expireJourneys(unit.owner->game->stepCounter);
    if(transport) {
        impl->lastTransportEventTick=unit.owner->game->stepCounter;
        if(impl->movementEndTick && *impl->movementEndTick<=*impl->lastTransportEventTick)impl->movementEndTick.reset();
    }
    auto& map=*unit.owner->map;
    if(transport && map.warpDistMax(oldX,oldY,unit.posX,unit.posY)!=1) {
        ++impl->movement.excludedNonNeighbor;transport=false;
        if(!unit.activeMaterialClaim)return;
    }
    // Movement cost needs only authoritative cell rules. Avoid warming growth
    // or area-effect caches while observing otherwise unclaimed movement.
    const auto view=map.cellView();
    const RouteProfile profile{accessClass(unit.movementAccess()),unit.owner->me,unit.gid,OccupancyPolicy::Static};
    const auto cost=routeEntryCost(view,map.coordToIndex(unit.posX,unit.posY),profile,unit.dx && unit.dy);
    if(transport) {
        auto kind=MovementKind::Other;
        if(unit.medical!=Unit::MED_FREE)kind=MovementKind::Medical;
        else if(unit.activity==Unit::ACT_FILLING && unit.displacement==Unit::DIS_GOING_TO_RESOURCE)
            kind=unit.activeMaterialClaim?MovementKind::ClaimedResource:MovementKind::UnclaimedResource;
        else if(unit.activity==Unit::ACT_FILLING && unit.displacement==Unit::DIS_GOING_TO_BUILDING)
            kind=MovementKind::ReturnToJob;
        else if(unit.activity==Unit::ACT_RANDOM)kind=MovementKind::Idle;
        const auto index=unsigned(kind);
        ++impl->movement.moves[index];impl->movement.weightedCost[index]+=cost;
        ++impl->movement.totalMoves;impl->movement.totalCost+=cost;
    }
    if(!unit.activeMaterialClaim || impl->config.movementOnly)return;
    ++impl->live.moves;impl->live.liveCost+=cost;
    const auto at=impl->journeys.find(unitOwner(unit.gid,unit.scriptIdentity));
    if(at==impl->journeys.end() || at->second.task!=unit.materialClaimTask){++impl->live.untrackedMoves;return;}
    ++at->second.steps;at->second.cost+=cost;
}

bool Probe::observesDecisionOutcomes() const { return !impl->finished && !impl->config.movementOnly; }
Probe::DecisionOutcome Probe::beginDecisionOutcome(const Unit& unit,bool pickup) const {
    DecisionOutcome out;out.preTask=unit.materialClaimTask;out.material=unit.destinationPurpose;out.pickup=pickup;
    if(unit.activeMaterialClaim)out.preAccess=unit.activeMaterialClaim->access;
    return out;
}
void Probe::selectedDecisionOutcome(DecisionOutcome& out,const Unit& unit) const {
    out.selectedTask=unit.materialClaimTask;
    if(unit.activeMaterialClaim)out.selectedAccess=unit.activeMaterialClaim->access;
}
void Probe::finishDecisionOutcome(const DecisionOutcome& out) const {
    if(!observesDecisionOutcomes())return;
    auto& counts=impl->outcomes.at(out.material)[out.pickup?1:0];++counts[Impl::Eligible];
    const bool held=out.preAccess!=UINT32_MAX,hasSelection=out.selectedAccess!=UINT32_MAX;
    if(out.attempted)++counts[Impl::Attempted];else {
        ++counts[Impl::NoAttempt];
        if(std::string_view(out.noAttempt)=="cooldown")++counts[Impl::Cooldown];
        if(std::string_view(out.noAttempt)=="missing_field")++counts[Impl::MissingField];
    }
    if(out.attempted && !out.selected)++counts[Impl::NoCandidate];
    if(hasSelection)++counts[!held?Impl::Acquired:out.selectedTask==out.preTask?Impl::HeldSame:
        out.selectedAccess==out.preAccess?Impl::RetargetDonor:Impl::RetargetAccess];
    const std::string_view route(out.route),fallback(out.fallback);
    if(route=="ready")++counts[Impl::Ready];else if(route=="pending")++counts[Impl::Pending];
    else if(route=="failed")++counts[Impl::Failed];else if(route=="arrived")++counts[Impl::Arrived];
    if(fallback=="wait")++counts[Impl::Wait];else if(fallback=="roam")++counts[Impl::Roam];else if(fallback=="release_job")++counts[Impl::Release];
    if(std::string_view(out.fallbackReason)=="cooldown")++counts[Impl::CooldownWait];
}
}
