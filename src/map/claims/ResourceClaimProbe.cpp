// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceClaimProbe.h"
#include "ResourceClaimOracle.h"
#include "ResourceClaimPatternEvaluation.h"
#include "ResourceClaimPilotPatterns.h"
#include "ResourceClaimRouteExperiment.h"
#include "Game.h"
#include "Map.h"
#include "Unit.h"
#include "Utilities.h"
#include "engine/sim/snapshot/SnapshotStorage.h"
#include <nlohmann/json.hpp>
#include <zlib.h>
#include <chrono>
#include <deque>
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
        bool metadataEnabled=false;
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

    Impl(ComputeExecutor& executor,std::string path,Config config):executor(executor),config(config),path(std::move(path))
    {
        if (!config.sampleEvery || !config.maxSamples || !config.maxOutputBytes || !config.maxPending || config.maxPending>8)
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
        for (auto& work:pending) { try { executor.join(work->batch,{JoinReason::Teardown,ComputeTelemetry::NoTick}); } catch (...) {} }
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
            Experiment::StaticRoutes routes(view,work.query,oracle,work.field);
            Json routeRows=Json::array();
            const auto measurement=[](const Experiment::RouteMeasurement& r) {
                return Json{{"status",r.status},{"cost",distanceJson(r.cost)},{"expansions",r.expansions},
                    {"cpu_ns",r.ns},{"continuation_steps",r.continuationSteps}};
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
            Json best=nullptr;
            if (oracle.best) {
                const auto& chosen=oracle.candidates[*oracle.best];
                best={{"source",chosen.source},{"access",chosen.access},{"stance",chosen.stance},{"distance",chosen.distance}};
            }
            Json row={{"kind","decision"},{"reason",work.reason},{"phase",work.pickup?"pickup":"movement"},{"tick",work.tick},{"ordinal",work.ordinal},{"seed",work.seed},
                {"width",view.width},{"height",view.height},{"owner",work.query.owner},{"gid",work.query.route.unitGid},{"team_mask",work.query.route.teamMask},
                {"movement",work.query.route.movement},{"material",work.query.material},{"origin",work.query.origin},
                {"nearest",work.nearest},{"capacity",work.capacity},{"claimed",work.claimed},
                {"destination_lookup",work.destinations?"published-xy":"weighted-scalar-projection"},
                {"material_destinations_enabled",work.metadataEnabled},{"destination_xy_missing",!work.destinations},
                {"occupancy","static"},{"nearest_octant",evaluator.closestOctant()},{"oracle_expanded",oracle.expanded},{"oracle_ns",oracleNs},
                {"static_routes",std::move(routeRows)},{"static_route_unique",ranked.size()},
                {"static_route_excluded_limit",ranked.size()>Experiment::StaticRoutes::OutcomeLimit?ranked.size()-Experiment::StaticRoutes::OutcomeLimit:0},
                {"static_route_cpu_ns",routeNs},{"source_count",oracle.candidates.size()},{"optimal",best},{"features",work.features},{"patterns",std::move(outcomes)}};
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
            write(work.line); ++metrics.completed; pending.pop_front();
        }
    }
};

Probe::Probe(ComputeExecutor& executor,std::string path,Config config):impl(std::make_unique<Impl>(executor,std::move(path),config))
{
    Json patterns=Json::array();
    for (const auto& pattern:Experiment::pilotPatterns()) patterns.push_back(pattern.name);
    impl->write(Json{{"kind","schema"},{"version",1},{"cohort","sampled-material-movement-and-pickup-decisions-before-choice"},
        {"sample_every",config.sampleEvery},{"max_samples",config.maxSamples},{"max_pending",config.maxPending},
        {"max_output_bytes",config.maxOutputBytes},{"claims_enforced",false},{"destination_lookup","per-event-published-xy-or-scalar-fallback"},
        {"continuation_policy","diagnostic-ordinal-resets-after-load"},{"patterns",patterns},
        {"new_destination_reads_semantics","marginal shared event-cache misses in pattern order; instrumentation amortization only, not standalone policy cost"},
        {"stale_goal_eligibility","published XY from natural-only field; failed scalar projection and mixed supplier fields excluded"},
        {"outcome_columns",{"pattern","policy","source","access","probe","weighted_cost","probes","new_destination_reads","fallback","access_weighted_cost"}},
        {"static_route_limits",{{"unique_outcomes",Experiment::StaticRoutes::OutcomeLimit},{"expansions_per_search",Experiment::StaticRoutes::ExpansionLimit},
            {"continuation_steps",Experiment::StaticRoutes::ContinuationLimit},{"selection","diagnostic_hash_without_replacement"},
            {"heuristic","minimum_rule_entry_chebyshev; zero_for_farm_goal_sets"}}},
        {"policies",{"first_available","closest_geometric"}}}.dump()+"\n");
}
Probe::~Probe()=default;
const Probe::Metrics& Probe::metrics() const {return impl->metrics;}
void Probe::publish(Uint32 tick) {impl->publish(tick);}
void Probe::finish()
{
    if(impl->finished)return;
    impl->publish(UINT32_MAX,true);
    const auto& m=impl->metrics;
    impl->write(Json{{"kind","summary"},{"complete",true},{"decisions",m.decisions},{"sampled",m.sampled},
        {"contested",m.contested},{"submitted",m.submitted},{"completed",m.completed},
        {"excluded_sample_limit",m.excludedSampleLimit},{"excluded_queue_limit",m.excludedQueueLimit},{"excluded_missing_field",m.excludedMissingField},
        {"material_decisions",impl->materialDecisions},{"material_sampled",impl->materialSampled},
        {"material_contested",impl->materialContested},{"sampled_infinite",impl->sampledInfinite},
        {"sampled_pooled",impl->sampledPooled},{"sampled_exhausted_pools",impl->sampledExhaustedPools},
        {"stale_scalar_ineligible",impl->staleScalarIneligible},{"stale_mixed_field_ineligible",impl->staleMixedFieldIneligible},
        {"stale_invalid_xy",impl->staleInvalidXY},{"capture_bytes",m.captureBytes}}.dump()+"\n");
    const auto file=impl->file; impl->file=nullptr;
    if(gzclose(file)!=Z_OK) throw std::runtime_error("resource claim probe close failed; dataset is incomplete");
    impl->metrics.outputBytes=std::filesystem::file_size(impl->path);
    impl->finished=true;
}

void Probe::decision(Game& game,const Unit& unit,const Uint16* field,Uint32 pickupAccess)
{
    if(impl->finished)return;
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
    const Uint32 reserve=pooled && !game.gameHeader.isResourceGrowthDisabled()?yield.seedReserve:0;
    const Uint32 capacity=!staleGoal && amount>reserve?amount-reserve:0;
    const Claim desired{Uint32(source),Uint32(access),Uint8(unit.destinationPurpose),map.getResource(source).type,exclusive};
    impl->sampledInfinite+=yield.consumption==ResourceConsumption::Infinite;
    impl->sampledPooled+=pooled; impl->sampledExhaustedPools+=exhaustedPool;
    if(!staleGoal && map.materialClaimLedger().available(unitOwner(unit.gid,unit.scriptIdentity),desired,capacity,yield.consumption==ResourceConsumption::Infinite))return;
    ++metrics.contested;
    ++impl->materialContested.at(unit.destinationPurpose);
    if(metrics.submitted>=impl->config.maxSamples){++metrics.excludedSampleLimit;return;}
    if(impl->pending.size()>=impl->config.maxPending){++metrics.excludedQueueLimit;return;}
    auto work=std::make_unique<Impl::Work>();
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
    if(work->destinations && work->destinations->cells!=map.cellCount())
        throw std::logic_error("claim probe destination publication shape mismatch");
    work->query={unitOwner(unit.gid,unit.scriptIdentity),Uint32(map.coordToIndex(unit.posX,unit.posY)),Uint8(unit.destinationPurpose),
        {unit.movementClass(),unit.owner->me,unit.gid,OccupancyPolicy::Static}};
    work->pickup=pickupAccess!=UINT32_MAX;
    work->tick=game.stepCounter; work->seed=seed; work->ordinal=ordinal; work->nearest=Uint32(access);
    work->capacity=capacity; work->claimed=map.materialClaimLedger().materialCount(Uint32(source),unit.destinationPurpose);
    Json fieldKey=nullptr, fieldGeneration=nullptr;
    for (const auto& plane:map.publishedResourceFields()) if (*plane.slot==field) {
        fieldKey=plane.key; fieldGeneration=plane.generation; break;
    }
    const auto properties=hasResource?map.resourcePropertiesByIndex(map.getResource(source).type):ResourceProperties{};
    unsigned forbiddenNeighbors=0;
    for (int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
        forbiddenNeighbors+=map.isForbidden(x+dx,y+dy,unit.owner->me);
    work->features={{"resource_material_mask",hasResource?Json(properties.materialMask):Json(nullptr)},{"farm_areas_enabled",map.farmAreasEnabled()},
        {"nearby_forbidden_count",forbiddenNeighbors},{"resource_persists_when_empty",hasResource?Json(properties.persistsWhenEmpty):Json(nullptr)},
        {"resource_growth_rate",hasResource?Json(properties.growthRate):Json(nullptr)},{"team_units",unit.owner->liveUnits.size()},{"team_buildings",unit.owner->liveBuildings.size()},
        {"colony_count",game.teamsCount()},{"unit_type",unit.typeNum},{"hp",unit.hp},{"max_hp",unit.performance[HP]},
        {"hunger",unit.hungry},{"food_capacity",unit.foodCapacity()},{"medical",int(unit.medical)},
        {"activity",int(unit.activity)},{"displacement",int(unit.displacement)},{"job_purpose",int(unit.jobPurpose)},
        {"cargo_packets",unit.carriedPacketCount()},{"carried_material",unit.carriedMaterial},
        {"harvest_level",unit.level[HARVEST]},{"walk_speed",unit.performance[WALK]},
        {"swim_speed",unit.performance[SWIM]},{"fly_speed",unit.performance[FLY]},
        {"pooled_farm",pooled},{"exhausted_pool",exhaustedPool},{"source",source},{"resource_type",hasResource?Json(map.getResource(source).type):Json(nullptr)},
        {"yield_consumption",hasResource?Json(int(yield.consumption)):Json(nullptr)},{"infinite",hasResource?Json(yield.consumption==ResourceConsumption::Infinite):Json(nullptr)},
        {"destroys_deposit",hasResource?Json(yield.destroysDeposit):Json(nullptr)},{"stock_amount",amount},{"stock_reserve",reserve},
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
