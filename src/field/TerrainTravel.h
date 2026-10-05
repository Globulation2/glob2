// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainMovementCosts.h"
#include "map/TerrainRegistry.h"
#include <limits>
#include <queue>
#include <vector>

#ifndef GLOB2_GRADIENT_BENCH_EVENT
#define GLOB2_GRADIENT_BENCH_EVENT(name, count) ((void)0)
#endif

namespace field
{
// Persisted IDs. Geometric fields deliberately ignore movement speed/access.
enum class TerrainTravel : unsigned char { Geometric=0, Walk=1, Swim=2, Fly=3 };
constexpr bool validTerrainTravel(unsigned value) { return value<=unsigned(TerrainTravel::Fly); }
constexpr bool terrainTravelAllowed(const TerrainProperties& p,TerrainTravel mode)
{
    return mode==TerrainTravel::Geometric || (mode==TerrainTravel::Fly?p.flyable:
        p.walkable || (mode==TerrainTravel::Swim && p.swimmable));
}
constexpr unsigned terrainTravelCost(const TerrainProperties& p,TerrainTravel mode)
{
    // Strategic swim fields describe an amphibious cohort, not one unit's
    // swimming ratio. Use the engine's equal walk/swim profile plus terrain.
    return gradient_kernel::scaledTerrainStep(GRADIENT_STEP,mode==TerrainTravel::Fly?p.airSpeedQ8:p.groundSpeedQ8);
}

// Air movement has its own speed table, so its queue bound cannot be inferred
// from the ground-gradient ring. Keep every positive edge below one full turn.
inline constexpr unsigned TERRAIN_TRAVEL_BUCKETS = 64;
static_assert(gradient_kernel::scaledTerrainStep(GRADIENT_STEP,64)<TERRAIN_TRAVEL_BUCKETS);

// AI distance encoding: 0 unreached, 1 obstacle, 2 source. Expand in wide
// gradient units, then publish rounded-up neutral-terrain tile equivalents.
// These strategic fields retain their historical Chebyshev metric: all eight
// neighbor steps cost the same before terrain scaling. A distant road must not
// change diagonal distances on routes that never touch modified terrain.
// Saturate only the public short distance; never the queue's ordering key.
template<class Values,class TerrainAt>
void expandTerrainTravel(Values& values,int width,int height,TerrainTravel mode,TerrainAt terrainAt,const TerrainRegistry& registry=*TerrainRegistry::builtins())
{
    constexpr unsigned infinity=std::numeric_limits<unsigned>::max();
    std::vector<unsigned> costs(values.size(),infinity);
    // These distance-only fields have bounded positive integer edge costs.
    // A circular bucket queue avoids heap comparisons without changing the
    // wide distances or the final rounded public encoding. All queues and
    // scratch distances belong to this call; no mutable cache is shared.
    std::array<GradientBucket,TERRAIN_TRAVEL_BUCKETS> buckets;
    std::size_t pending=0;
    for(std::size_t i=0;i<values.size();++i) if(values[i]==2)
    { costs[i]=0;buckets[0].push(static_cast<std::uint32_t>(i));++pending; }
    for(unsigned cost=0;pending;++cost)
    {
        auto& bucket=buckets[cost%TERRAIN_TRAVEL_BUCKETS];
        const auto count=bucket.size;
        GLOB2_GRADIENT_BENCH_EVENT(occupied, count!=0);
        GLOB2_GRADIENT_BENCH_EVENT(popped, count);
        for(std::size_t at=0;at<count;++at)
        {
            const auto index=bucket.cells[at];
            if(cost!=costs[index]) { GLOB2_GRADIENT_BENCH_EVENT(stale, 1); continue; }
            // This is a reverse field: index is the destination of the forward
            // move from next. Charge entry to index, not entry to next.
            const unsigned candidate=cost+(mode==TerrainTravel::Fly?registry.airCost(terrainAt(index)):registry.movement(3).entries[terrainAt(index)].cardinal);
            const int x=index%width,y=index/width;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            {
                if(!dx&&!dy)continue;
                const int ux=x+dx,uy=y+dy;
                const int nx=ux<0?width-1:ux==width?0:ux,ny=uy<0?height-1:uy==height?0:uy;
                const int next=ny*width+nx;
                if(values[next]==1)continue;
                if(candidate<costs[next])
                {
                    costs[next]=candidate;
                    // The positive step is smaller than the ring, so appends
                    // cannot target or reallocate the bucket being traversed.
                    buckets[candidate%TERRAIN_TRAVEL_BUCKETS].push(next);
                    ++pending;
                    GLOB2_GRADIENT_BENCH_EVENT(relaxations, 1);
                }
            }
        }
        pending-=count;
        bucket.clear();
    }
    for(std::size_t i=0;i<values.size();++i) if(costs[i]!=infinity)
        values[i]=2+std::min(32765u,(costs[i]+GRADIENT_STEP-1)/GRADIENT_STEP);
}
// Forward, byte-valued reach/influence fields retain 0 obstacles and 1 floor.
// Keep sub-tile costs in the queue so two half-cost road steps consume one
// strength unit. Only publish rounded strength after the full expansion.
template<class Value,class TerrainAt>
void expandTerrainInfluence(Value* values,int width,int height,TerrainAt terrainAt,const TerrainRegistry& registry=*TerrainRegistry::builtins())
{
    const int size=width*height;
    std::vector<unsigned> strength(size);
    using Entry=std::pair<unsigned,int>;
    std::priority_queue<Entry> queue;
    for(int i=0;i<size;++i)if(values[i]>1)
    {strength[i]=unsigned(values[i])*GRADIENT_STEP;queue.emplace(strength[i],i);}
    while(!queue.empty())
    {
        const auto [remaining,index]=queue.top();queue.pop();
        if(strength[index]!=remaining)continue;
        const int x=index%width,y=index/width;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
        {
            if(!dx&&!dy)continue;
            const int ux=x+dx,uy=y+dy;
            const int nx=ux<0?width-1:ux==width?0:ux,ny=uy<0?height-1:uy==height?0:uy;
            const int next=ny*width+nx;
            if(!values[next])continue;
            const unsigned step=registry.movement(3).entries[terrainAt(next)].cardinal;
            if(remaining<=step+GRADIENT_STEP)continue;
            const unsigned candidate=remaining-step;
            if(candidate>strength[next]){strength[next]=candidate;queue.emplace(candidate,next);}
        }
    }
    for(int i=0;i<size;++i)if(strength[i])values[i]=Value(strength[i]/GRADIENT_STEP);
}

}
