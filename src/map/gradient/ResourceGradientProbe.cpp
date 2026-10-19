// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceGradientProbe.h"
#include "GradientRuntime.h"
#include "Map.h"
#include "Game.h"
#include "MapInternal.h"
#include "SeedPoint.h"
#include <filesystem>
#include <stdexcept>

namespace {
void identity(std::ostream& out,GradientFieldKey k) {out<<unsigned(k.family)<<','<<unsigned(k.team)<<','<<unsigned(k.swim)<<','<<unsigned(k.material)<<','<<k.packed();}
const Uint16* serving(Map& map,GradientFieldKey k) {
    switch(k.family) {
    case GradientFieldFamily::Materials:return map.lookupMaterialGradientSlot(k.team,k.material,k.swim,false);
    case GradientFieldFamily::Markets:return map.lookupMaterialGradientSlot(k.team,k.material,k.swim,true);
    case GradientFieldFamily::Guard:return map.lookupGuardAreasGradient(k.team,k.swim);
    case GradientFieldFamily::Clear:return map.lookupClearAreasGradient(k.team,k.swim);
    case GradientFieldFamily::Forbidden:return map.lookupForbiddenGradient(k.team,k.swim);
    }
    return nullptr;
}
}
ResourceGradientProbe::ResourceGradientProbe(const char* path):output(path),study(std::filesystem::path(path).parent_path()/"gradient-episodes.csv"),planning(std::filesystem::path(path).parent_path()/"gradient-planning-quality.csv"),movement(std::filesystem::path(path).parent_path()/"gradient-contradictions.csv") {
    seedScratch.materials.disabled=true; // Private captures are not owner-cache observations.
    if(!output||!study||!planning||!movement)throw std::runtime_error("Cannot open unified gradient diagnostics");
    output<<"query,tick,family,team,movement,material,key,extra_cost,false_unreachable,false_reachable,invalid_goal,missed_move,escape_failure,stall,guard_balance\n";
    study<<"tick,family,team,movement,material,key,complete,publication_delay,queries,defer_extra,refresh_extra,age,q32,q128,target_added,target_removed,target_added_frac_q12,target_removed_frac_q12,since_capture_invalid_goal,since_capture_reachable_no_move,since_capture_movement_queries,capture_known,movement_observed,route_changes,seed_cost_changes,planning_queries,defer_planning_false_negative,defer_planning_false_positive,refresh_planning_false_negative,refresh_planning_false_positive,behavior_interval,behavior_seed,defer_stalls,refresh_stalls,defer_invalid_goals,refresh_invalid_goals,defer_missed_moves,refresh_missed_moves,defer_escape_failures,refresh_escape_failures,guard_balance_queries,serving_capture_tick,captured_goal_count,capture_target_added_epoch,capture_target_removed_epoch,capture_route_epoch,capture_seed_cost_epoch,current_target_added_epoch,current_target_removed_epoch,current_route_epoch,current_seed_cost_epoch,defer_false_unreachable,refresh_false_unreachable,defer_false_reachable,refresh_false_reachable,overlap_supported,overlap_support_reason,overlap_history_known,overlap_generation,overlap_publication_tick,queries_near_goal_change,queries_near_passability_change,overlap_movement_queries,queries_near_goal_added,queries_near_goal_removed,queries_near_blocked,queries_near_opened\n";
    planning<<"query,tick,family,team,movement,material,key,false_unreachable,false_reachable,live_value,fresh_value\n";
    movement<<"epoch,family,team,movement,material,key,invalid_goal,reachable_no_move,movement_queries\n";
}
ResourceGradientProbe::~ResourceGradientProbe(){finishStudy(lastTick);writeMovementTotals();}
GradientFieldKey ResourceGradientProbe::legacyKey(int key) {return {GradientFieldFamily::Materials,std::uint8_t(key/(MaterialCount*SWIM_CLASS_COUNT)),std::uint8_t(key%SWIM_CLASS_COUNT),std::uint8_t((key/SWIM_CLASS_COUNT)%MaterialCount)};}
void ResourceGradientProbe::publishedOverlap(GradientFieldKey key,unsigned tick,const Uint16* cells) {
    if(!spatialEnabled || !gradient_seed_overlap::supported(key))return;
    movementHistory[int(key.packed())].overlap.published(tick,cells);
}
void ResourceGradientProbe::observeOverlap(Map& map,GradientFieldKey key,int x,int y,const Uint16* cells) {
    if(!spatialEnabled || !gradient_seed_overlap::supported(key) || !cells || !map.game || map.compute.insideJob())return;
    const auto found=movementHistory.find(int(key.packed()));
    if(found==movementHistory.end())return;
    auto& history=found->second.overlap;
    // A newly attached probe has unknown history until a real publication.
    // A stale borrowed pointer must not contribute to a replacement generation.
    if(!history.known || history.publishedCells!=cells || serving(map,key)!=cells)return;
    const auto view=map.cellView();
    const auto old=gradient_seed_overlap::read(x,y,view.width,view.height,[&](size_t i){return gradient_seed_overlap::recoveredSeed(cells[i]);});
    gradient_preparation::withOpenTerrain(view,key.swim,[&](auto terrain) {
        if(key.family==GradientFieldFamily::Materials) {
            const auto current=gradient_preparation::naturalMaterialSeed<false>(view,map.fogOfWar,key.team,key.material,terrain);
            history.record(gradient_seed_overlap::difference(old,gradient_seed_overlap::read(x,y,view.width,view.height,current)));
        } else {
            const auto current=gradient_preparation::clearSeed<false>(view,map.farmAreasEnabled(),key.team,terrain);
            history.record(gradient_seed_overlap::difference(old,gradient_seed_overlap::read(x,y,view.width,view.height,current)));
        }
    });
}
void ResourceGradientProbe::writeMovementTotals(){for(const auto& [key,h]:movementHistory){if(!h.total.values[5])continue;movement<<movementEpoch<<',';identity(movement,GradientFieldKey::unpack(key));for(unsigned index:{0u,4u,5u})movement<<','<<h.total.values[index];movement<<'\n';}movement.flush();}
void ResourceGradientProbe::writeEpisode(int key,const Episode& e,bool complete){
    study<<e.tick<<',';identity(study,GradientFieldKey::unpack(key));study<<','<<complete<<','<<e.delay<<','<<e.queries<<','<<e.deferExtra<<','<<e.refreshExtra;
    for(auto n:e.inputs)study<<','<<n;
    study<<','<<e.contradictions.values[0]<<','<<e.contradictions.values[4]<<','<<e.contradictions.values[5]<<','<<e.contradictionCaptureKnown<<','<<e.movementObserved<<','<<e.routeChanges<<','<<e.seedCostChanges<<','<<e.planningQueries<<','<<e.deferFalseNegative<<','<<e.deferFalsePositive<<','<<e.refreshFalseNegative<<','<<e.refreshFalsePositive<<','<<e.behaviorInterval<<','<<e.behaviorSeed<<','<<e.deferStalls<<','<<e.refreshStalls<<','<<e.deferInvalidGoals<<','<<e.refreshInvalidGoals<<','<<e.deferMissedMoves<<','<<e.refreshMissedMoves<<','<<e.deferEscapeFailures<<','<<e.refreshEscapeFailures<<','<<e.guardBalanceQueries<<','<<e.servingCaptureTick<<','<<e.capturedGoalCount<<','<<e.capturedTargets.added<<','<<e.capturedTargets.removed<<','<<e.capturedTargets.routeEpoch<<','<<e.capturedTargets.seedCostEpoch<<','<<e.currentTargets.added<<','<<e.currentTargets.removed<<','<<e.currentTargets.routeEpoch<<','<<e.currentTargets.seedCostEpoch;
    study<<','<<e.deferFalseUnreachable<<','<<e.refreshFalseUnreachable<<','<<e.deferFalseReachable<<','<<e.refreshFalseReachable;
    const auto field=GradientFieldKey::unpack(key);
    const auto& o=e.overlap;
    study<<','<<gradient_seed_overlap::supported(field)<<','<<(gradient_seed_overlap::supported(field)&&!e.overlapEnabled?"disabled":gradient_seed_overlap::supportReason(field))<<','<<o.known<<','<<o.generation<<','<<o.publicationTick
        <<','<<o.goalQueries<<','<<o.passabilityQueries<<','<<o.queries<<','<<o.goalAddedQueries<<','<<o.goalRemovedQueries<<','<<o.blockedQueries<<','<<o.openedQueries<<'\n';
}
void ResourceGradientProbe::finishStudy(unsigned tick){for(auto& [key,f]:fields){for(const auto&e:f.episodes)writeEpisode(key,e,complete(unsigned(tick-e.tick)));f.episodes.clear();}study.flush();}
ResourceGradientProbe::Field* ResourceGradientProbe::sampleField(unsigned tick,GradientFieldKey key){
    lastTick=tick;if(tick%4096>=1024)return nullptr;
    if(window!=tick/4096){finishStudy(tick);fields.clear();window=tick/4096;}
    const int packed=int(key.packed());if(auto it=fields.find(packed);it!=fields.end()){it->second.lastQuery=tick;return &it->second;}
    // First demanded field per family guarantees rare families an opportunity;
    // one additional hash-selected field bounds private oracle memory/work.
    unsigned count=0;for(const auto& [k,f]:fields)count+=GradientFieldKey::unpack(k).family==key.family;
    const unsigned hash=(key.packed()^window*2246822519u)*2654435761u;
    if(count>=2 || (count && ((hash^(hash>>16))%8)!=0))return nullptr;
    auto& f=fields[packed];f.firstTick=f.lastQuery=tick;return &f;
}
void ResourceGradientProbe::rebuildFresh(Map& map,GradientFieldKey key,std::vector<Uint16>& fresh){
    // Private current-world capture. observeCompletedWorld is deliberately NOT
    // used: it may publish/admit owner work and is fixed before mid-tick edits.
    constexpr auto requirements=SimulationSnapshot::bit(SimulationSnapshot::Component::Catalogs)|SimulationSnapshot::bit(SimulationSnapshot::Component::Terrain)|SimulationSnapshot::bit(SimulationSnapshot::Component::Resources)|SimulationSnapshot::bit(SimulationSnapshot::Component::Occupancy)|SimulationSnapshot::bit(SimulationSnapshot::Component::Areas)|SimulationSnapshot::bit(SimulationSnapshot::Component::Visibility)|SimulationSnapshot::bit(SimulationSnapshot::Component::Entities)|SimulationSnapshot::bit(SimulationSnapshot::Component::Teams)|SimulationSnapshot::bit(SimulationSnapshot::Component::Rules);
    const auto snapshot=SimulationSnapshot::capture(*map.game,SimulationSnapshot::captureCatalog(*map.game),requirements);
    gradient_preparation::Request r;r.kind=gradient_preparation::Kind(key.family);r.team=key.team;r.swim=key.swim;r.material=key.material==GradientFieldKey::NoMaterial?0:key.material;r.allies=map.game->teams[key.team]->allies;r.crowding=map.game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing);r.terrainBuckets=map.movementQueueBuckets(key.swim);
    fresh.resize(map.size);gradient_kernel::CPUSelectionScope cpu(gradient_kernel::Backend::CPU);gradient_preparation::seed(r,snapshot,fresh.data(),seedScratch);gradient_preparation::propagateCPU(r,snapshot,fresh.data(),scratch);
}
void ResourceGradientProbe::stageEpisodes(Map& map){
    lastTick=map.game->stepCounter;
    for(auto& [key,f]:fields)while(!f.episodes.empty()&&complete(unsigned(lastTick-f.episodes.front().tick))){writeEpisode(key,f.episodes.front(),true);f.episodes.pop_front();}
    if(lastTick%4096+HorizonTicks>1024||lastTick/4096!=window)return;
    for(auto& [packed,f]:fields){const auto key=GradientFieldKey::unpack(packed);
        if(unsigned(lastTick-f.lastQuery)>128||unsigned(lastTick-f.firstTick)<256||(f.episodeKnown&&unsigned(lastTick-f.lastEpisode)<HorizonTicks))continue;
        const auto it=map.gradientRuntime->adaptive.fields.find(key);if(it==map.gradientRuntime->adaptive.fields.end()||it->second.queued||it->second.inflight||unsigned(lastTick-it->second.submitted)<8)continue;
        const auto* old=serving(map,key);if(!old)continue;
        auto state=it->second;const auto targets=map.gradientRuntime->adaptive.currentTargets(key);Episode e;e.tick=lastTick;e.delay=map.gradientRuntime->pipeline.delayTicks();e.inputs=state.inputs(lastTick,targets);const auto counts=state.contradictions();e.contradictions.values[0]=counts[0];e.contradictions.values[4]=counts[1];e.contradictions.values[5]=counts[2];e.contradictionCaptureKnown=state.serving.known;e.servingCaptureTick=state.serving.tick;e.capturedGoalCount=state.serving.goalCount;e.capturedTargets=state.serving.targets;e.currentTargets=targets;e.movementObserved=f.movementObserved||state.count128>0;e.routeChanges=targets.routeEpoch-state.serving.targets.routeEpoch;e.seedCostChanges=targets.seedCostEpoch-state.serving.targets.seedCostEpoch;
        e.overlapEnabled=spatialEnabled;
        if(spatialEnabled)if(const auto history=movementHistory.find(packed);history!=movementHistory.end())e.overlap=history->second.overlap;
        if(const auto& behavior=map.gradientRuntime->exploration){e.behaviorInterval=behavior->intervalFor(packed);e.behaviorSeed=behavior->seed;}
        e.deferred.assign(old,old+map.size);rebuildFresh(map,key,e.refreshed);f.episodes.push_back(std::move(e));f.lastEpisode=lastTick;f.episodeKnown=true;
    }
}
void ResourceGradientProbe::observe(Map& map,const GradientQueryContext& context,const Uint16* live){
    const auto tick=map.game->stepCounter;auto* f=sampleField(tick,context.key);if(!f)return;++query;f->movementObserved=true;for(auto&e:f->episodes)e.movementObserved=true;
    std::vector<Uint16> fresh;rebuildFresh(map,context.key,fresh);const auto here=map.coordToIndex(context.x,context.y);const auto reference=map.selectGradientQuery(context,fresh.data());
    const bool balancing=context.key.family==GradientFieldFamily::Guard && !reference.travelRequired && !reference.atGoal;
    const auto score=[&](const GradientQueryResult&r){const auto next=map.coordToIndex(context.x+r.dx,context.y+r.dy);return int(fresh[next])-map.stepCost(r.dx,r.dy,next,context.key.swim);};
    const auto quality=[&](const Uint16*cells){const auto selected=map.selectGradientQuery(context,cells);const auto next=map.coordToIndex(context.x+selected.dx,context.y+selected.dy);const bool reachable=cells[here]>GRADIENT_UNREACHABLE;const bool comparable=selected.moved&&reference.moved&&((selected.travelRequired&&reference.travelRequired)||balancing)&&fresh[next]>GRADIENT_UNREACHABLE;
        const int cost=comparable?std::max(0,score(reference)-score(selected)):0;const bool missed=!selected.moved&&reference.moved&&reference.travelRequired;const bool invalid=selected.atGoal&&!selected.goalValid;const bool stall=reachable&&selected.travelRequired&&!selected.moved&&!selected.atGoal;const bool escape=context.key.family==GradientFieldFamily::Forbidden&&missed;
        return std::array<std::int64_t,5>{cost,stall,invalid,missed,escape};};
    for(auto&e:f->episodes)if(labelEligible(unsigned(tick-e.tick),e.delay)){const auto a=quality(e.deferred.data()),b=quality(e.refreshed.data());++e.queries;const bool freshReachable=fresh[here]>GRADIENT_UNREACHABLE;
        e.deferFalseUnreachable+=e.deferred[here]<=GRADIENT_UNREACHABLE&&freshReachable;
        e.refreshFalseUnreachable+=e.refreshed[here]<=GRADIENT_UNREACHABLE&&freshReachable;
        e.deferFalseReachable+=e.deferred[here]>GRADIENT_UNREACHABLE&&!freshReachable;
        e.refreshFalseReachable+=e.refreshed[here]>GRADIENT_UNREACHABLE&&!freshReachable;
        e.guardBalanceQueries+=balancing;e.deferExtra+=a[0];e.refreshExtra+=b[0];e.deferStalls+=a[1];e.refreshStalls+=b[1];e.deferInvalidGoals+=a[2];e.refreshInvalidGoals+=b[2];e.deferMissedMoves+=a[3];e.refreshMissedMoves+=b[3];e.deferEscapeFailures+=a[4];e.refreshEscapeFailures+=b[4];}
    const auto q=quality(live);output<<query<<','<<tick<<',';identity(output,context.key);output<<','<<q[0]<<','<<(live[here]<=GRADIENT_UNREACHABLE&&fresh[here]>GRADIENT_UNREACHABLE)<<','<<(live[here]>GRADIENT_UNREACHABLE&&fresh[here]<=GRADIENT_UNREACHABLE)<<','<<q[2]<<','<<q[3]<<','<<q[4]<<','<<q[1]<<','<<balancing<<'\n';
}
void ResourceGradientProbe::observePlanning(Map& map,GradientFieldKey key,int x,int y,const Uint16* live){const auto tick=map.game->stepCounter;auto*f=sampleField(tick,key);if(!f)return;std::vector<Uint16> fresh;rebuildFresh(map,key,fresh);const auto here=map.coordToIndex(x,y);const bool reachable=fresh[here]>GRADIENT_UNREACHABLE;for(auto&e:f->episodes)if(labelEligible(unsigned(tick-e.tick),e.delay)){++e.planningQueries;e.deferFalseNegative+=e.deferred[here]<=GRADIENT_UNREACHABLE&&reachable;e.deferFalsePositive+=e.deferred[here]>GRADIENT_UNREACHABLE&&!reachable;e.refreshFalseNegative+=e.refreshed[here]<=GRADIENT_UNREACHABLE&&reachable;e.refreshFalsePositive+=e.refreshed[here]>GRADIENT_UNREACHABLE&&!reachable;}planning<<++planningQuery<<','<<tick<<',';identity(planning,key);planning<<','<<(live[here]<=GRADIENT_UNREACHABLE&&reachable)<<','<<(live[here]>GRADIENT_UNREACHABLE&&!reachable)<<','<<live[here]<<','<<fresh[here]<<'\n';}
void ResourceGradientProbe::observe(Map&map,int team,int material,int swim,int x,int y,const Uint16*live,EntityRandom random){observe(map,GradientQueryContext{{GradientFieldFamily::Materials,std::uint8_t(team),std::uint8_t(swim),std::uint8_t(material)},x,y,random},live);}
void ResourceGradientProbe::observePlanning(Map&map,int team,int material,int swim,int x,int y,const Uint16*live){observePlanning(map,{GradientFieldFamily::Materials,std::uint8_t(team),std::uint8_t(swim),std::uint8_t(material)},x,y,live);}
