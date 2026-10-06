// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>

namespace MapGeneration
{
// Shared by colony placement and its resource-distance preview. Dimensions are
// terrain corners, so the right/bottom edges include the footprint's last corner.
struct StartingLayout
{
    int width, height, workerRows;
    StartingLayout(int width, int height, int workers)
        : width(width), height(height), workerRows(std::max(2,(workers+width-1)/width)) {}
    int workerX(int index) const { return index%width; }
    int workerY(int index) const { return -1-index/width; }
    bool clears(int x,int y) const { return x>=0 && x<=width && y>=-workerRows && y<=height; }
};
inline bool touchesStartingFootprint(int x,int y,int left,int top,int width,int height,int maskW,int maskH)
{
    const int dx=(x-left+1)&maskW, dy=(y-top+1)&maskH;
    return dx<=width+1 && dy<=height+1 && (dx==0 || dy==0 || dx==width+1 || dy==height+1);
}
}
