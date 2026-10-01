// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "../src/MapRenderGeometry.h"
#include "../src/DynamicClouds.h"
#include <limits>
#include <set>
#include <utility>

TEST_SUITE("MapRenderGeometry")
{
TEST_CASE("whole-world buildings; seam overhang and repeated viewport copies")
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
                    REQUIRE(copies.size() == 1);
                    REQUIRE(copies.count({x * 32, y * 32}) == 1);
                }
    std::set<std::pair<int, int>> seam;
    MapRenderGeometry::wrappedCopies(0, 0, -32, -64, 64, 64, 1024, 1024, 1024, 1024,
        [&](int x, int y) { seam.emplace(x, y); });
    REQUIRE((seam == std::set<std::pair<int, int>>{{0, 0}, {1024, 0}, {0, 1024}, {1024, 1024}}));
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
                REQUIRE(actual == expected);
            }
    MESSAGE("Whole-world buildings, seam overhang and repeated viewport copies passed");
}
}

TEST_SUITE("MapRenderGeometry")
{
TEST_CASE("cloud detail follows camera zoom without changing normal detail")
{
    CHECK(DynamicClouds::gridLimitForZoom(256, 256, 16, 1.0) == 0);
    CHECK(DynamicClouds::gridLimitForZoom(256, 256, 16, 3.0) == 0);
    CHECK(DynamicClouds::gridLimitForZoom(256, 256, 16, .500001) == 0);
    CHECK(DynamicClouds::gridLimitForZoom(256, 256, 16, .5) == 256);
    CHECK(DynamicClouds::gridLimitForZoom(256, 256, 16, .25) == 128);
    CHECK(DynamicClouds::gridLimitForZoom(256, 256, 16, 1120.0/8192) == 128);
    CHECK(DynamicClouds::gridLimitForZoom(512, 64, 16, .25) == 256);
    CHECK(DynamicClouds::gridLimitForZoom(64, 512, 16, .25) == 256);
    CHECK(DynamicClouds::gridLimitForZoom(256, 256, 0, .25) == 2048);
    CHECK(DynamicClouds::gridLimitForZoom(256, 256, 16, 0) == 0);
    CHECK(DynamicClouds::gridLimitForZoom(256, 256, 16,
        std::numeric_limits<double>::quiet_NaN()) == 0);
}
}
