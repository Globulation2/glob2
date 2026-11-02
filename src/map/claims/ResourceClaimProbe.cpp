// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceClaimProbe.h"
#include "ResourceClaimOracle.h"
#include "ResourceClaimPatternEvaluation.h"
#include "ResourceClaimPilotPatterns.h"
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
            // Temporary shared lookup cache in PatternEvaluator avoids repeating
            // weighted ascents. This callback is replaced by published XY planes
            // when those planes are wired into material snapshots.
            const auto destination=[&](Uint32 cell) {
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
            unsigned patternIndex=0;
            for (const auto& pattern:Experiment::pilotPatterns()) {
                for (unsigned policy=0;policy<2;++policy) {
                    const auto outcome=evaluator.evaluate(pattern.viewForDirection(evaluator.closestOctant()),policy?Experiment::PatternSelection::ClosestGeometric:Experiment::PatternSelection::FirstAvailable,destination);
                    outcomes.push_back({patternIndex,policy,outcome.source,outcome.access,outcome.probe,distanceJson(outcome.distance),outcome.probes,outcome.destinationReads,outcome.fallback,distanceJson(outcome.accessDistance)});
                }
                ++patternIndex;
            }
            Json best=nullptr;
            if (oracle.best) {
                const auto& chosen=oracle.candidates[*oracle.best];
                best={{"source",chosen.source},{"access",chosen.access},{"stance",chosen.stance},{"distance",chosen.distance}};
            }
            Json row={{"kind","decision"},{"phase",work.pickup?"pickup":"movement"},{"tick",work.tick},{"ordinal",work.ordinal},{"seed",work.seed},
                {"width",view.width},{"height",view.height},{"owner",work.query.owner},{"gid",work.query.route.unitGid},{"team_mask",work.query.route.teamMask},
                {"movement",work.query.route.movement},{"material",work.query.material},{"origin",work.query.origin},
                {"nearest",work.nearest},{"capacity",work.capacity},{"claimed",work.claimed},
                {"occupancy","static"},{"nearest_octant",evaluator.closestOctant()},{"oracle_expanded",oracle.expanded},{"oracle_ns",oracleNs},
                {"source_count",oracle.candidates.size()},{"optimal",best},{"features",work.features},{"patterns",std::move(outcomes)}};
            work.line=row.dump()+"\n";
        } catch (...) { work.error=std::current_exception(); }
        work.world={}; work.owners.clear(); work.field.clear();
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
        {"max_output_bytes",config.maxOutputBytes},{"claims_enforced",false},{"destination_lookup","weighted-scalar-projection"},
        {"continuation_policy","diagnostic-ordinal-resets-after-load"},{"patterns",patterns},
        {"outcome_columns",{"pattern","policy","source","access","probe","weighted_cost","probes","new_destination_reads","fallback","access_weighted_cost"}},
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
        {"capture_bytes",m.captureBytes}}.dump()+"\n");
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
    const auto seed=game.gameHeader.getRandomSeed();
    if(mix(mix(seed)^mix(game.stepCounter)^mix(unit.scriptIdentity)^mix(ordinal))%impl->config.sampleEvery)return;
    ++metrics.sampled;
    if(!field){++metrics.excludedMissingField;return;}
    auto& map=game.map;
    map.reconcileMaterialClaims();
    Sint32 x=0,y=0;
    if(pickupAccess!=UINT32_MAX) { x=int(pickupAccess%map.getW()); y=int(pickupAccess/map.getW()); }
    else if(!map.projectMaterialDestination(field,unit.owner->teamNumber,unit.destinationPurpose,unit.movementClass(),unit.posX,unit.posY,&x,&y,
        unit.attachedBuilding && unit.attachedBuilding->fetchesFromMarkets(),unit.attachedBuilding))return;
    const auto access=map.coordToIndex(x,y);
    if(!map.materialAmountAtSlot(access,unit.destinationPurpose))return; // Supplier contention is a separate cohort.
    auto source=access;
    const auto& accessYield=map.resourceRegistry().yields(static_cast<ResourceId>(map.getResource(access).type))[unit.destinationPurpose];
    const bool exclusive=accessYield.consumption==ResourceConsumption::All || accessYield.destroysDeposit;
    const bool pooled=!exclusive && map.farmAreasEnabled() && map.isFarmArea(x,y,unit.owner->me) && map.isFarmableResourceByIndex(map.getResource(access).type);
    bool exhaustedPool=false;
    if(pooled) {
        const auto selected=map.pickClaimFarmHarvestTileSlot(unit.posX,unit.posY,unit.destinationPurpose,unit.owner->me,
            pickupAccess!=UINT32_MAX?std::nullopt:std::optional<size_t>(access));
        if(selected) source=*selected;
        else exhaustedPool=true; // The seed-only field is itself a contested failure case.
    }
    const auto& yield=map.resourceRegistry().yields(static_cast<ResourceId>(map.getResource(source).type))[unit.destinationPurpose];
    const Uint32 amount=map.materialAmountAtSlot(source,unit.destinationPurpose);
    const Uint32 reserve=pooled && !game.gameHeader.isResourceGrowthDisabled()?yield.seedReserve:0;
    const Uint32 capacity=amount>reserve?amount-reserve:0;
    const Claim desired{Uint32(source),Uint32(access),Uint8(unit.destinationPurpose),map.getResource(source).type,exclusive};
    if(map.materialClaimLedger().available(unit.scriptIdentity,desired,capacity,yield.consumption==ResourceConsumption::Infinite))return;
    ++metrics.contested;
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
    work->query={unit.scriptIdentity,Uint32(map.coordToIndex(unit.posX,unit.posY)),Uint8(unit.destinationPurpose),
        {unit.movementClass(),unit.owner->me,unit.gid,OccupancyPolicy::Static}};
    work->pickup=pickupAccess!=UINT32_MAX;
    work->tick=game.stepCounter; work->seed=seed; work->ordinal=ordinal; work->nearest=Uint32(access);
    work->capacity=capacity; work->claimed=map.materialClaimLedger().materialCount(Uint32(source),unit.destinationPurpose);
    Json fieldKey=nullptr, fieldGeneration=nullptr;
    for (const auto& plane:map.publishedResourceFields()) if (*plane.slot==field) {
        fieldKey=plane.key; fieldGeneration=plane.generation; break;
    }
    const auto& properties=map.resourcePropertiesByIndex(map.getResource(source).type);
    unsigned forbiddenNeighbors=0;
    for (int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
        forbiddenNeighbors+=map.isForbidden(x+dx,y+dy,unit.owner->me);
    work->features={{"resource_material_mask",properties.materialMask},{"farm_areas_enabled",map.farmAreasEnabled()},
        {"nearby_forbidden_count",forbiddenNeighbors},{"resource_persists_when_empty",properties.persistsWhenEmpty},
        {"resource_growth_rate",properties.growthRate},{"team_units",unit.owner->liveUnits.size()},{"team_buildings",unit.owner->liveBuildings.size()},
        {"colony_count",game.teamsCount()},{"unit_type",unit.typeNum},{"hp",unit.hp},{"max_hp",unit.performance[HP]},
        {"hunger",unit.hungry},{"food_capacity",unit.foodCapacity()},{"medical",int(unit.medical)},
        {"activity",int(unit.activity)},{"displacement",int(unit.displacement)},{"job_purpose",int(unit.jobPurpose)},
        {"cargo_packets",unit.carriedPacketCount()},{"carried_material",unit.carriedMaterial},
        {"harvest_level",unit.level[HARVEST]},{"walk_speed",unit.performance[WALK]},
        {"swim_speed",unit.performance[SWIM]},{"fly_speed",unit.performance[FLY]},
        {"pooled_farm",pooled},{"exhausted_pool",exhaustedPool},{"source",source},{"resource_type",map.getResource(source).type},
        {"yield_consumption",int(yield.consumption)},{"infinite",yield.consumption==ResourceConsumption::Infinite},
        {"destroys_deposit",yield.destroysDeposit},{"stock_amount",amount},{"stock_reserve",reserve},
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
