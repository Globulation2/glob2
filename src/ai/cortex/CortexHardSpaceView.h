// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Map.h"
#include <vector>

namespace Cortex
{
// One immutable placement pass, only the no-ignoreGid hard-building predicate.
// Discovery, transient units and immediate/virtual placement remain caller checks.
class HardSpaceView
{
    const Map& map;
    int width,maskX,maskY;
    std::vector<unsigned char> states; // 0 unknown, 1 allowed, 2 blocked
public:
    explicit HardSpaceView(const Map& map)
        : map(map),width(map.getW()),maskX(width-1),maskY(map.getH()-1),
          states(size_t(width)*map.getH(),0) {}
    bool at(int x,int y)
    {
        const int nx=x&maskX,ny=y&maskY;
        auto& state=states[size_t(ny)*width+nx];
        if(!state) state=map.isHardSpaceForBuilding(nx,ny) ? 1 : 2;
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
