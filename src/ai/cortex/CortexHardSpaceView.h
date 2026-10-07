// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include <vector>

namespace Cortex
{
// One immutable placement pass, only the no-ignoreGid hard-building predicate.
// Discovery, transient units and immediate/virtual placement remain caller checks.
class HardSpaceView
{
    const MapState::View& world;
    int width,maskX,maskY;
    std::vector<unsigned char> states; // 0 unknown, 1 allowed, 2 blocked
public:
    explicit HardSpaceView(const MapState::View& world)
        : world(world),width(world.width),maskX(width-1),maskY(world.height-1),
          states(size_t(width)*world.height,0) {}
    bool at(int x,int y)
    {
        const int nx=x&maskX,ny=y&maskY;
        auto& state=states[size_t(ny)*width+nx];
        if(!state) state=MapState::hardSpaceForBuildingAt(world,size_t(ny)*width+nx) ? 1 : 2;
        return state==1;
    }
    bool rectangle(int x,int y,int w,int h)
    {
        // Preserve the engine's row order and early rejection, including seams.
        for(int yi=y;yi<y+h;++yi)
            for(int xi=x;xi<x+w;++xi)
                if(!at(xi,yi)) return false;
        return true;
    }
};
}
