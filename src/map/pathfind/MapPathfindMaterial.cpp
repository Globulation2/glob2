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
	const Uint16 *gradient=getMaterialGradientSlot(teamNumber, resourceType, swimClass, withMarkets, target);
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
	if (auto& probe=gradientRuntime->qualityProbe)
		if (gradient==materialGradients[teamNumber][resourceType][swimClass] &&
			gradient[coordToIndex(x,y)]!=GRADIENT_FORBIDDEN)
			probe->observe(*this,teamNumber,resourceType,swimClass,x,y,gradient,before,
				moved,moved ? *dx : 0,moved ? *dy : 0);
	return moved;
}

ResourceGradientProbe::ResourceGradientProbe(const char* path):output(path)
{
    if (!output) throw std::runtime_error("Cannot open resource gradient probe output");
    output << "query,tick,window,window_elapsed,team,resource,swim,x,y,policy,publication_delay,capture,age,"
        "fresh_here,old_here,false_unreachable,false_reachable,depleted_goal,"
        "missed_move,extra_cost,scored_move,unreachable_step,left_fresh_goal,goals_added,goals_removed,obstacles_changed,sources_added,sources_removed\n";
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
        window=currentWindow; fields.clear();
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
    const auto stamp=captured.find(live);
    emit(0,map.gradientRuntime->pipeline.delayTicks(),-1,stamp==captured.end() ? -1 : int(tick-stamp->second),live,nullptr,moved,dx,dy);
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
