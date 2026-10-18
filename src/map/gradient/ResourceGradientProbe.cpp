// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceGradientProbe.h"
#include "GradientRuntime.h"
#include "Map.h"
#include "Game.h"
#include "MapInternal.h"
#include "field/RuntimeTerrainGradient.h"
#include <filesystem>
#include <stdexcept>

ResourceGradientProbe::ResourceGradientProbe(const char* path):output(path),
    study(std::filesystem::path(path).parent_path()/"gradient-episodes.csv"),
    planning(std::filesystem::path(path).parent_path()/"gradient-planning-quality.csv")
{
    if (!output || !study || !planning) throw std::runtime_error("Cannot open resource gradient training outputs");
    output<<"query,tick,window,window_elapsed,team,resource,swim,extra_cost,false_unreachable,false_reachable,depleted_goal,missed_move,unreachable_step,left_fresh_goal,flagged\n";
    study<<"tick,key,complete,queries,defer_extra,refresh_extra,age,q32,q128,source_added,source_removed,source_added_frac_q12,source_removed_frac_q12,planning_queries,defer_planning_false_negative,defer_planning_false_positive,refresh_planning_false_negative,refresh_planning_false_positive,behavior_interval,behavior_seed,movement_observed\n";
    planning<<"query,tick,window,team,resource,swim,false_unreachable,false_reachable,live_value,fresh_value\n";
}
ResourceGradientProbe::~ResourceGradientProbe() { finishStudy(lastTick); }
void ResourceGradientProbe::writeEpisode(int key,const Episode& e,bool complete)
{
    study<<e.tick<<','<<key<<','<<complete<<','<<e.queries<<','<<e.deferExtra<<','<<e.refreshExtra;
    for (auto v:e.inputs) study<<','<<v;
    study<<','<<e.planningQueries<<','<<e.deferFalseNegative<<','<<e.deferFalsePositive
        <<','<<e.refreshFalseNegative<<','<<e.refreshFalsePositive<<','<<e.behaviorInterval<<','<<e.behaviorSeed<<','<<e.movementObserved<<'\n';
}
void ResourceGradientProbe::finishStudy(unsigned tick)
{
    for (auto& [key,field]:fields) {
        for (const auto& e:field.episodes) writeEpisode(key,e,unsigned(tick-e.tick)>=32);
        field.episodes.clear();
    }
    study.flush();
}
void ResourceGradientProbe::stageEpisodes(Map& map)
{
    lastTick=map.game->stepCounter;
    // Complete episodes even if the field receives no more queries. This keeps
    // quiet, zero-benefit examples in training rather than selecting busy fields.
    for (auto& [key,field]:fields) {
        while (!field.episodes.empty() && unsigned(lastTick-field.episodes.front().tick)>=32) {
            writeEpisode(key,field.episodes.front(),true);field.episodes.pop_front();
        }
    }
    if (lastTick%4096+32>1024 || lastTick/4096!=window) return;
    for (auto& [key,field]:fields) {
        const int swim=key%SWIM_CLASS_COUNT,resource=(key/SWIM_CLASS_COUNT)%MaterialCount,
            team=key/(MaterialCount*SWIM_CLASS_COUNT);
        if (unsigned(lastTick-field.lastQuery)>128 || unsigned(lastTick-field.firstTick)<256
            || (field.episodeKnown && unsigned(lastTick-field.lastEpisode)<32)
            || !map.periodicGradientClassActive(team,swim)) continue;
        const auto found=map.gradientRuntime->resources.fields.find(key);
        if (found!=map.gradientRuntime->resources.fields.end() && unsigned(lastTick-found->second.submitted)<8) continue;
        const auto* old=map.materialGradients[team][resource][swim];
        if (!old) continue;
        Episode e;e.tick=lastTick;e.delay=map.gradientRuntime->pipeline.delayTicks();
        // Compute from a copy: collecting an example must not advance live
        // rolling counters or alter any other simulation-owned state.
        auto state=found==map.gradientRuntime->resources.fields.end()
            ? ResourceGradientPolicy::Field{} : found->second;
        e.inputs=state.inputs(lastTick,map.gradientRuntime->resources.sources[resource]);
        e.movementObserved=field.movementObserved || state.count128>0;
        if (const auto& behavior=map.gradientRuntime->exploration) {
            e.behaviorInterval=behavior->intervalFor(key);e.behaviorSeed=behavior->seed;
        }
        e.deferred.assign(old,old+map.size);
        rebuildFresh(map,team,resource,swim,e.refreshed);
        field.episodes.push_back(std::move(e));field.lastEpisode=lastTick;field.episodeKnown=true;
    }
}
void ResourceGradientProbe::observe(Map& map,int team,int resource,int swim,int x,int y,
    const std::uint16_t* live,EntityRandom random)
{
    const auto tick=map.game->stepCounter;
    const int key=(team*MaterialCount+resource)*SWIM_CLASS_COUNT+swim;
    auto* sampled=sampleField(tick,key);if (!sampled) return;
    auto& field=*sampled;++query;field.movementObserved=true;
    for (auto& e:field.episodes) e.movementObserved=true;
    std::vector<Uint16> fresh;
    rebuildFresh(map,team,resource,swim,fresh);
    const auto here=map.coordToIndex(x,y);
    // Forbidden-area escape uses a separate field and is outside this probe.
    if (fresh[here]==GRADIENT_FORBIDDEN) return;
    const auto mask=Team::teamNumberToMask(team);
    const auto choose=[&](const std::uint16_t* g,int& sx,int& sy) {
        if (g[here]<=GRADIENT_UNREACHABLE || g[here]==GRADIENT_AT_GOAL) return false;
        auto copy=random;
        return map.directionByGradient(copy,mask,swim,x,y,g,&sx,&sy,true) ||
            map.directionByGradient(copy,mask,swim,x,y,g,&sx,&sy,false);
    };
    int fx=0,fy=0;
    const bool freshMoves=choose(fresh.data(),fx,fy);
    // Use the fresh selector with the same RNG state as the reference. In
    // congestion it deliberately prefers uphill steps over equal-value
    // sidesteps; comparing against arbitrary neighbors would misattribute that
    // existing behavior to staleness. Equal-cost alternative steps score zero.
    const auto freshNext=map.coordToIndex(x+fx,y+fy);
    const int best=freshMoves ? int(fresh[freshNext])-map.stepCost(fx,fy,freshNext,swim) : -1000000;
        const auto quality=[&](const std::uint16_t* cells) {
            int sx=0,sy=0;const bool oldMoves=choose(cells,sx,sy);
            const auto a=cells[here],b=fresh[here];
            const bool ar=a>GRADIENT_UNREACHABLE,br=b>GRADIENT_UNREACHABLE;
            const auto n=map.coordToIndex(x+sx,y+sy);
            const bool scored=oldMoves&&(sx||sy)&&br&&b!=GRADIENT_AT_GOAL&&fresh[n]>GRADIENT_UNREACHABLE&&best>-1000000;
            const int regret=scored?std::max(0,best-(int(fresh[n])-map.stepCost(sx,sy,n,swim))):0;
            const bool missed=!oldMoves&&freshMoves,goal=a==GRADIENT_AT_GOAL&&b!=GRADIENT_AT_GOAL;
            const bool unreachable=oldMoves&&(sx||sy)&&fresh[n]<=GRADIENT_UNREACHABLE;
            const bool leftGoal=oldMoves&&(sx||sy)&&b==GRADIENT_AT_GOAL;
            const bool flagged=ar!=br||missed||goal||regret>0||unreachable||leftGoal;
            return std::array<long long,6>{flagged,regret,missed,goal,unreachable,leftGoal};
        };

    for (auto& e:field.episodes) if (unsigned(tick-e.tick)>=e.delay && unsigned(tick-e.tick)<32) {
        ++e.queries;e.deferExtra+=quality(e.deferred.data())[1];e.refreshExtra+=quality(e.refreshed.data())[1];
    }
    const auto flags=quality(live);const auto a=live[here],b=fresh[here];
    output<<query<<','<<tick<<','<<window<<','<<unsigned(tick-field.firstTick)<<','<<team<<','<<resource<<','<<swim
        <<','<<flags[1]<<','<<(a<=GRADIENT_UNREACHABLE && b>GRADIENT_UNREACHABLE)
        <<','<<(a>GRADIENT_UNREACHABLE && b<=GRADIENT_UNREACHABLE)<<','<<flags[3]<<','<<flags[2]
        <<','<<flags[4]<<','<<flags[5]<<','<<flags[0]<<'\n';
}

ResourceGradientProbe::Field* ResourceGradientProbe::sampleField(unsigned tick,int key)
{
    lastTick=tick;
    if (tick%4096>=1024) return nullptr;
    if (window!=tick/4096) { finishStudy(tick);fields.clear();window=tick/4096; }
    const unsigned hash=(unsigned(key)^window*2246822519u)*2654435761u;
    if (((hash^(hash>>16))%8)!=0) return nullptr;
    auto [it,inserted]=fields.try_emplace(key);
    if (inserted) it->second.firstTick=tick;
    it->second.lastQuery=tick;
    return &it->second;
}

void ResourceGradientProbe::rebuildFresh(Map& map,int team,int resource,int swim,std::vector<std::uint16_t>& fresh)
{
    fresh.resize(map.size);
    // Private oracle: no live cache, worker submission, backend session or RNG.
    // Rebuild per query because earlier decisions can consume sources this tick.
    map.seedMaterialGradientDirect(team,resource,swim,fresh.data(),nullptr);
    gradient_kernel::CPUSelectionScope cpu(gradient_kernel::Backend::CPU);
    if (!map.hasTerrainMovementModifiers()) {
        const auto water=map.frozenWaterSnapshot();
        gradient_kernel::propagateField(fresh.data(),swim,Map::GRADIENT_COST_LIMIT,
            {map.getW(),map.getH()},scratch,[p=water->data()](size_t i) { return p[i]!=0; });
    } else {
        const auto profiles=map.frozenTerrainMovementSnapshot(swim);
        gradient_kernel::propagateTerrainProfiles(fresh.data(),swim,Map::GRADIENT_COST_LIMIT,
            {map.getW(),map.getH()},scratch,profiles->data(),profiles->movement,map.terrainQueueBuckets());
    }
}

void ResourceGradientProbe::observePlanning(Map& map,int team,int resource,int swim,int x,int y,const std::uint16_t* live)
{
    const auto tick=map.game->stepCounter;
    const int key=(team*MaterialCount+resource)*SWIM_CLASS_COUNT+swim;
    auto* field=sampleField(tick,key);if (!field) return;
    std::vector<Uint16> fresh;
    rebuildFresh(map,team,resource,swim,fresh);
    const auto here=map.coordToIndex(x,y);
    const bool reference=fresh[here]>GRADIENT_UNREACHABLE;
    for (auto& e:field->episodes) if (unsigned(tick-e.tick)>=e.delay && unsigned(tick-e.tick)<32) {
        ++e.planningQueries;
        e.deferFalseNegative+=e.deferred[here]<=GRADIENT_UNREACHABLE && reference;
        e.deferFalsePositive+=e.deferred[here]>GRADIENT_UNREACHABLE && !reference;
        e.refreshFalseNegative+=e.refreshed[here]<=GRADIENT_UNREACHABLE && reference;
        e.refreshFalsePositive+=e.refreshed[here]>GRADIENT_UNREACHABLE && !reference;
    }
    planning<<++planningQuery<<','<<tick<<','<<window<<','<<team<<','<<resource<<','<<swim
        <<','<<(live[here]<=GRADIENT_UNREACHABLE && reference)
        <<','<<(live[here]>GRADIENT_UNREACHABLE && !reference)<<','<<live[here]<<','<<fresh[here]<<'\n';
}
