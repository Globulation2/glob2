// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "Utilities.h"
#include "Unit.h"
#include "Game.h"
#include "MapInternal.h"
#include "gradient/GradientRuntime.h"
#include "field/RuntimeTerrainGradient.h"
#include <stdexcept>



// Material pathfinding and idle ground movement.

bool Map::pathfindMaterial(EntityRandom& random, int teamNumber, Uint8 resourceType, int swimClass, int x, int y, int *dx, int *dy, bool *stopWork, Building *target, bool withMarkets)
{
	PERF_SCOPE_TIME(PathResource);
	assert(resourceType<MaterialCount);
    ResourceExperiment::ReadScope resourceRead(ResourceExperiment::Reader::Movement);
	const Uint16 *gradient=getMaterialGradientSlot(teamNumber, resourceType, swimClass, withMarkets, target);
    if(gradient==materialGradients[teamNumber][resourceType][swimClass]) {
        auto** slot=&materialGradients[teamNumber][resourceType][swimClass];
        gradientRuntime->resolveResource(slot,coordToIndex(x,y),ResourceExperiment::Reader::Movement);
        if(gradient[coordToIndex(x,y)]==GRADIENT_FORBIDDEN)
            for(const auto& d:tabClose)gradientRuntime->resolveResource(slot,coordToIndex(x+d[0],y+d[1]),ResourceExperiment::Reader::Movement);
        if(gradientRuntime->adaptiveEnabled || gradientRuntime->refreshModel.active() || (gradientRuntime->qualityProbe && gradientRuntime->qualityProbe->study.is_open()))
            gradientRuntime->demand[teamNumber][resourceType][swimClass].queried(game->stepCounter,gradient[coordToIndex(x,y)]);
    }
	const auto before=random;
	const auto move=[&]() {
	size_t hereIndex=coordToIndex(x, y);
	Uint16 here=gradient[hereIndex];
	Uint32 teamMask=Team::teamNumberToMask(teamNumber);
	if (here==GRADIENT_FORBIDDEN)
	{
		*stopWork=true;
		return pathfindForbidden(gradient, teamNumber, swimClass, x, y, dx, dy);
	}
	if (here==GRADIENT_UNREACHABLE)
	{
		*stopWork=true;
		return false;
	}
	*stopWork=false;
	if (here==GRADIENT_AT_GOAL)
		return false; // standing where the resource was: it is gone, wander until the gradient is rebuilt
	if (directionByGradient(random, teamMask, swimClass, x, y, gradient, dx, dy, true))
		return true;
	return directionByGradient(random, teamMask, swimClass, x, y, gradient, dx, dy, false);
	};
	const bool moved=move();
    if (gradientRuntime->demandEnabled && gradient==materialGradients[teamNumber][resourceType][swimClass]
        && gradient[coordToIndex(x,y)]!=GRADIENT_FORBIDDEN)
        gradientRuntime->demand[teamNumber][resourceType][swimClass].requested=true;
	if (auto& probe=gradientRuntime->qualityProbe)
		if (gradient==materialGradients[teamNumber][resourceType][swimClass] &&
			gradient[coordToIndex(x,y)]!=GRADIENT_FORBIDDEN)
			probe->observe(*this,teamNumber,resourceType,swimClass,x,y,gradient,before,
				moved,moved ? *dx : 0,moved ? *dy : 0);
	return moved;
}

void GradientRuntime::initializeAdaptiveSources(const Map& map)
{
    if(!adaptiveEnabled || adaptiveSourcesInitialized)return;
    for(size_t i=0;i<size_t(map.getW())*map.getH();++i)for(unsigned bits=map.materialMaskAt(i);bits;bits&=bits-1)++adaptiveSources[std::countr_zero(bits)].count;
    adaptiveSourcesInitialized=true;
}

ResourceGradientProbe::ResourceGradientProbe(const char* path):output(path)
{
    if (!output) throw std::runtime_error("Cannot open resource gradient probe output");
    if(const char* path=std::getenv("GLOB2_RESOURCE_STUDY_CSV")) {
        study.open(path);if(!study)throw std::runtime_error("Cannot open refresh study");
        study<<"tick,key,complete,queries,defer_flags,refresh_flags,defer_extra,refresh_extra,defer_missed,refresh_missed,defer_goal,refresh_goal,age,q32,q128,near32,mean_cost32,goals32,unreachable32,material,swim,class_units,resource_delta,occupancy_delta,area_delta,visibility_delta,last_query_age,last_query_gap,source_count,source_capture_count,source_added,source_removed,source_capture_known,source_candidate_count,refresh_cpu_ns\n";
    }
    output << "query,tick,window,window_elapsed,team,resource,swim,x,y,policy,publication_delay,capture,age,"
        "fresh_here,old_here,false_unreachable,false_reachable,depleted_goal,"
        "missed_move,extra_cost,scored_move,unreachable_step,left_fresh_goal,goals_added,goals_removed,obstacles_changed,sources_added,sources_removed\n";
}

void ResourceGradientProbe::writeEpisode(const Episode& e,bool complete)
{
    if(!study)return;
    study<<e.tick<<','<<e.key<<','<<complete<<','<<e.queries;
    for(size_t i=0;i<e.defer.size();++i)study<<','<<e.defer[i]<<','<<e.refresh[i];
    for(int f:e.features)study<<','<<f;study<<','<<e.refreshCpuNs<<'\n';
}
void ResourceGradientProbe::finishStudy(unsigned tick)
{
    if(!study)return;
    const unsigned observedEnd=window==UINT32_MAX?0:std::min(tick,window*4096+1024);
    for(auto& [key,field]:fields) {
        for(const auto& episode:field.episodes)writeEpisode(episode,episode.tick+32<=observedEnd);
        field.episodes.clear();
    }
    study.flush();
}

void ResourceGradientProbe::initializeSources(Map& map)
{
    if(sourcesInitialized || !study)return;
    for(size_t i=0;i<map.size;++i) {
        const auto mask=map.materialMaskAt(i);
        for(unsigned m=0;m<MaterialCount;++m)if(mask&(1u<<m))++sourceCounts[m].count;
    }
    sourcesInitialized=true;
}
void ResourceGradientProbe::stockChanged(unsigned before,unsigned after)
{
    if(!sourcesInitialized)return;
    for(unsigned m=0;m<MaterialCount;++m)if((before^after)&(1u<<m)) {
        auto& c=sourceCounts[m];
        if(after&(1u<<m)){++c.count;++c.added;}
        else {if(!c.count)throw std::logic_error("Source counter underflow");--c.count;++c.removed;}
    }
}
void ResourceGradientProbe::captureSources(const std::uint16_t* cells,int resource)
{
    if(sourcesInitialized)sourceCaptures[cells]=sourceCounts[resource];
    else sourceCaptures.erase(cells);
}
void ResourceGradientProbe::verifySources(Map& map)
{
    std::array<std::uint64_t,MaterialCount> actual{};
    for(size_t i=0;i<map.size;++i){const auto mask=map.materialMaskAt(i);for(unsigned m=0;m<MaterialCount;++m)if(mask&(1u<<m))++actual[m];}
    for(unsigned m=0;m<MaterialCount;++m)if(actual[m]!=sourceCounts[m].count)throw std::logic_error("Diagnostic source counts drifted");
    if(map.gradientRuntime->adaptiveSourcesInitialized)for(unsigned m=0;m<MaterialCount;++m)if(actual[m]!=map.gradientRuntime->adaptiveSources[m].count)throw std::logic_error("Scheduler source counters drifted");
}

void ResourceGradientProbe::stageEpisodes(Map& map)
{
    if(!study)return;
    const auto tick=map.game->stepCounter;
    initializeSources(map);if(tick%4096==0)verifySources(map);
    if(tick%4096+32>1024 || tick/4096!=window)return;
    for(auto& [key,field]:fields) {
        const int swim=key%SWIM_CLASS_COUNT,resource=(key/SWIM_CLASS_COUNT)%MaterialCount,team=key/(SWIM_CLASS_COUNT*MaterialCount);
        if(field.lastQuery==UINT32_MAX || tick-field.lastQuery>128 || tick-field.firstTick<256 || (field.lastEpisode && tick-field.lastEpisode<32)
            || !map.periodicGradientClassActive(team,swim) || !map.hasMaterialSourceSlot(resource))continue;
        if(tick-map.gradientRuntime->demand[team][resource][swim].lastSubmitted<8)continue;
        const auto* old=map.materialGradients[team][resource][swim];if(!old)continue;
        Episode e;e.tick=tick;e.key=key;
        const int units=map.game->teams[team]->stats.measurements().groundUnitsByMovementClass[swim];
        e.features=map.gradientRuntime->demand[team][resource][swim].features(tick,resource,swim,units,map.snapshotGenerations());
        const auto& demand=map.gradientRuntime->demand[team][resource][swim];
        const auto current=sourceCounts[resource];const auto found=sourceCaptures.find(old);
        const bool known=found!=sourceCaptures.end();const auto captured=known?found->second:current;
        const auto bounded=[](std::uint64_t v){return int(std::min<std::uint64_t>(1000000,v));};
        for(int f:{bounded(tick-demand.lastQueryTick),bounded(demand.lastQueryGap),bounded(current.count),bounded(captured.count),bounded(current.added-captured.added),bounded(current.removed-captured.removed),int(known),int(map.cellView().materialSourceCounts[resource])})e.features.push_back(f);
        if(known && map.gradientRuntime->adaptiveEnabled) {
            const auto inputs=map.gradientRuntime->adaptiveInputs(tick,resource,swim,team);
            const auto den=std::max<std::uint64_t>(1,captured.count);
            const std::array<AdaptiveResource::I,7> expected{e.features[0],e.features[1],e.features[2],e.features[18],e.features[19],AdaptiveResource::I(std::min<std::uint64_t>(1000000,(current.added-captured.added)*4096/den)),AdaptiveResource::I(std::min<std::uint64_t>(1000000,(current.removed-captured.removed)*4096/den))};
            if(inputs!=expected)throw std::logic_error("Adaptive inputs differ from diagnostic capture counters");
        }
        e.oldCells.assign(old,old+map.size);e.newCells.resize(map.size);
        const auto oracleStarted=BudgetExperiment::threadCpuNs();
        map.seedMaterialGradientDirect(team,resource,swim,e.newCells.data(),nullptr);
        gradient_kernel::CPUSelectionScope cpu(gradient_kernel::Backend::CPU);
        if(!map.hasTerrainMovementModifiers()) {
            const auto water=map.frozenWaterSnapshot();
            gradient_kernel::propagateField(e.newCells.data(),swim,Map::GRADIENT_COST_LIMIT,{map.getW(),map.getH()},scratch,[p=water->data()](size_t i){return p[i]!=0;});
        } else {
            const auto profiles=map.frozenTerrainMovementSnapshot(swim);
            gradient_kernel::propagateTerrainProfiles(e.newCells.data(),swim,Map::GRADIENT_COST_LIMIT,{map.getW(),map.getH()},scratch,profiles->data(),profiles->movement,map.terrainQueueBuckets());
        }
        e.refreshCpuNs=BudgetExperiment::threadCpuNs()-oracleStarted;
        field.episodes.push_back(std::move(e));field.lastEpisode=tick;
    }
}

void ResourceGradientProbe::observe(Map& map, int team, int resource, int swim,
    int x, int y, const std::uint16_t* live, EntityRandom random, bool moved, int dx, int dy)
{
    const auto tick=map.game->stepCounter;
    // A 1024-tick window every 4096 ticks bounds diagnostic cost. Observe
    // queried keys in a rotating deterministic stratum, with independent history.
    if (tick%4096>=1024) return;
    const auto currentWindow=tick/4096;
    if (window!=currentWindow) {
        finishStudy(tick);window=currentWindow; fields.clear();
    }
    const int key=(team*MaterialCount+resource)*SWIM_CLASS_COUNT+swim;
    const unsigned hash=(unsigned(key)^window*2246822519u)*2654435761u;
    if (((hash^(hash>>16))%8)!=0) return;
    auto [fieldIt,inserted]=fields.try_emplace(key);
    auto& field=fieldIt->second;
    if (inserted) field.firstTick=tick;
    const auto firstTick=field.firstTick;
    ++query;
    Reference fresh;
    fresh.tick=tick; fresh.seeds.resize(map.size);
    // Direct seeding bypasses live caches; private propagation bypasses worker
    // queues and backend sessions. Rebuild per query: earlier units in this
    // very tick may already have consumed a source.
    map.seedMaterialGradientDirect(team,resource,swim,fresh.seeds.data(),nullptr);
    fresh.sources.resize(map.size);
    for (size_t i=0;i<map.size;++i) fresh.sources[i]=map.materialAmountAtSlot(i,resource)>0;
    fresh.cells=fresh.seeds;
    gradient_kernel::CPUSelectionScope cpu(gradient_kernel::Backend::CPU);
    if (!map.hasTerrainMovementModifiers()) {
        const auto water=map.frozenWaterSnapshot();
        gradient_kernel::propagateField(fresh.cells.data(),swim,Map::GRADIENT_COST_LIMIT,
            {map.getW(),map.getH()},scratch,[p=water->data()](size_t i) { return p[i]!=0; });
    } else {
        const auto profiles=map.frozenTerrainMovementSnapshot(swim);
        gradient_kernel::propagateTerrainProfiles(fresh.cells.data(),swim,Map::GRADIENT_COST_LIMIT,
            {map.getW(),map.getH()},scratch,profiles->data(),profiles->movement,map.terrainQueueBuckets());
    }
    const auto here=map.coordToIndex(x,y);
    // Forbidden-area escape uses a separate field and is outside this probe.
    if (fresh.cells[here]==GRADIENT_FORBIDDEN) return;
    const auto mask=Team::teamNumberToMask(team);
    const auto choose=[&](const std::uint16_t* g,int& sx,int& sy) {
        if (g[here]<=GRADIENT_UNREACHABLE || g[here]==GRADIENT_AT_GOAL) return false;
        auto copy=random;
        return map.directionByGradient(copy,mask,swim,x,y,g,&sx,&sy,true) ||
            map.directionByGradient(copy,mask,swim,x,y,g,&sx,&sy,false);
    };
    int fx=0,fy=0;
    const bool freshMoves=choose(fresh.cells.data(),fx,fy);
    // Use the fresh selector with the same RNG state as the reference. In
    // congestion it deliberately prefers uphill steps over equal-value
    // sidesteps; comparing against arbitrary neighbors would misattribute that
    // existing behavior to staleness. Equal-cost alternative steps score zero.
    const auto freshNext=map.coordToIndex(x+fx,y+fy);
    const int best=freshMoves ? int(fresh.cells[freshNext])-map.stepCost(fx,fy,freshNext,swim) : -1000000;
    const auto emit=[&](int policy,int delay,int capture,int age,const std::uint16_t* old,const Reference* ref,
        bool oldMoves,int sx,int sy) {
        const auto a=old[here],b=fresh.cells[here];
        const bool aReach=a>GRADIENT_UNREACHABLE,bReach=b>GRADIENT_UNREACHABLE;
        const auto n=map.coordToIndex(x+sx,y+sy);
        const bool scored=oldMoves && (sx || sy) && bReach &&
            b!=GRADIENT_AT_GOAL && fresh.cells[n]>GRADIENT_UNREACHABLE && best>-1000000;
        const int regret=scored ? std::max(0,best-(int(fresh.cells[n])-map.stepCost(sx,sy,n,swim))) : 0;
        int added=-1,removed=-1,obstacles=-1,sourcesAdded=-1,sourcesRemoved=-1;
        if (ref) {
            added=removed=obstacles=0;
            sourcesAdded=sourcesRemoved=0;
            for (size_t i=0;i<map.size;++i) {
                added+=fresh.seeds[i]==GRADIENT_AT_GOAL && ref->seeds[i]!=GRADIENT_AT_GOAL;
                removed+=fresh.seeds[i]!=GRADIENT_AT_GOAL && ref->seeds[i]==GRADIENT_AT_GOAL;
                obstacles+=(fresh.seeds[i]==GRADIENT_FORBIDDEN)!=(ref->seeds[i]==GRADIENT_FORBIDDEN);
                sourcesAdded+=fresh.sources[i] && !ref->sources[i];
                sourcesRemoved+=!fresh.sources[i] && ref->sources[i];
            }
        }
        output << query << ',' << tick << ',' << window << ',' << tick-firstTick << ','
            << team << ',' << resource << ',' << swim << ',' << x << ',' << y << ','
            << policy << ',' << delay << ',' << capture << ',' << age << ',' << b << ',' << a << ','
            << (!aReach && bReach) << ',' << (aReach && !bReach) << ','
            << (a==GRADIENT_AT_GOAL && b!=GRADIENT_AT_GOAL) << ','
            << (!oldMoves && freshMoves) << ',' << regret << ',' << scored << ','
            << (oldMoves && (sx || sy) && fresh.cells[n]<=GRADIENT_UNREACHABLE) << ','
            << (oldMoves && (sx || sy) && b==GRADIENT_AT_GOAL) << ','
            << added << ',' << removed << ',' << obstacles << ',' << sourcesAdded << ',' << sourcesRemoved << '\n';
    };
    field.lastQuery=tick;
    if(study) {
        while(!field.episodes.empty() && tick>=field.episodes.front().tick+32) {
            writeEpisode(field.episodes.front(),true);field.episodes.pop_front();
        }
        const auto quality=[&](const std::uint16_t* cells) {
            int sx=0,sy=0;const bool oldMoves=choose(cells,sx,sy);
            const auto a=cells[here],b=fresh.cells[here];
            const bool ar=a>GRADIENT_UNREACHABLE,br=b>GRADIENT_UNREACHABLE;
            const auto n=map.coordToIndex(x+sx,y+sy);
            const bool scored=oldMoves&&(sx||sy)&&br&&b!=GRADIENT_AT_GOAL&&fresh.cells[n]>GRADIENT_UNREACHABLE&&best>-1000000;
            const int regret=scored?std::max(0,best-(int(fresh.cells[n])-map.stepCost(sx,sy,n,swim))):0;
            const bool missed=!oldMoves&&freshMoves,goal=a==GRADIENT_AT_GOAL&&b!=GRADIENT_AT_GOAL;
            const bool flagged=ar!=br||missed||goal||regret>0||(oldMoves&&(sx||sy)&&fresh.cells[n]<=GRADIENT_UNREACHABLE)||(oldMoves&&(sx||sy)&&b==GRADIENT_AT_GOAL);
            return std::array<long long,4>{flagged,regret,missed,goal};
        };
        for(auto& e:field.episodes)if(tick>=e.tick+7) {
            ++e.queries;const auto a=quality(e.oldCells.data()),b=quality(e.newCells.data());
            for(size_t i=0;i<a.size();++i){e.defer[i]+=a[i];e.refresh[i]+=b[i];}
        }

    }
    const auto stamp=captured.find(live);
    emit(0,map.gradientRuntime->pipeline.delayTicks(),-1,stamp==captured.end() ? -1 : int(tick-stamp->second),live,nullptr,moved,dx,dy);
    if(std::getenv("GLOB2_RESOURCE_PROBE_LIVE_ONLY"))return;
    for (size_t i=0;i<intervals.size();++i) {
        auto& ref=field.references[i];
        // Demand-driven fixed intervals: no refresh is needed between queries.
        // These comparisons have zero publication delay; that is deliberate.
        const bool capture=ref.cells.empty() || tick-ref.tick>=intervals[i];
        if (capture) ref=fresh;
        int sx=0,sy=0;
        const bool moves=choose(ref.cells.data(),sx,sy);
        emit(intervals[i],0,capture,tick-ref.tick,ref.cells.data(),&ref,moves,sx,sy);
        auto& lagged=field.delayed[i];
        auto& queue=field.pending[i];
        bool delayedCapture=false;
        if (lagged.cells.empty()) { lagged=fresh; field.lastScheduled[i]=tick; delayedCapture=true; }
        while (!queue.empty() && tick-queue.front().tick>=8) {
            lagged=std::move(queue.front()); queue.pop_front();
        }
        if (tick-field.lastScheduled[i]>=intervals[i]) { queue.push_back(fresh); field.lastScheduled[i]=tick; delayedCapture=true; }
        sx=sy=0;
        const bool delayedMoves=choose(lagged.cells.data(),sx,sy);
        emit(1000+intervals[i],8,delayedCapture,tick-lagged.tick,lagged.cells.data(),&lagged,delayedMoves,sx,sy);
    }
}


void Map::pathfindRandom(Unit *unit)
{
	int x=unit->posX;
	int y=unit->posY;
	if (terrainPropertiesAt(x,y).groundHealthQ8 < 0)
	{
		pathfindTerrainSafety(unit);
		return;
	}
	if ((areaCells[x+(y<<wDec)].forbidden)&unit->owner->me)
	{
		if (unit->findDisplacement([&](int* dx, int* dy) { return pathfindForbidden(NULL, unit->owner->teamNumber, unit->swimClass(), x, y, dx, dy); })
			&& terrainPropertiesAt(x+unit->dx,y+unit->dy).groundHealthQ8 >= 0)
		{
			unit->directionFromDxDy();
		}
		else
		{
			unit->dx=0;
			unit->dy=0;
			unit->direction=8;
		}
	}
	else
	{
		// Guard-area balancing: a warrior on the paint steps onto a free painted
		// neighbour first, so a full area does not empty all at once, and takes
		// the ordinary step when none is free rather than standing still.
		const bool keepInGuardArea = unit->hasCapability(UnitRuntimeTraits::GuardIdle)
			&& unit->owner->game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing)
			&& (areaCells[coordToIndex(x, y)].guard & unit->owner->me);
		bool da[8];
		int count=0;
		for (int pass = keepInGuardArea ? 0 : 1; pass < 2 && count == 0; pass++)
		{
			for (int di=0; di<8; di++)
			{
				int tx=(x+tabClose[di][0])&wMask;
				int ty=(y+tabClose[di][1])&hMask;
				if (pass == 0 && !(areaCells[coordToIndex(tx, ty)].guard & unit->owner->me))
					da[di]=false;
				else if ((unit->performance[WALK] || terrainPropertiesAt(tx,ty).swimmable) && terrainPropertiesAt(tx,ty).groundHealthQ8 >= 0
					&& isFreeForGroundUnit(tx, ty, (unit->performance[SWIM]>0), unit->owner->me))
				{
					da[di]=true;
					count++;
				}
				else
					da[di]=false;
			}
		}
		if (count==0)
		{
			unit->dx=0;
			unit->dy=0;
			unit->direction=8;
			return;
		}
		int dir=unit->entityRandom.nextU32()%count;
		for (int di=0; di<8; di++)
			if (da[di] && dir--==0)
			{
				unit->dx=tabClose[di][0];
				unit->dy=tabClose[di][1];
				unit->direction=di;
				return;
			}
		assert(false);
	}
}
