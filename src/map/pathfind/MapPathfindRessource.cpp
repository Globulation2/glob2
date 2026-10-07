// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "Utilities.h"
#include "Unit.h"
#include "Game.h"
#include "MapInternal.h"
#include "gradient/GradientRuntime.h"
#include <limits>



// Resource pathfinding for units (pathfindResource, pathfindRandom)

bool Map::pathfindResource(int teamNumber, Uint8 resourceType, int swimClass, int x, int y, int *dx, int *dy, bool *stopWork, Building *target, bool withMarkets)
{
	PERF_SCOPE_TIME(PathResource);
	assert(resourceType<MAX_RESOURCES);
	const Uint16 *gradient=getResourceGradient(teamNumber, resourceType, swimClass, withMarkets, target);
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
	if (target)
	{
		// The round-trip gradient may lag behind this one by a few ticks; when
		// it is blocked or stale here, the plain gradient below still leads to
		// a resource.
		const Uint16 *roundTrip=roundTripGradient(target, resourceType, swimClass);
		if (roundTrip && roundTrip[hereIndex]>GRADIENT_UNREACHABLE
			&& directionByGradient(teamMask, swimClass, x, y, roundTrip, dx, dy, true))
			return true;
	}
	if (directionByGradient(teamMask, swimClass, x, y, gradient, dx, dy, true))
		return true;
	return directionByGradient(teamMask, swimClass, x, y, gradient, dx, dy, false);
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
	if ((tiles[x+(y<<wDec)].forbidden)&unit->owner->me)
	{
		if (pathfindForbidden(NULL, unit->owner->teamNumber, unit->swimClass(), x, y, &unit->dx, &unit->dy)
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
		const bool keepInGuardArea = unit->typeNum == WARRIOR
			&& unit->owner->game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing)
			&& (tiles[coordToIndex(x, y)].guardArea & unit->owner->me);
		bool da[8];
		int count=0;
		for (int pass = keepInGuardArea ? 0 : 1; pass < 2 && count == 0; pass++)
		{
			for (int di=0; di<8; di++)
			{
				int tx=(x+tabClose[di][0])&wMask;
				int ty=(y+tabClose[di][1])&hMask;
				if (pass == 0 && !(tiles[coordToIndex(tx, ty)].guardArea & unit->owner->me))
					da[di]=false;
				else if (terrainPropertiesAt(tx,ty).groundHealthQ8 >= 0
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
		int dir=syncRand()%count;
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


bool Map::pathfindTerrainSafety(Unit *unit)
{
    unit->dx=unit->dy=0;
    unit->direction=8;
    const bool air=unit->performance[FLY]>0;
    const int swim=unit->swimClass();
    const Uint32 team=unit->owner->me;
    const bool escapeForbidden=!air && (tiles[coordToIndex(unit->posX,unit->posY)].forbidden & team);
    const auto safe=[&](const TerrainProperties& p) {
        return air ? p.flyable && p.airHealthQ8>=0
            : (p.walkable || (swim>0 && p.swimmable)) && p.groundHealthQ8>=0;
    };
    bool hasSafeTerrain=false;
    for(std::size_t t=0;t<terrainCounts.size();++t)
        if(terrainCounts[t] && safe(terrainRegistry().properties(TerrainType(t)))) {
            hasSafeTerrain=true; break;
        }
    if(!hasSafeTerrain) return false;

    auto& cache=gradientRuntime->safety;
    // Flyers ignore ground obstacles and forbidden paint and share across teams.
    const unsigned key=air ? 0 : 1+(unit->owner->teamNumber*SWIM_CLASS_COUNT+swim)*2+escapeForbidden;
    auto found=cache.fields.find(key);
    if(found==cache.fields.end()) {
        // Retain at least one field even on maps larger than the normal budget.
        const auto capacity=std::max<std::size_t>(1,cache.MaximumBytes/(size*sizeof(Uint32)));
        if(cache.fields.size()>=capacity) {
            auto oldest=cache.fields.begin();
            for(auto it=cache.fields.begin();it!=cache.fields.end();++it)
                if(it->second.used<oldest->second.used) oldest=it;
            cache.fields.erase(oldest);
        }
        found=cache.fields.try_emplace(key).first;
    }
    if(++cache.clock==0) {
        for(auto& [unused,entry] : cache.fields) entry.used=0;
        cache.clock=1;
    }
    auto& field=found->second;
    field.used=cache.clock;
    const Uint64 generation=air ? cache.airGeneration : cache.groundGeneration;
    constexpr Uint32 infinity=std::numeric_limits<Uint32>::max();
    constexpr Uint32 blocked=infinity-1;
    const auto entryCost=[&](std::size_t index,bool diagonal) -> unsigned {
        if(air) {
            const auto cardinal=terrainRegistry().airRouteCost(terrainTypeAt(index));
            return diagonal ? cardinal*14/10 : cardinal;
        }
        const auto cost=terrainRegistry().movement(swim).entries[terrainTypeAt(index)];
        return diagonal ? cost.diagonal : cost.cardinal;
    };
    if(field.costs.size()!=size || field.generation!=generation) {
        field.costs.resize(size);
        auto& costs=field.costs;
        // Static obstacles only. Occupancy is checked below at movement time.
        for(std::size_t i=0;i<size;++i) {
            const auto& p=terrainPropertiesAt(i);
            const auto& tile=tiles[i];
            const bool forbidden=!air && (tile.forbidden & team);
            const bool passable=air ? p.flyable :
                (p.walkable || (swim>0 && p.swimmable)) && tile.building==NOGUID &&
                tile.resource.type==NO_RES_TYPE && (!forbidden || escapeForbidden);
            costs[i]=!passable ? blocked : safe(p) && !forbidden ? 0 : infinity;
        }
        // Reverse multi-source Dijkstra. Seed only hazardous cells beside a safe
        // goal, avoiding queues containing the entire safe part of the map.
        // All compiled edges are positive and <=253, so a 256-bucket ring is safe.
        std::array<std::vector<Uint32>,256> buckets;
        std::size_t pending=0;
        for(std::size_t i=0;i<size;++i) if(costs[i]==infinity) {
            Uint32 best=infinity;
            const int x=i&wMask,y=i>>wDec;
            for(int d=0;d<8;++d) {
                const int dx=tabClose[d][0],dy=tabClose[d][1];
                const auto n=coordToIndex(x+dx,y+dy);
                if(costs[n]==0) best=std::min(best,entryCost(n,dx && dy));
            }
            if(best!=infinity) { costs[i]=best; buckets[best%256].push_back(i); ++pending; }
        }
        for(Uint32 current=0;pending;++current) {
            auto& bucket=buckets[current%256];
            for(const auto i : bucket) {
                --pending;
                if(costs[i]!=current) continue;
                const int x=i&wMask,y=i>>wDec;
                const auto cardinal=entryCost(i,false),diagonal=entryCost(i,true);
                for(int d=0;d<8;++d) {
                    const int dx=tabClose[d][0],dy=tabClose[d][1];
                    const auto n=coordToIndex(x+dx,y+dy);
                    if(costs[n]==blocked) continue;
                    const Uint64 next=Uint64(current)+(dx && dy ? diagonal : cardinal);
                    if(next>=blocked || next>=costs[n]) continue;
                    costs[n]=Uint32(next);
                    buckets[next%256].push_back(n); ++pending;
                }
            }
            bucket.clear();
        }
        field.generation=generation;
        ++cache.builds;
    }
    const auto here=field.costs[coordToIndex(unit->posX,unit->posY)];
    if(here==infinity || here==0) return false;
    Uint64 best=std::numeric_limits<Uint64>::max();
    for(int d=0;d<8;++d) {
        const int dx=tabClose[d][0],dy=tabClose[d][1];
        const int x=(unit->posX+dx)&wMask,y=(unit->posY+dy)&hMask;
        const auto n=coordToIndex(x,y);
        if(field.costs[n]>=here) continue;
        const bool free=air ? isFreeForAirUnit(x,y) : escapeForbidden
            ? isFreeForGroundUnitNoForbidden(x,y,swim>0) : isFreeForGroundUnit(x,y,swim>0,team);
        if(!free) continue;
        const auto score=Uint64(field.costs[n])+entryCost(n,dx && dy);
        if(score<best) { best=score; unit->dx=dx; unit->dy=dy; }
    }
    unit->directionFromDxDy();
    return best!=std::numeric_limits<Uint64>::max();
}
