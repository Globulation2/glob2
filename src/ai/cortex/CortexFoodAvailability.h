// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include <vector>

namespace Cortex
{
namespace food_queries
{
// Keep the original ordered rings, including aliases when the radius exceeds
// a torus dimension. Lookup owns wrapping and the actual-stock predicate.
template<class Lookup> int nearest(int x,int y,int cap,Lookup available)
{
    for(int radius=0;radius<=cap;++radius) {
        if(radius==0) { if(available(x,y)) return 0; continue; }
        for(int dx=-radius;dx<radius;++dx) if(available(x+dx,y-radius)) return radius;
        for(int dy=-radius;dy<radius;++dy) if(available(x+radius,y+dy)) return radius;
        for(int dx=radius;dx>-radius;--dx) if(available(x+dx,y+radius)) return radius;
        for(int dy=radius;dy>-radius;--dy) if(available(x-radius,y+dy)) return radius;
    }
    return -1;
}
template<class Lookup> bool anyWithin(int x,int y,int w,int h,int distance,Lookup available)
{
    for(int dy=-distance;dy<h+distance;++dy)
        for(int dx=-distance;dx<w+distance;++dx)
            if(available(x+dx,y+dy)) return true;
    return false;
}
}

// Owned by one read-only placement pass, never retained by the AI or Map.
// Presence means positive Food stock, including secondary yields. Deliberately
// ignores forbidden paint, fog, and mobility like the direct placement queries.
class FoodAvailabilityView
{
    int width,maskX,maskY;
    std::vector<unsigned char> available;
public:
    explicit FoodAvailabilityView(const MapState::View& world)
        : width(world.width),maskX(width-1),maskY(world.height-1),available(size_t(width)*world.height)
    {
        for(size_t index=0;index<available.size();++index)
            available[index]=MapState::hasMaterial(world,index,MaterialId::Food);
    }
    bool at(int x,int y) const { return available[(y&maskY)*width+(x&maskX)]!=0; }
    int nearestDistance(int x,int y,int cap) const
    { return food_queries::nearest(x,y,cap,[&](int px,int py) { return at(px,py); }); }
    bool anyWithin(int x,int y,int w,int h,int distance) const
    { return food_queries::anyWithin(x,y,w,h,distance,[&](int px,int py) { return at(px,py); }); }
};
}
