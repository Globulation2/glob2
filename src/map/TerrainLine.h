// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>

// Integer supercover of a pixel segment. A corner crossing tests both incident
// cells, so a shot cannot leak between two touching obstacles. Coordinates may
// be unwrapped; the caller's predicate applies the map's toroidal addressing.
template<class Blocked>
bool terrainSegmentClear(std::int64_t x0, std::int64_t y0,
                         std::int64_t x1, std::int64_t y1, Blocked blocked)
{
    constexpr std::int64_t side = 32;
    const auto cell = [](std::int64_t x) { return x >= 0 ? x / side : -((-x + side - 1) / side); };
    const auto endpointBlocked = [&](std::int64_t px, std::int64_t py) {
        const auto cx=cell(px), cy=cell(py);
        return blocked(cx,cy) || (px%side==0 && blocked(cx-1,cy)) ||
            (py%side==0 && blocked(cx,cy-1)) ||
            (px%side==0 && py%side==0 && blocked(cx-1,cy-1));
    };
    if (endpointBlocked(x0,y0) || endpointBlocked(x1,y1)) return false;
    auto x = cell(x0), y = cell(y0);
    const auto endX = cell(x1), endY = cell(y1);
    const auto dx = std::abs(x1-x0), dy = std::abs(y1-y0);
    const int sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1;
    // A segment travelling exactly along a cell boundary touches both sides.
    const bool verticalBoundary = dx == 0 && x0 % side == 0;
    const bool horizontalBoundary = dy == 0 && y0 % side == 0;
    const auto touchesBlocked = [&](std::int64_t cx, std::int64_t cy) {
        return blocked(cx,cy) || (verticalBoundary && blocked(cx-1,cy)) ||
            (horizontalBoundary && blocked(cx,cy-1)) ||
            (verticalBoundary && horizontalBoundary && blocked(cx-1,cy-1));
    };
    if (touchesBlocked(x,y)) return false;
    auto nextX = sx > 0 ? (x+1)*side-x0 : x0-x*side;
    auto nextY = sy > 0 ? (y+1)*side-y0 : y0-y*side;
    while (x != endX || y != endY)
    {
        if (x == endX) { y += sy; nextY += side; }
        else if (y == endY) { x += sx; nextX += side; }
        else if (nextX*dy < nextY*dx) { x += sx; nextX += side; }
        else if (nextX*dy > nextY*dx) { y += sy; nextY += side; }
        else
        {
            if (touchesBlocked(x+sx,y) || touchesBlocked(x,y+sy)) return false;
            x += sx; y += sy; nextX += side; nextY += side;
        }
        if (touchesBlocked(x,y)) return false;
    }
    return true;
}
