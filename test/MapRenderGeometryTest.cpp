// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/MapRenderGeometry.h"
#include <cassert>
#include <iostream>
#include <set>
#include <utility>

int main()
{
    // The old 16-tile viewport margin placed these buildings entirely outside
    // a whole-world capture. Every tile must appear at its canonical position.
    for (int w : {32, 64, 128})
        for (int h : {32, 64, 128})
            for (int x = 0; x < w; ++x)
                for (int y = 0; y < h; ++y)
                {
                    std::set<std::pair<int, int>> copies;
                    MapRenderGeometry::wrappedCopies(x * 32, y * 32, 0, 0, 32, 32,
                        w * 32, h * 32, w * 32, h * 32,
                        [&](int px, int py) { copies.emplace(px, py); });
                    assert(copies.size() == 1);
                    assert(copies.count({x * 32, y * 32}) == 1);
                }
    std::set<std::pair<int, int>> seam;
    MapRenderGeometry::wrappedCopies(0, 0, -32, -64, 64, 64, 1024, 1024, 1024, 1024,
        [&](int x, int y) { seam.emplace(x, y); });
    assert((seam == std::set<std::pair<int, int>>{{0, 0}, {1024, 0}, {0, 1024}, {1024, 1024}}));
    // Compare small viewports and repeated maps against brute-force intersection.
    for (int x : {-32, 0, 448, 992})
        for (int y : {-64, 0, 256, 992})
            for (int viewW : {320, 1024, 2400})
            {
                std::set<std::pair<int, int>> actual, expected;
                MapRenderGeometry::wrappedCopies(x, y, -32, -64, 96, 64, 1024, 1024, viewW, 768,
                    [&](int px, int py) { actual.emplace(px, py); });
                for (int dy = -3; dy <= 3; ++dy)
                    for (int dx = -3; dx <= 3; ++dx)
                    {
                        int px = x + dx * 1024, py = y + dy * 1024;
                        if (px - 32 < viewW && py - 64 < 768 && px + 96 > 0 && py + 64 > 0)
                            expected.emplace(px, py);
                    }
                assert(actual == expected);
            }
    std::cout << "Whole-world buildings, seam overhang and repeated viewport copies passed\n";
}
