// SPDX-License-Identifier: GPL-3.0-or-later
#include "MovementShadow.h"
#include "Game.h"
#include "Unit.h"
#include "Building.h"
#include "Team.h"
#include <algorithm>
#include <chrono>
#include <tuple>

namespace MovementPlanning
{
namespace
{
using Clock=std::chrono::steady_clock;
Uint64 elapsed(Clock::time_point start) { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count(); }
Task signature(Task task) { task.x=task.y=0; return task; }
void capturePursuitTarget(Task& task,const SimulationSnapshot::Handle& world)
{
    const auto cell=world.occupancy->cells[world.view().index(task.targetX,task.targetY)];
    const auto gid=cell.groundUnit!=0xffff?cell.groundUnit:cell.airUnit;
    if(gid<world.unitCores->unitSlotIndices.size()) {
        const auto record=world.unitCores->unitSlotIndices[gid];
        if(record!=SimulationSnapshot::Entities::NoRecord)task.targetUnit=world.unitCores->units.at(record).identity;
    }
    if(cell.building<world.entities->buildingSlotIndices.size()) {
        const auto record=world.entities->buildingSlotIndices[cell.building];
        if(record!=SimulationSnapshot::Entities::NoRecord)task.target=world.entities->buildings.at(record).identity;
    }
}
void currentPursuitTarget(Task& task,const Game& game)
{
    const auto ground=game.map.getGroundUnit(task.targetX,task.targetY),air=game.map.getAirUnit(task.targetX,task.targetY);
    const auto gid=ground!=NOGUID?ground:air;
    if(gid!=NOGUID) {
        const auto team=Unit::GIDtoTeam(gid),slot=Unit::GIDtoID(gid);
        if(team<game.teamsCount() && game.teams[team])task.targetUnit=Game::refOf(game.teams[team]->myUnits[slot]);
    }
    const auto bid=game.map.getBuilding(task.targetX,task.targetY);
    if(bid!=NOGBID) {
        const auto team=Building::GIDtoTeam(bid),slot=Building::GIDtoID(bid);
        if(team<game.teamsCount() && game.teams[team])task.target=Game::refOf(game.teams[team]->myBuildings[slot]);
    }
}
SimulationSnapshot::UnitCoreView viewOf(const Unit& unit)
{
    SimulationSnapshot::UnitCoreView view;
    view.core=unit.core;
    view.identity=Game::refOf(&unit); view.team=unit.owner->teamNumber;
    view.attached=Game::refOf(unit.attachedBuilding); view.target=Game::refOf(unit.targetBuilding);
    return view;
}
bool currentTowerThreat(const Unit& unit,int x,int y)
{
    for(unsigned team=0;team<Team::MAX_COUNT;++team)
        if(const auto* enemy=unit.owner->game->teams[team]; enemy && (unit.owner->enemies & enemy->me))
            for(const auto* building:enemy->liveBuildings.entries())
                if(building->runtime->shootingRange>0
                    && unit.owner->map->warpDistMax(building->posX,building->posY,x,y)<=building->runtime->shootingRange+1
                    && building->hasClearShotTo(x,y))return true;
    return false;
}
constexpr const char* Names[] = {"idle","explore","resource","building","clearing","pursuit"};
}
Shadow::Shadow(ComputeExecutor& executor,unsigned delay,unsigned teams,int width,int height)
    : executor(executor),delay(delay)
{
    if (delay!=0 && delay!=1 && delay!=2 && delay!=4 && delay!=8) throw std::invalid_argument("movement shadow delay must be 1, 2, 4 or 8");
    if(!teams || teams>Team::MAX_COUNT || width<=0 || height<=0)throw std::invalid_argument("invalid movement shadow configuration");
    if (!delay) return; // profiling-only reference control
    const std::size_t quota=std::min<std::size_t>(32*1024*1024,128*1024*1024/teams);
    const Uint64 cells=Uint64(width)*height;
    // Conservative capture arrays (terrain/resource/occupancy/areas/visibility,
    // exploration per team) plus one scratch/field workspace per team. Published
    // planes, entities and nested payloads are reported separately by the store.
    if (cells > (quota-4096)/16) throw std::invalid_argument("movement shadow map exceeds route workspace quota");
    for(unsigned t=0;t<teams;++t)planners[t]=std::make_unique<Planner>(quota);
    static_assert(ComputeExecutor::MovementHorizon>=8);
    static_assert(ComputeExecutor::MovementLanes>=Team::MAX_COUNT);
}
Shadow::~Shadow() { try { drain(); } catch (...) { /* Explicit boundary/drain reports failures. */ } }
void Shadow::run(void* context,std::size_t)
{
    auto& job=*static_cast<TeamJob*>(context);
    const auto start=Clock::now();
    for(std::size_t i=0;i<job.requests.size();++i)job.outputs[i]=job.planner->plan(job.world,job.requests[i],job.seed);
    job.activeNs=elapsed(start);
}
void Shadow::publish(Uint32 tick)
{
    while(!pending.empty() && pending.front()->due<=tick) {
        auto& batch=*pending.front();
        const bool unfinished=!executor.finished(batch.ticket);
        const auto start=Clock::now();
        executor.join(batch.ticket); // rethrows before publishing any output
        if(unfinished) { ++waits; waitNs+=elapsed(start); }
        for(auto& job:batch.teams) {
            activeNs+=job.activeNs;
            for(auto& output:job.outputs) {
                auto& m=metrics[output.request.task.team][unsigned(output.request.mode)]; ++m.published;
                const auto found=actors.find(output.request.task.unit.gid);
                if(found==actors.end())continue;
                auto& actor=found->second;
                if(actor.task.unit!=output.request.task.unit || actor.pendingSequence!=output.request.sequence)continue;
                actor.pendingSequence=0; actor.published=std::move(output);
            }
        }
        pending.pop_front();
    }
}
void Shadow::boundary(const Game& game,const SimulationSnapshot::Handle& world)
{
    publish(world.tick);
    if(lastBoundary && *lastBoundary==world.tick)return;
    lastBoundary=world.tick;
    const auto start=Clock::now();
    auto batch=std::make_unique<Batch>(); batch->due=world.tick+delay;
    if(batch->due<world.tick)throw std::overflow_error("movement publication tick overflow");
    const unsigned teams=game.mapHeader.getNumberOfTeams();
    batch->teams.resize(teams);
    for(unsigned team=0;team<teams;++team) {
        batch->teams[team].world=world.project(requirements);
        batch->teams[team].planner=planners[team].get();
        batch->teams[team].seed=game.gameHeader.getRandomSeed();
    }
    std::vector<Uint16> live; live.reserve(world.unitCores->units.size());
    for(const auto& unit:world.unitCores->units) {
        live.push_back(unit.core.gid);
        auto& actor=actors[unit.core.gid];
        auto task=taskOf(unit);
        if(modeOf(unit.core)==Mode::Pursuit)capturePursuitTarget(task,world);
        if(!actor.initialized || actor.task.unit!=task.unit) { actor={};actor.task=task;actor.initialized=true; }
        const bool changed=signature(actor.task)!=signature(task);
        if(changed) { ++actor.epoch;actor.published.reset();actor.pendingSequence=0;actor.requested=false; }
        const bool moved=actor.task.x!=task.x || actor.task.y!=task.y;
        actor.task=task;
        auto mode=modeOf(unit.core); if(!mode)continue;
        auto remaining=completionTicks(unit.core); if(!remaining)continue;
        if(actor.published && (actor.published->request.task!=task || actor.published->request.actionGeneration!=actor.actionGeneration))actor.published.reset();
        if(actor.pendingSequence && actor.requestedGeneration==actor.actionGeneration && !moved && !changed)continue;
        if(actor.published)continue;
        const bool invalidated=changed || moved || !actor.requested || actor.requestedGeneration!=actor.actionGeneration;
        if(!invalidated && *remaining>delay)continue;
        Request request{task,*mode,actor.epoch,actor.actionGeneration,++actor.sequence,world.tick,
            world.tick+*remaining-1,batch->due};
        actor.pendingSequence=request.sequence;actor.requested=true;actor.requestedGeneration=actor.actionGeneration;
        batch->teams[unit.team].requests.push_back(request);
        ++metrics[unit.team][unsigned(*mode)].admitted;
    }
    std::sort(live.begin(),live.end());
    for(auto it=actors.begin();it!=actors.end();) {
        if(!std::binary_search(live.begin(),live.end(),it->first))it=actors.erase(it);else ++it;
    }
    std::vector<ComputeExecutor::Group> groups;
    for(unsigned team=0;team<teams;++team) {
        auto& job=batch->teams[team];
        std::sort(job.requests.begin(),job.requests.end(),[](const auto& a,const auto& b) {
            return std::tie(a.task.unit.generation,a.task.unit.gid)<std::tie(b.task.unit.generation,b.task.unit.gid);
        });
        job.outputs.resize(job.requests.size());
        if(!job.requests.empty())groups.push_back({1,{&run,&job},ComputeExecutor::MovementLaneBase+team});
        else job.world={};
    }
    if(!groups.empty()) {
        batch->ticket=executor.submit(groups,ComputeExecutor::advanceDue(batch->due));
        pending.push_back(std::move(batch));maxBatches=std::max<Uint64>(maxBatches,pending.size());
        if(pending.size()>delay+1)throw std::logic_error("movement shadow exceeded publication horizon");
    }
    admissionNs+=elapsed(start);peakBytes=std::max(peakBytes,retainedBytes());
}
Counters* Shadow::counters(const Unit& unit)
{
    const auto mode=modeOf(unit.core);
    if(!mode)return nullptr;
    return &metrics[unit.owner->teamNumber][unsigned(*mode)].reference;
}
void Shadow::completeAction(const Unit& unit)
{
    const auto found=actors.find(unit.core.gid);
    if(found!=actors.end() && found->second.task.unit==Game::refOf(&unit))++found->second.actionGeneration;
}
void Shadow::observe(const Unit& unit)
{
    const auto mode=modeOf(unit.core);
    // Clearing preemption and combat acquisition can replace a travel action.
    if(!mode || unit.core.movement==UnitState::MOV_ATTACKING_TARGET || unit.core.movement==UnitState::MOV_HARVESTING)return;
    auto& m=metrics[unit.owner->teamNumber][unsigned(*mode)];++m.reference.opportunities;
    if(!delay)return;
    auto task=taskOf(viewOf(unit));
    if(*mode==Mode::Pursuit)currentPursuitTarget(task,*unit.owner->game);
    auto found=actors.find(unit.core.gid);
    if(found==actors.end()) { ++m.rapid;++m.missing;return; }
    auto& actor=found->second;
    const bool stable=signature(task)==signature(actor.task);
    if(stable)++m.stable;else ++m.rapid;
    if(!actor.published) { ++m.missing; return; }
    auto suggestion=std::move(*actor.published);actor.published.reset();
    const auto& r=suggestion.request;
    const bool samePosition=task.x==r.task.x && task.y==r.task.y;
    const bool sameTask=signature(task)==signature(r.task) && actor.epoch==r.epoch;
    const bool sameGeneration=task.unit==r.task.unit && actor.actionGeneration==r.actionGeneration;
    if(!samePosition) { ++m.positionRejected;return; }
    if(!sameTask) { ++m.taskRejected;return; }
    if(!sameGeneration) { ++m.generationRejected;return; }
    if(stable)++m.valid;
    m.predictionErrorTicks+=std::abs(Sint64(unit.owner->game->stepCounter)-r.expectedCompletionTick);
    if(suggestion.disposition==Disposition::GoalReached)++m.goalReached;
    if(suggestion.disposition==Disposition::NoRoute)++m.noRoute;
    auto& map=unit.owner->game->map;
    if(suggestion.destination) {
        const auto& d=*suggestion.destination;
        bool valid=true;
        if(!d.supplier.empty()) {
            const auto* supplier=unit.owner->game->resolveBuilding(d.supplier);
            const auto* home=unit.attachedBuilding;
            valid=supplier && home && supplier!=home && supplier->owner==unit.owner
                && supplier->buildingState==Building::ALIVE && home->buildingState==Building::ALIVE;
            if(valid && r.mode==Mode::Resource) {
                const auto material=r.task.material;
                valid=material>=0 && material<int(MaterialCount) && supplier->availableMaterial(material)>0;
                if(valid) {
                    const auto mask=1u<<material;
                    valid=((supplier->runtime->suppliesStockMask & home->runtime->fetchesStockMask)
                        | (supplier->runtime->suppliesDirectStockMask & home->runtime->fetchesDirectStockMask)) & mask;
                }
            }
        } else if(r.mode==Mode::Resource)valid=map.isMaterialTakeableSlot(d.x,d.y,r.task.material);
        if(!valid)++m.destinationRejected;
    }
    unsigned rank=0;
    for(auto direction:suggestion.directions) {
        const auto d=Directions[direction]; const int x=task.x+d[0],y=task.y+d[1];
        bool allowed=task.fly ? map.isFreeForAirUnit(x,y) : map.isFreeForGroundUnit(x,y,task.swim>0,unit.owner->me);
        const auto& terrain=map.terrainPropertiesAt(x,y);
        allowed &= task.fly || task.walk>0 || terrain.swimmable;
        if(*mode==Mode::Idle || *mode==Mode::Explore)allowed &= (task.fly?terrain.airHealthQ8:terrain.groundHealthQ8)>=0;
        if(*mode==Mode::Explore)allowed &= !currentTowerThreat(unit,x,y);
        if(allowed)break;
        ++rank;
    }
    ++m.selectedRank[rank];
    if(rank==8)++m.blocked;else if(rank<suggestion.preferred)++m.preferredSuccess;
}
void Shadow::drain()
{
    // Saving/lifecycle completes computation but never publishes a future result.
    std::exception_ptr failure;
    for(auto& batch:pending)try{executor.join(batch->ticket);}catch(...){if(!failure)failure=std::current_exception();}
    if(failure)std::rethrow_exception(failure);
}
Uint64 Shadow::retainedBytes() const
{
    Uint64 bytes=sizeof(*this)+actors.size()*(sizeof(Actor)+4*sizeof(void*));
    for(const auto& batch:pending) {
        bytes+=sizeof(Batch)+batch->teams.capacity()*sizeof(TeamJob);
        for(const auto& job:batch->teams)bytes+=job.requests.capacity()*sizeof(Request)+job.outputs.capacity()*sizeof(Suggestion);
    }
    // Worker-owned caches and scratch are published only after their lane joins;
    // do not inspect a vector concurrently with a running job.
    return bytes;
}
std::vector<std::pair<std::string,Uint64>> Shadow::summary() const
{
    Uint64 completedActive=activeNs;
    bool settled=true;
    for(const auto& batch:pending) { settled &= executor.finished(batch->ticket); }
    if(settled)for(const auto& batch:pending)for(const auto& job:batch->teams)completedActive+=job.activeNs;
    std::vector<std::pair<std::string,Uint64>> result{{"owner_cpu_available",OwnerCpuAvailable},{"workers_settled",settled},{"delay",delay},{"active_ns",completedActive},{"admission_ns",admissionNs},
        {"deadline_waits",waits},{"deadline_wait_ns",waitNs},{"maximum_batches",maxBatches},{"owner_retained_bytes",retainedBytes()},{"owner_peak_bytes",peakBytes}};
    if(settled) {
        Uint64 cacheBytes=0,peakWorkspace=0,visited=0,hits=0,fields=0,unknown=0;
        for(const auto& planner:planners)if(planner) {
            cacheBytes+=planner->retainedBytes();peakWorkspace+=planner->metrics.peakBytes;
            visited+=planner->metrics.visited;hits+=planner->metrics.cacheHits;fields+=planner->metrics.fields;unknown+=planner->metrics.unknown;
        }
        result.emplace_back("cache_bytes",cacheBytes);result.emplace_back("peak_cache_workspace_bytes",peakWorkspace);
        result.emplace_back("planner_visited",visited);result.emplace_back("planner_cache_hits",hits);
        result.emplace_back("planner_fields",fields);result.emplace_back("planner_unknown",unknown);
    }
    for(unsigned t=0;t<Team::MAX_COUNT;++t)for(unsigned mode=0;mode<unsigned(Mode::Count);++mode) {
        const auto& m=metrics[t][mode];if(!m.admitted && !m.reference.opportunities)continue;
        const std::string prefix="team_"+std::to_string(t)+"_"+Names[mode]+"_";
        auto add=[&](const char* name,Uint64 value){result.emplace_back(prefix+name,value);};
        add("admitted",m.admitted);add("published",m.published);add("stable_opportunities",m.stable);add("rapid_opportunities",m.rapid);
        add("valid",m.valid);add("missing",m.missing);add("position_rejected",m.positionRejected);add("task_rejected",m.taskRejected);add("generation_rejected",m.generationRejected);
        add("preferred_success",m.preferredSuccess);add("blocked",m.blocked);add("goal_reached",m.goalReached);add("no_route",m.noRoute);add("destination_rejected",m.destinationRejected);
        add("prediction_error_ticks",m.predictionErrorTicks);
        for(unsigned rank=0;rank<9;++rank)result.emplace_back(prefix+"rank_"+std::to_string(rank),m.selectedRank[rank]);
        add("reference_route_expansions",m.reference.routeExpansions);add("reference_destination_searches",m.reference.destinationSearches);add("reference_direction_choices",m.reference.directionChoices);add("reference_failed_searches",m.reference.failedSearches);
        add("reference_searches",m.reference.searches);add("reference_visited",m.reference.visited);add("reference_search_ns",m.reference.searchNs);
        for(unsigned phase=0;phase<unsigned(Phase::Count);++phase) {
            result.emplace_back(prefix+"reference_phase_"+std::to_string(phase)+"_ns",m.reference.phaseNs[phase]);
            result.emplace_back(prefix+"reference_phase_"+std::to_string(phase)+"_cpu_ns",m.reference.phaseCpuNs[phase]);
        }
    }
    return result;
}
}
