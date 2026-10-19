// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>
#include <utility>

// The actual unconstrained-air selector, shared by live units and diagnostic
// alternatives. The caller supplies existing occupancy checks; no RNG is used.
namespace direct_flight {
struct Step {int dx=0,dy=0;};
inline Step bearing(int dx,int dy,int width,int height)
{
    if(dx>width/2)dx-=width;else if(dx<-(width/2))dx+=width;
    if(dy>height/2)dy-=height;else if(dy<-(height/2))dy+=height;
    const auto sign=[](int value){return (value>0)-(value<0);};
    if(100*std::abs(dx)>241*std::abs(dy))return {sign(dx),0};
    if(100*std::abs(dy)>241*std::abs(dx))return {0,sign(dy)};
    return {sign(dx),sign(dy)};
}
template<class Free> Step select(int x,int y,int tx,int ty,int width,int height,Free free)
{
    auto step=bearing(tx-x,ty-y,width,height);
    if(!step.dx&&!step.dy)return step;
    constexpr int direction[3][3]={{0,1,2},{7,8,3},{6,5,4}};
    constexpr Step steps[8]={{-1,-1},{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0}};
    const int heading=direction[step.dy+1][step.dx+1];
    for(int turn:{0,1,7,2,6,3,5,4}) {
        step=steps[(heading+turn)&7];
        if(free(x+step.dx,y+step.dy))return step;
    }
    return {};
}
}
