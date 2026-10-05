// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "GradientConstants.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <queue>
#include <tuple>
#include <limits>
#include <vector>

namespace field
{
enum class AirDistanceDirection { FromSource, ToDestination };

// Incremental single-source Dijkstra field for ranking several destinations.
// A query resumes the same frontier, so candidate buildings do not each trigger
// a complete A* search. Disabled instances allocate nothing on uniform maps.
template<class Passable, class EntryCost>
class AirDistanceField
{
    int width, height;
    Passable passable;
    EntryCost entryCost;
    AirDistanceDirection direction;
    std::vector<unsigned> distance;
    std::vector<bool> settled;
    using Entry=std::pair<unsigned,int>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> frontier;
public:
    static constexpr unsigned unreachable=std::numeric_limits<unsigned>::max();
    AirDistanceField(int w,int h,int x,int y,Passable access,EntryCost cost,bool enabled=true,
        AirDistanceDirection orientation=AirDistanceDirection::FromSource)
        : width(w),height(h),passable(access),entryCost(cost),direction(orientation)
    {
        if (!enabled) return;
        distance.assign(w*h,unreachable); settled.assign(w*h,false);
        const auto seed = [&](int sx,int sy) {
            const int index=sx*h+sy;
            if (distance[index]==0) return;
            distance[index]=0; frontier.emplace(0,index);
        };
        if (direction==AirDistanceDirection::FromSource || passable(x,y)) seed(x,y);
        else for (int sy=-1;sy<=1;++sy) for (int sx=-1;sx<=1;++sx)
        {
            if (!sx&&!sy) continue;
            const int nx=(x+sx+w)%w,ny=(y+sy+h)%h;
            if (passable(nx,ny)) seed(nx,ny);
        }
    }
    bool enabled() const { return !distance.empty(); }
    unsigned costTo(int x,int y)
    {
        if (!enabled()) return unreachable;
        if (direction==AirDistanceDirection::ToDestination && !passable(x,y)) return unreachable;
        std::array<int,8> goals{};
        unsigned goalCount=0;
        if (passable(x,y)) goals[goalCount++]=x*height+y;
        else for(int sy=-1;sy<=1;++sy) for(int sx=-1;sx<=1;++sx)
        {
            if (!sx&&!sy) continue;
            const int nx=(x+sx+width)%width,ny=(y+sy+height)%height;
            if(passable(nx,ny)) goals[goalCount++]=nx*height+ny;
        }
        unsigned best=unreachable;
        for(unsigned i=0;i<goalCount;++i) if(settled[goals[i]]) best=std::min(best,distance[goals[i]]);
        if(best!=unreachable || !goalCount) return best;
        while(!frontier.empty())
        {
            const auto [cost,index]=frontier.top(); frontier.pop();
            if(settled[index] || distance[index]!=cost) continue;
            settled[index]=true;
            for(int sy=-1;sy<=1;++sy) for(int sx=-1;sx<=1;++sx)
            {
                if(!sx&&!sy) continue;
                const int ux=index/height+sx,uy=index%height+sy;
                const int nx=ux<0?width-1:ux==width?0:ux;
                const int ny=uy<0?height-1:uy==height?0:uy;
                const int next=nx*height+ny;
                if(settled[next] || !passable(nx,ny)) continue;
                // Reverse arcs u<-v cost entry(v), exactly the forward cost
                // of u->v. Charging entry(u) here reverses speed preferences.
                const unsigned cardinal=direction==AirDistanceDirection::ToDestination
                    ? entryCost(index/height,index%height) : entryCost(nx,ny);
                const unsigned candidate=cost+(sx&&sy?cardinal*GRADIENT_DIAGONAL_STEP/GRADIENT_STEP:cardinal);
                if(candidate<distance[next])
                { distance[next]=candidate; frontier.emplace(candidate,next); }
            }
            // Expand before returning so the next candidate can continue.
            if(std::find(goals.begin(),goals.begin()+goalCount,index)!=goals.begin()+goalCount) return cost;
        }
        return unreachable;
    }
};

// The caller supplies immutable access/cost observations and reusable scratch.
// Coordinates use the existing A* x-major index. A blocked destination is
// approached through its cheapest reachable adjacent cell, never entered.
template<class Node, class Passable, class EntryCost>
bool airRoute(int width, int height, int x, int y, int targetX, int targetY,
    unsigned minimumStep, Node* nodes, std::vector<int>& examined,
    Passable passable, EntryCost entryCost, int* dx, int* dy)
{
    *dx = *dy = 0;
    const int start = x * height + y;
    const bool approach = !passable(targetX, targetY);
    auto distance = [&](int px, int py) {
        const int ax = std::abs(px-targetX), ay = std::abs(py-targetY);
        return std::max(std::min(ax,width-ax),std::min(ay,height-ay));
    };
    auto goal = [&](int px, int py) {
        return approach ? distance(px,py) == 1 : px == targetX && py == targetY;
    };
    if (goal(x,y)) return true;
    using Entry = std::tuple<unsigned,int,unsigned>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> frontier;
    nodes[start] = Node(x,y,0,0,0,0,false);
    examined.push_back(start);
    frontier.emplace(0,start,0);
    bool found = false;
    while (!frontier.empty())
    {
        const auto [estimate,index,cost] = frontier.top(); frontier.pop();
        auto& current = nodes[index];
        if (current.isClosed || current.moveCost != cost) continue;
        current.isClosed = true;
        if (goal(current.x,current.y))
        {
            *dx = current.dx; *dy = current.dy; found = true; break;
        }
        for (int sy=-1; sy<=1; ++sy) for (int sx=-1; sx<=1; ++sx)
        {
            if (!sx && !sy) continue;
            const int ux=current.x+sx, uy=current.y+sy;
            const int nx=ux<0?width-1:ux==width?0:ux;
            const int ny=uy<0?height-1:uy==height?0:uy;
            const int next=nx*height+ny;
            auto& cell=nodes[next];
            if (cell.isClosed || !passable(nx,ny)) continue;
            const unsigned cardinal=entryCost(nx,ny);
            const unsigned candidate=cost+(sx&&sy?cardinal*GRADIENT_DIAGONAL_STEP/GRADIENT_STEP:cardinal);
            if (cell.x != -1 && candidate >= cell.moveCost) continue;
            if (cell.x == -1) examined.push_back(next);
            const unsigned total=candidate+minimumStep*std::max(0,distance(nx,ny)-int(approach));
            cell=Node(nx,ny,index==start?sx:current.dx,index==start?sy:current.dy,candidate,total,false);
            frontier.emplace(total,next,candidate);
        }
    }
    for (const int index : examined) nodes[index]=Node();
    examined.clear();
    return found;
}
}
