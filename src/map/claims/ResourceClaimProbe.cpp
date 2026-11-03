// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceClaimProbe.h"
#include "ResourceClaimOracle.h"
#include "ResourceClaimDeepEvent.h"
#include "ClaimPolicyConfig.h"
#include "ResourceClaimPatternEvaluation.h"
#include "ResourceClaimPilotPatterns.h"
#include "ResourceClaimRouteExperiment.h"
#include "ClaimRoutePipeline.h"
#include "Game.h"
#include "Map.h"
#include "Unit.h"
#include "Building.h"
#include "Team.h"
#include "Utilities.h"
#include "engine/sim/snapshot/SnapshotStorage.h"
#include <nlohmann/json.hpp>
#include <zlib.h>
#include <chrono>
#include <deque>
#include <map>
#include <filesystem>
#include <limits>
#include <stdexcept>

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
Json distanceJson(Uint64 cost) { return cost==Oracle::Unreachable ? Json(nullptr) : Json(cost); }
}
struct Probe::Impl
{
    struct Work {
        SimulationSnapshot::Handle world;
        std::vector<std::pair<Owner,Claim>> owners;
        std::vector<Uint16> field;
        std::shared_ptr<const gradient_kernel::OwnedDestinations> destinations;
        bool deepSelected=false;
        Uint64 deepPriority=0;
        Json deepContext;
        std::vector<Uint8> deepBytes;
        bool metadataEnabled=false;
        bool claimsEnforced=false,experimental=false,goalRefresh=false;
        Json policyConfig;std::string policyDigest;
        Uint32 incomingAttackers=0,outgoingEnemies=0,observerVisibility=0;
        const char* reason="fully_claimed";
        Oracle::Query query;
        Uint32 tick=0, seed=0, nearest=0, capacity=0, claimed=0;
        Uint64 ordinal=0;
        bool pickup=false;
        Json features;
        std::string line;
        ComputeExecutor::Batch batch;
        std::exception_ptr error;
    };
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
    };
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
            {"policy_digest",j.policyDigest}}.dump()+"\n");
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
            {"movement_availability","actual_moves and actual_weighted_cost count committed moves only while a claim is held; disabled or shadow claims do not observe ordinary pursuit movement; zero is not zero total travel"},
            {"inherited_claim_coverage","held-claim moves counted from segment start; acquisition baseline and full journey unavailable until a new sampled acquisition"},
            {"journey_eligible",live.journeyEligible},{"journey_sampled",live.journeySampled},{"journey_admitted",live.journeyAdmitted},
            {"journey_completed",live.journeyCompleted},{"excluded_capacity",live.excludedCapacity},{"excluded_limit",live.excludedLimit},
            {"untracked_claim_moves",live.untrackedMoves},{"sample_every",config.sampleEvery},
            {"route_completed",live.routeCompleted},{"route_canceled",live.routeCanceled},{"route_expansions",live.routeExpansions},
            {"route_worker_elapsed_ns",live.routeElapsedNs},{"route_unobserved_work",live.routeUnobserved},{"route_restored_outputs",live.routeRestored},
            {"route_status_counts",live.routeStatuses},{"route_status_names",Json::array({"found","unreachable","cost_limit","expansion_limit"})},
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
    std::array<Uint64,MaterialCount> materialDecisions{}, materialSampled{}, materialContested{};
    Uint64 sampledInfinite=0, sampledPooled=0, sampledExhaustedPools=0;
    Uint64 staleScalarIneligible=0, staleMixedFieldIneligible=0, staleInvalidXY=0;
    std::string path;
    gzFile file=nullptr;
    SimulationSnapshot::Storage storage;
    std::deque<std::unique_ptr<Work>> pending;
    bool finished=false;
    Json deepContext;
    Experiment::DeepReservoir deep;
    Uint64 deepIneligibleNoXY=0;
    std::string deepDigest;
    std::optional<ClaimPolicyConfig> cachedPolicy;
    Json cachedPolicyJson;
    std::string cachedPolicyDigest;
    std::filesystem::path deepDirectory;

    void deepManifest(bool complete)
    {
        if(config.deepContext.empty())return;
        const bool censored=deep.censored();
        const auto value=Json{{"version",1},{"complete",complete},{"censored",bool(censored)},{"design",Experiment::DeepDesign},
            {"eligible",deep.eligible},{"excluded_eligible_admission",deep.excludedSampleCap+deep.excludedQueueCap},{"excluded_eligible_sample_cap",deep.excludedSampleCap},{"excluded_eligible_queue_cap",deep.excludedQueueCap},{"ineligible_contested_no_xy",deepIneligibleNoXY},{"ordinary_missing_field",metrics.excludedMissingField},{"priority",deep.priority},{"ordinal",deep.ordinal},{"captured_ordinal",deep.writtenOrdinal},
            {"replacements",deep.replacements},{"file_sha256",deepDigest},{"context",deepContext}}.dump();
        Experiment::replaceDeepFile(deepDirectory/"manifest.json",std::span(reinterpret_cast<const Uint8*>(value.data()),value.size()));
    }

    Impl(ComputeExecutor& executor,std::string path,Config config):executor(executor),config(config),path(std::move(path))
    {
        if (!config.sampleEvery || !config.maxSamples || !config.maxOutputBytes || !config.maxPending || config.maxPending>8)
            throw std::invalid_argument("invalid resource claim probe limits");
        if (std::filesystem::exists(this->path)) throw std::runtime_error("resource claim probe output already exists");
        if (std::filesystem::space(std::filesystem::path(this->path).parent_path()).available<16ull*1024*1024)
            throw std::runtime_error("insufficient free space for resource claim probe");
        if(!config.deepContext.empty()) {
            deepContext=Json::parse(config.deepContext);
            for(const auto* key:{"source_revision","bundle_id","cohort_id","game_id"})
                if(!deepContext.contains(key) || !deepContext[key].is_string() || deepContext[key].get<std::string>().empty() || deepContext[key].get<std::string>().size()>256)
                    throw std::invalid_argument("deep context requires bounded source/bundle/cohort/game identities");
            deepDirectory=std::filesystem::path(this->path).parent_path()/"resource-claims-deep";
            if(!std::filesystem::create_directory(deepDirectory))throw std::runtime_error("deep output already exists");
            deepManifest(false);
        }
        file=gzopen(this->path.c_str(),"wb6");
        if (!file) throw std::runtime_error("cannot open resource claim probe output");
    }
    ~Impl()
    {
        // Explicit finish reports errors; teardown still waits for every borrower.
        for (auto& work:pending) { try { executor.join(work->batch,{JoinReason::Teardown,ComputeTelemetry::NoTick}); } catch (...) {} }
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
    }
    static void run(void* context,std::size_t) noexcept
    {
        auto& work=*static_cast<Work*>(context);
        try {
            const auto start=std::chrono::steady_clock::now();
            const auto oracle=Oracle::evaluate(work.world,work.query,work.owners);
            const auto oracleNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            const auto view=work.world.view();
            // The immutable XY lease and copied cost plane come from the same
            // publication. Older/warming fields explicitly use scalar fallback.
            const auto destination=[&](Uint32 cell) {
                if(work.destinations) {
                    const auto x=work.destinations->x[cell],y=work.destinations->y[cell];
                    if(x>=view.width || y>=view.height)return Oracle::NoTile;
                    return Uint32(y*view.width+x);
                }
                Uint16 here=work.field[cell];
                if (here<=GRADIENT_UNREACHABLE) return Oracle::NoTile;
                while (here!=GRADIENT_AT_GOAL) {
                    const int x=int(cell%view.width),y=int(cell/view.width);
                    int best=-1; Uint32 selected=Oracle::NoTile;
                    for (int d=0;d<8;++d) {
                        const int dx=tabClose[d][0],dy=tabClose[d][1];
                        const auto next=Uint32(view.index(x+dx,y+dy));
                        if (work.field[next]<=here) continue;
                        const int score=int(work.field[next])-int(routeEntryCost(view,next,work.query.route,dx&&dy));
                        if (score>best) { best=score; selected=next; }
                    }
                    if (selected==Oracle::NoTile) break;
                    cell=selected; here=work.field[cell];
                }
                return cell;
            };
            const auto exposureStart=std::chrono::steady_clock::now();
            std::unique_ptr<Experiment::ExposureProxy> exposure;
            if(work.deepSelected)exposure=std::make_unique<Experiment::ExposureProxy>(view.width,view.height,work.world.occupancy->cells,work.world.visibility->visible,
                work.incomingAttackers,work.observerVisibility,unsigned(Unit::MAX_COUNT),unsigned(Building::MAX_COUNT));
            const auto exposureNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-exposureStart).count();
            Experiment::PatternEvaluator evaluator(oracle,view.width,view.height,work.query.origin,work.nearest);
            Json outcomes=Json::array();
            std::map<std::tuple<Uint32,Uint32,Uint32>,Experiment::PatternOutcome> uniqueRoutes;
            unsigned patternIndex=0;
            for (const auto& pattern:Experiment::pilotPatterns()) {
                for (unsigned policy=0;policy<2;++policy) {
                    const auto outcome=evaluator.evaluate(pattern.viewForDirection(evaluator.closestOctant()),policy?Experiment::PatternSelection::ClosestGeometric:Experiment::PatternSelection::FirstAvailable,destination);
                    if(!outcome.fallback)uniqueRoutes.try_emplace(std::tuple{outcome.source,outcome.access,outcome.probe},outcome);
                    outcomes.push_back({patternIndex,policy,outcome.source,outcome.access,outcome.probe,distanceJson(outcome.distance),outcome.probes,outcome.destinationReads,outcome.fallback,distanceJson(outcome.accessDistance)});
                }
                ++patternIndex;
            }
            // Hash-rank distinct routes, independent of pattern enumeration order.
            // This is an explicit bounded subset, not a per-pattern performance census.
            std::vector<std::pair<Uint64,Experiment::PatternOutcome>> ranked;
            for(const auto& [key,outcome]:uniqueRoutes) {
                const auto hash=mix(mix(work.ordinal ^ (Uint64(work.seed)<<32)) ^ mix(outcome.source) ^
                    mix(Uint64(outcome.access)+0x100000000ull) ^ mix(Uint64(outcome.probe)+0x200000000ull));
                ranked.emplace_back(hash,outcome);
            }
            std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){
                return std::tie(a.first,a.second.source,a.second.access,a.second.probe)<
                    std::tie(b.first,b.second.source,b.second.access,b.second.probe);});
            Experiment::StaticRoutes routes(view,work.query,oracle,work.field,exposure.get());
            Json routeRows=Json::array();
            const auto measurement=[&](const Experiment::RouteMeasurement& r) {
                Json proxy=nullptr;
                if(exposure && r.exposure.endpoint!=UINT32_MAX)proxy={{"complete",std::string_view(r.status)=="reached"},{"endpoint",r.exposure.endpoint},{"endpoint_distance",exposure->at(r.exposure.endpoint)},
                    {"entered_steps",r.exposure.steps},{"entered_cost",r.exposure.cost},{"min_entered_distance",r.exposure.minimum},
                    {"near_steps",r.exposure.nearSteps},{"near_cost",r.exposure.nearCost}};
                return Json{{"status",r.status},{"cost",distanceJson(r.cost)},{"expansions",r.expansions},
                    {"elapsed_ns",r.ns},{"continuation_steps",r.continuationSteps},{"exposure_proxy",std::move(proxy)}};
            };
            const auto routeStart=std::chrono::steady_clock::now();
            for(std::size_t i=0;i<std::min(ranked.size(),std::size_t(Experiment::StaticRoutes::OutcomeLimit));++i) {
                const auto& outcome=ranked[i].second;
                routeRows.push_back({{"source",outcome.source},{"access",outcome.access},{"probe",outcome.probe},
                    {"source_optimal_cost",distanceJson(outcome.distance)},{"access_optimal_cost",distanceJson(outcome.accessDistance)},
                    {"direct_source_component",measurement(routes.direct(outcome.source))},
                    {"via_probe_selected_access",measurement(routes.via(outcome.access,outcome.probe))}});
            }
            const auto routeNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-routeStart).count();
            Json accessExposure=nullptr;std::set<Uint32> measuredAccess;
            if(exposure) {
                accessExposure=Json::array();
                for(const auto& [key,outcome]:uniqueRoutes)if(measuredAccess.insert(outcome.access).second)
                    accessExposure.push_back({outcome.access,exposure->at(outcome.access)});
            }
            Json optimalExposure=nullptr;
            if(exposure && oracle.best) {
                const auto& candidate=oracle.candidates[*oracle.best];
                optimalExposure={{"access",candidate.access},{"access_distance",exposure->at(candidate.access)},
                    {"stance",candidate.stance},{"stance_distance",exposure->at(candidate.stance)}};
            }
            Json best=nullptr;
            if (oracle.best) {
                const auto& chosen=oracle.candidates[*oracle.best];
                best={{"source",chosen.source},{"access",chosen.access},{"stance",chosen.stance},{"distance",chosen.distance}};
            }
            Json exposureMetadata=nullptr;
            if(exposure)exposureMetadata={{"incoming_attackable_team_mask",work.incomingAttackers},{"outgoing_enemy_team_mask",work.outgoingEnemies},
                {"observer_visibility_mask",work.observerVisibility},{"seed_cells",exposure->seedCells()},
                {"origin_distance",exposure->at(work.query.origin)},{"nearest_access_distance",exposure->at(work.nearest)},
                {"build_elapsed_ns",exposureNs}};
            Json row={{"kind","decision"},{"claims_enforced",work.claimsEnforced},{"active_policy",work.experimental?"experimental":work.claimsEnforced?"prototype-stationary-v1":"disabled"},
                {"policy_config",work.policyConfig},{"policy_digest",work.policyDigest},
                {"decision_kind",work.goalRefresh?"goal_refresh":work.claimsEnforced?"new_claim_acquisition":"legacy_resource_decision"},{"reason",work.reason},{"phase",work.pickup?"pickup":"movement"},{"tick",work.tick},{"ordinal",work.ordinal},{"seed",work.seed},
                {"width",view.width},{"height",view.height},{"owner",work.query.owner},{"gid",work.query.route.unitGid},{"team_mask",work.query.route.teamMask},
                {"movement",work.query.route.movement},{"material",work.query.material},{"origin",work.query.origin},
                {"nearest",work.nearest},{"capacity",work.capacity},{"claimed",work.claimed},
                {"destination_lookup",work.destinations?"published-xy":"weighted-scalar-projection"},
                {"material_destinations_enabled",work.metadataEnabled},{"destination_xy_missing",!work.destinations},
                {"exposure_missing",!exposure},{"exposure_proxy",std::move(exposureMetadata)},
                {"access_exposure",std::move(accessExposure)},{"optimal_exposure",std::move(optimalExposure)},
                {"occupancy","static"},{"nearest_octant",evaluator.closestOctant()},{"oracle_expanded",oracle.expanded},{"oracle_elapsed_ns",oracleNs},
                {"static_routes",std::move(routeRows)},{"static_route_unique",ranked.size()},
                {"static_route_excluded_limit",ranked.size()>Experiment::StaticRoutes::OutcomeLimit?ranked.size()-Experiment::StaticRoutes::OutcomeLimit:0},
                {"static_route_elapsed_ns",routeNs},{"source_count",oracle.candidates.size()},{"optimal",best},{"features",work.features},{"patterns",std::move(outcomes)}};
            if(work.deepSelected) {
                auto metadata=row;
                metadata.erase("static_routes");
                metadata.erase("access_exposure"); // Compact trace holds selected-access proxies; full offline risk needs a future wire revision.
                metadata.erase("features"); // Feature row remains in ordinary trace, joined by ordinal.
                metadata["game_id"]=work.deepContext.at("game_id");
                metadata["event_id"]=std::to_string(work.seed)+":"+std::to_string(work.ordinal);
                metadata["priority"]=work.deepPriority;
                metadata["optimum"]=oracle.best?distanceJson(oracle.candidates[*oracle.best].distance):Json(nullptr);
                auto provenance=work.deepContext;provenance.erase("game_id");
                provenance["metadata_mode"]="published-xy";provenance["censored"]=false;
                metadata["provenance"]=std::move(provenance);
                std::vector<Uint32> plane(view.width*view.height);
                for(Uint32 tile=0;tile<plane.size();++tile)plane[tile]=destination(tile);
                work.deepBytes=Experiment::encodeDeepEvent(metadata,plane,oracle.candidates);
            }
            work.line=row.dump()+"\n";
        } catch (...) { work.error=std::current_exception(); }
        work.world={}; work.owners.clear(); work.field.clear(); work.destinations.reset();
    }
    void publish(Uint64 tick,bool all=false)
    {
        while (!pending.empty() && (all || Uint64(pending.front()->tick)+1<=tick)) {
            auto& work=*pending.front();
            executor.join(work.batch,{all?JoinReason::Save:JoinReason::TickDeadline,Uint32(tick)});
            if(work.error) std::rethrow_exception(work.error);
            write(work.line);
            if(work.deepSelected) {
                Experiment::replaceDeepFile(deepDirectory/"event.bin",work.deepBytes);
                deepDigest=Online::Sha256::toHex(Online::Sha256::of(work.deepBytes));
                deep.written(work.ordinal);
            }
            ++metrics.completed; pending.pop_front();
        }
    }
};

Probe::Probe(ComputeExecutor& executor,std::string path,Config config):impl(std::make_unique<Impl>(executor,std::move(path),config))
{
    Json patterns=Json::array();
    for (const auto& pattern:Experiment::pilotPatterns()) patterns.push_back(pattern.name);
    impl->write(Json{{"kind","schema"},{"version",1},{"timing_clock","worker-steady-clock-elapsed"},{"cohort","sampled-material-movement-and-pickup-decisions-before-choice"},
        {"deep_capture",config.deepContext.empty()?Json(nullptr):Json{{"design",Experiment::DeepDesign},{"context",impl->deepContext},{"record_limit",Experiment::DeepRecordLimit},{"directory_limit",Experiment::DeepDirectoryLimit},{"free_reserve",Experiment::DeepFreeReserve}}},
        {"sample_every",config.sampleEvery},{"max_samples",config.maxSamples},{"max_pending",config.maxPending},
        {"movement_audit_version",1},
        {"movement_only",config.movementOnly},{"decision_sampling_enabled",!config.movementOnly},
        {"live_journey_version",1},{"live_event_names",Json::array({"acquired","released","retargeted","harvest_begin","harvested","harvest_failed",
            "goal_refresh_same","goal_refresh_changed","goal_refresh_failed","fallback_wait","fallback_roam","fallback_release",
            "route_initial","route_refresh","route_repair","route_probe_fallback","pending_path","traffic_wait","geometry_contradiction",
            "reanchor_joined","reanchor_failed","probe_tail_failed","obsolete_probe"})},
        {"max_output_bytes",config.maxOutputBytes},{"claims_enforced",nullptr},{"mode_source","decision.claims_enforced and decision.active_policy"},{"destination_lookup","per-event-published-xy-or-scalar-fallback"},
        {"continuation_policy","diagnostic-ordinal-resets-after-load"},{"patterns",patterns},
        {"new_destination_reads_semantics","marginal shared event-cache misses in pattern order; instrumentation amortization only, not standalone policy cost"},
        {"stale_goal_eligibility","published XY from natural-only field; failed scalar projection and mixed supplier fields excluded"},
        {"outcome_columns",{"pattern","policy","source","access","probe","weighted_cost","probes","new_destination_reads","fallback","access_weighted_cost"}},
        {"exposure_proxy_contract",{{"version",1},{"selection","deep minhash reservoir replacements only; use final per-game minimum for unbiased eligible-event comparisons"},{"meaning","coarse hostile occupied-cell proximity, not combat risk"},
            {"distance","toroidal Chebyshev, capped at16;17 means beyond cap or no seed"},
            {"planes",{"units_omniscient","buildings_omniscient","units_fog_visible","buildings_fog_visible"}},
            {"visibility","observer team bit; fog plane already incorporates sharing; entity cloaking not modeled"},
            {"bands",{1,4,8}},{"fractions","near_steps/entered_steps and near_cost/entered_cost; zero denominators unavailable"},
            {"path_scope","actual reconstructed entered cells; failed continuation is partial; no-path null"}}},
        {"static_route_limits",{{"unique_outcomes",Experiment::StaticRoutes::OutcomeLimit},{"expansions_per_search",Experiment::StaticRoutes::ExpansionLimit},
            {"continuation_steps",Experiment::StaticRoutes::ContinuationLimit},{"selection","diagnostic_hash_without_replacement"},
            {"heuristic","minimum_rule_entry_chebyshev; zero_for_farm_goal_sets"}}},
        {"policies",{"first_available","closest_geometric"}}}.dump()+"\n");
}
Probe::~Probe()=default;
const Probe::Metrics& Probe::metrics() const {return impl->metrics;}
void Probe::publish(Uint32 tick) {
    impl->publish(tick);
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
    impl->publish(UINT32_MAX,true);
    impl->publishBaselines(UINT32_MAX,true);
    while(!impl->journeys.empty())impl->closeJourney(impl->journeys.begin(),"end_of_segment",impl->latestLiveTick);
    const auto& m=impl->metrics;
    impl->write(Json{{"kind","summary"},{"complete",true},{"decisions",m.decisions},{"sampled",m.sampled},
        {"contested",m.contested},{"submitted",m.submitted},{"completed",m.completed},
        {"excluded_sample_limit",m.excludedSampleLimit},{"excluded_queue_limit",m.excludedQueueLimit},{"excluded_missing_field",m.excludedMissingField},
        {"material_decisions",impl->materialDecisions},{"material_sampled",impl->materialSampled},
        {"material_contested",impl->materialContested},{"sampled_infinite",impl->sampledInfinite},
        {"sampled_pooled",impl->sampledPooled},{"sampled_exhausted_pools",impl->sampledExhaustedPools},
        {"stale_scalar_ineligible",impl->staleScalarIneligible},{"stale_mixed_field_ineligible",impl->staleMixedFieldIneligible},
        {"stale_invalid_xy",impl->staleInvalidXY},{"capture_bytes",m.captureBytes},{"live",impl->liveSummary()},
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
    impl->deepManifest(true);
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
                const auto canonical=config.canonicalJson();impl->cachedPolicy=config;
                impl->cachedPolicyJson=Json::parse(canonical);impl->cachedPolicyDigest=Online::Sha256::hex(canonical);
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
        baseline->profile={unit.movementClass(),unit.owner->me,unit.gid,OccupancyPolicy::Static};
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
    else if(event==LiveEvent::Released && !at->second.harvesting)impl->closeJourney(at,"released",tick);
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
    const RouteProfile profile{unit.movementClass(),unit.owner->me,unit.gid,OccupancyPolicy::Static};
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

void Probe::decision(Game& game,const Unit& unit,const Uint16* field,Uint32 pickupAccess)
{
    if(impl->finished || impl->config.movementOnly)return;
    auto& metrics=impl->metrics;
    const Uint64 ordinal=++metrics.decisions;
    ++impl->materialDecisions.at(unit.destinationPurpose);
    const auto seed=game.gameHeader.getRandomSeed();
    if(mix(mix(seed)^mix(game.stepCounter)^mix(unitOwner(unit.gid,unit.scriptIdentity))^mix(ordinal))%impl->config.sampleEvery)return;
    ++metrics.sampled;
    ++impl->materialSampled.at(unit.destinationPurpose);
    if(!field){++metrics.excludedMissingField;return;}
    auto& map=game.map;
    map.reconcileMaterialClaims();
    Sint32 x=0,y=0;
    bool staleGoal=false;
    const auto destinations=map.materialGradientDestinations(field);
    if(destinations && destinations->cells!=map.cellCount())
        throw std::logic_error("claim probe destination publication shape mismatch");
    if(pickupAccess!=UINT32_MAX) { x=int(pickupAccess%map.getW()); y=int(pickupAccess/map.getW()); }
    else if(!map.projectMaterialDestination(field,unit.owner->teamNumber,unit.destinationPurpose,unit.movementClass(),unit.posX,unit.posY,&x,&y,
        unit.attachedBuilding && unit.attachedBuilding->fetchesFromMarkets(),unit.attachedBuilding)) {
        if(!destinations){++impl->staleScalarIneligible;return;}
        if(field!=map.lookupMaterialGradientSlot(unit.owner->teamNumber,unit.destinationPurpose,unit.movementClass(),false,nullptr)) {
            ++impl->staleMixedFieldIneligible;return;
        }
        const auto origin=map.coordToIndex(unit.posX,unit.posY);
        x=destinations->x[origin]; y=destinations->y[origin];
        if(x>=map.getW() || y>=map.getH()) {++impl->staleInvalidXY;return;}
        staleGoal=true;
    }
    const auto access=map.coordToIndex(x,y);
    if(!staleGoal && !map.materialAmountAtSlot(access,unit.destinationPurpose))return; // Supplier cohort.
    auto source=access;
    const bool accessResource=map.resourceRegistry().valid(static_cast<ResourceId>(map.getResource(access).type));
    const auto accessYield=accessResource?map.resourceRegistry().yields(static_cast<ResourceId>(map.getResource(access).type))[unit.destinationPurpose]:YieldProperties{};
    const bool exclusive=accessResource && (accessYield.consumption==ResourceConsumption::All || accessYield.destroysDeposit);
    const bool pooled=!staleGoal && !exclusive && map.farmAreasEnabled() && map.isFarmArea(x,y,unit.owner->me) && map.isFarmableResourceByIndex(map.getResource(access).type);
    bool exhaustedPool=false;
    if(pooled) {
        const auto selected=map.pickClaimFarmHarvestTileSlot(unit.posX,unit.posY,unit.destinationPurpose,unit.owner->me,
            pickupAccess!=UINT32_MAX?std::nullopt:std::optional<size_t>(access));
        if(selected) source=*selected;
        else exhaustedPool=true; // The seed-only field is itself a contested failure case.
    }
    const bool hasResource=map.resourceRegistry().valid(static_cast<ResourceId>(map.getResource(source).type));
    const auto yield=hasResource?map.resourceRegistry().yields(static_cast<ResourceId>(map.getResource(source).type))[unit.destinationPurpose]:YieldProperties{};
    const Uint32 amount=map.materialAmountAtSlot(source,unit.destinationPurpose);
    const Uint32 reserve=pooled && yield.consumption!=ResourceConsumption::Infinite && !game.gameHeader.isResourceGrowthDisabled()?yield.seedReserve:0;
    const Uint32 capacity=!staleGoal && amount>reserve?amount-reserve:0;
    const Claim desired{Uint32(source),Uint32(access),Uint8(unit.destinationPurpose),map.getResource(source).type,exclusive,Uint16(reserve)};
    impl->sampledInfinite+=yield.consumption==ResourceConsumption::Infinite;
    impl->sampledPooled+=pooled; impl->sampledExhaustedPools+=exhaustedPool;
    if(!staleGoal && map.materialClaimLedger().available(unitOwner(unit.gid,unit.scriptIdentity),desired,capacity,yield.consumption==ResourceConsumption::Infinite))return;
    ++metrics.contested;
    ++impl->materialContested.at(unit.destinationPurpose);
    bool deepSelected=false;
    Uint64 deepPriority=0;
    if(!impl->config.deepContext.empty() && destinations) {
        deepPriority=mix(0xD33FC1A17A93B561ull ^ mix(seed) ^ mix(game.stepCounter) ^ mix(unitOwner(unit.gid,unit.scriptIdentity)) ^ mix(ordinal));
        deepSelected=impl->deep.consider(deepPriority,ordinal);
    }
    if(!impl->config.deepContext.empty() && !destinations)++impl->deepIneligibleNoXY;
    if(metrics.submitted>=impl->config.maxSamples){++metrics.excludedSampleLimit;if(!impl->config.deepContext.empty() && destinations)impl->deep.excluded(false);return;}
    if(impl->pending.size()>=impl->config.maxPending){++metrics.excludedQueueLimit;if(!impl->config.deepContext.empty() && destinations)impl->deep.excluded(true);return;}
    auto work=std::make_unique<Impl::Work>();
    work->deepSelected=deepSelected;work->deepPriority=deepPriority;
    if(deepSelected)work->deepContext=impl->deepContext;
    using C=SimulationSnapshot::Component;
    const auto mask=SimulationSnapshot::bit(C::Catalogs)|SimulationSnapshot::bit(C::Terrain)|SimulationSnapshot::bit(C::Resources)
        |SimulationSnapshot::bit(C::Occupancy)|SimulationSnapshot::bit(C::Areas)|SimulationSnapshot::bit(C::Visibility)|SimulationSnapshot::bit(C::Rules);
    const auto bytes=impl->storage.bytesCopied;
    // Private exact-event capture: never reuse/invalidate the game's completed observation.
    work->world=SimulationSnapshot::capture(game,SimulationSnapshot::captureCatalog(game),mask,nullptr,&impl->storage,false);
    metrics.captureBytes+=impl->storage.bytesCopied-bytes;
    work->owners=map.materialClaimLedger().entries();
    work->field.assign(field,field+map.cellCount());
    work->destinations=destinations;
    work->reason=staleGoal?"stale_goal":exhaustedPool?"farm_reserve":"fully_claimed";
    work->metadataEnabled=map.materialDestinationsEnabled();
    work->claimsEnforced=map.activeClaimsEnabled();
    work->experimental=map.materialClaimPolicy()==ActiveClaimPolicy::Experimental;
    work->goalRefresh=work->experimental && unit.activeMaterialClaim.has_value();
    if(work->experimental) {
        const auto& config=map.materialClaimPolicyConfig();
        if(!impl->cachedPolicy || *impl->cachedPolicy!=config) {
            const auto canonical=config.canonicalJson();
            impl->cachedPolicy=config;impl->cachedPolicyJson=Json::parse(canonical);
            impl->cachedPolicyDigest=Online::Sha256::hex(canonical);
        }
        work->policyConfig=impl->cachedPolicyJson;work->policyDigest=impl->cachedPolicyDigest;
    }
    if(work->destinations && work->destinations->cells!=map.cellCount())
        throw std::logic_error("claim probe destination publication shape mismatch");
    work->query={unitOwner(unit.gid,unit.scriptIdentity),Uint32(map.coordToIndex(unit.posX,unit.posY)),Uint8(unit.destinationPurpose),
        {unit.movementClass(),unit.owner->me,unit.gid,OccupancyPolicy::Static}};
    if(work->deepSelected) {
        work->outgoingEnemies=unit.owner->enemies;work->observerVisibility=unit.owner->me;
        for(int team=0;team<game.teamsCount();++team)if(game.teams[team] && (game.teams[team]->attackableTeams()&unit.owner->me))
            work->incomingAttackers|=game.teams[team]->me;
    }
    work->pickup=pickupAccess!=UINT32_MAX;
    work->tick=game.stepCounter; work->seed=seed; work->ordinal=ordinal; work->nearest=Uint32(access);
    work->capacity=capacity; work->claimed=map.materialClaimLedger().materialCount(Uint32(source),unit.destinationPurpose);
    Json fieldKey=nullptr, fieldGeneration=nullptr;
    for (const auto& plane:map.publishedResourceFields()) if (*plane.slot==field) {
        fieldKey=plane.key; fieldGeneration=plane.generation; break;
    }
    const auto properties=hasResource?map.resourcePropertiesByIndex(map.getResource(source).type):ResourceProperties{};
    unsigned forbiddenNeighbors=0,forbiddenMaterialNeighbors=0,growingForbiddenMaterialNeighbors=0;
    Uint32 forbiddenMaterialStock=0,growingForbiddenMaterialStock=0;
    for (int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        if(!map.isForbidden(x+dx,y+dy,unit.owner->me))continue;
        ++forbiddenNeighbors;
        const auto neighbor=map.coordToIndex(x+dx,y+dy);
        const auto stock=map.materialAmountAtSlot(neighbor,unit.destinationPurpose);
        if(!stock)continue;
        ++forbiddenMaterialNeighbors;forbiddenMaterialStock+=stock;
        const auto resource=static_cast<ResourceId>(map.getResource(neighbor).type);
        const auto& traits=map.resourceRegistry().properties(resource);
        const auto& materialYield=map.resourceRegistry().yields(resource)[unit.destinationPurpose];
        // Configured regrowth potential, not a prediction of habitat/fertility
        // success or proof that an AI intentionally reserved this donor.
        if(!game.gameHeader.isResourceGrowthDisabled() && traits.ecology!=ResourceEcology::None &&
            traits.growthRate && materialYield.growthRate) {
            ++growingForbiddenMaterialNeighbors;growingForbiddenMaterialStock+=stock;
        }
    }
    Uint32 effectiveReserve=reserve;
    for(const auto claimant:map.materialClaimLedger().ownersAtSource(Uint32(source))) {
        if(claimant==unitOwner(unit.gid,unit.scriptIdentity))continue;
        const auto* existing=map.materialClaimLedger().find(claimant);
        if(existing && existing->material==unit.destinationPurpose)effectiveReserve=std::max<Uint32>(effectiveReserve,existing->reserve);
    }
    work->features={{"resource_material_mask",hasResource?Json(properties.materialMask):Json(nullptr)},{"farm_areas_enabled",map.farmAreasEnabled()},
        {"nearby_forbidden_count",forbiddenNeighbors},
        {"nearby_forbidden_material_count",forbiddenMaterialNeighbors},{"nearby_forbidden_material_stock",forbiddenMaterialStock},
        {"nearby_forbidden_growing_material_count",growingForbiddenMaterialNeighbors},{"nearby_forbidden_growing_material_stock",growingForbiddenMaterialStock},
        {"resource_persists_when_empty",hasResource?Json(properties.persistsWhenEmpty):Json(nullptr)},
        {"resource_growth_rate",hasResource?Json(properties.growthRate):Json(nullptr)},{"team_units",unit.owner->liveUnits.size()},{"team_buildings",unit.owner->liveBuildings.size()},
        {"colony_count",game.teamsCount()},{"unit_type",unit.typeNum},{"hp",unit.hp},{"max_hp",unit.performance[HP]},
        {"hunger",unit.hungry},{"food_capacity",unit.foodCapacity()},{"medical",int(unit.medical)},
        {"activity",int(unit.activity)},{"displacement",int(unit.displacement)},{"job_purpose",int(unit.jobPurpose)},
        {"cargo_packets",unit.carriedPacketCount()},{"carried_material",unit.carriedMaterial},
        {"harvest_level",unit.level[HARVEST]},{"walk_speed",unit.performance[WALK]},
        {"swim_speed",unit.performance[SWIM]},{"fly_speed",unit.performance[FLY]},
        {"pooled_farm",pooled},{"exhausted_pool",exhaustedPool},{"source",source},{"resource_type",hasResource?Json(map.getResource(source).type):Json(nullptr)},
        {"yield_consumption",hasResource?Json(int(yield.consumption)):Json(nullptr)},{"infinite",hasResource?Json(yield.consumption==ResourceConsumption::Infinite):Json(nullptr)},
        {"destroys_deposit",hasResource?Json(yield.destroysDeposit):Json(nullptr)},{"stock_amount",amount},{"stock_reserve",reserve},{"effective_seed_floor",effectiveReserve},
        {"effective_capacity",!staleGoal && amount>effectiveReserve?amount-effectiveReserve:0},
        {"origin_field_value",field[work->query.origin]},{"nearest_field_value",field[access]},
        {"field_key",fieldKey},{"field_generation",fieldGeneration},
        {"consumer_gid",unit.attachedBuilding?Json(unit.attachedBuilding->gid):Json(nullptr)},
        {"consumer_identity",unit.attachedBuilding?Json(unit.attachedBuilding->scriptIdentity):Json(nullptr)}};
    auto* raw=work.get(); impl->pending.push_back(std::move(work));
    const ComputeExecutor::Group group{1,{Impl::run,raw},ComputeExecutor::NoLane};
    try { raw->batch=impl->executor.submit(std::span(&group,1),ComputeExecutor::boundaryDue(Uint64(game.stepCounter)+1),
        {ComputeProducer::Other,game.stepCounter,Uint64(game.stepCounter)+1}); }
    catch(...) {impl->pending.pop_back();throw;}
    ++metrics.submitted;
}
}
