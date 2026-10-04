// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "../src/TorusTextureTiles.h"
using namespace TorusTextureTiles;
TEST_SUITE("TorusTextureTiles")
{
TEST_CASE("native layout covers each cell once with wrapped padding and respects limits")
{
    for (int w : {64, 128, 256, 512}) for (int h : {64, 128, 512})
        for (int limit : {128, 1024, 2048}) for (int pixels : {32, 16, 1})
        {
            auto tiles = layout(w * 32, h * 32, limit, pixels);
            std::vector<int> covered(w * h);
            for (const auto &tile : tiles)
            {
                REQUIRE(tile.textureW <= limit); REQUIRE(tile.textureH <= limit);
                REQUIRE(tile.textureW == (tile.w / 32 + 2) * pixels);
                REQUIRE(tile.textureH == (tile.h / 32 + 2) * pixels);
                for (int y = tile.y / 32; y < (tile.y + tile.h) / 32; ++y)
                    for (int x = tile.x / 32; x < (tile.x + tile.w) / 32; ++x) ++covered[y * w + x];
            }
            for (int n : covered) REQUIRE(n == 1);
        }
    REQUIRE(layout(2048, 2048, 95, 32).empty());
    REQUIRE(!layout(2048, 2048, 96, 32).empty());
}
TEST_CASE("partition preserves coverage and perspective picking across texture and wrap seams")
{
    // Two triangles with varying homogeneous W, color and normals. Clipping
    // must preserve the original perspective-correct surface and attributes.
    std::vector<Vertex> mesh(4);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x)
    {
        Vertex &v = mesh[y * 2 + x];
        float w = 1 + .3f * x + .2f * y;
        v = {{100.f * x * w, 100.f * y * w, float(x + y), w},
             {float(x), float(y), .7f}, {float(x), float(y)}, {float(x), float(y), 1}};
    }
    for (float u : {-.1f, 0.f, .37f, 1.2f}) for (float v : {-.2f, 0.f, .61f, 1.3f})
    {
        auto tiles = layout(2048, 4096, 1024, 32);
        auto clipped = partition(tiles, mesh, 1, 1, 2048, 4096, u, v);
        double area = 0;
        for (size_t i = 0; i < clipped.size(); i += 3)
        {
            const auto &a = clipped[i], &b = clipped[i+1], &c = clipped[i+2];
            double ax=a.position[0]/a.position[3], ay=a.position[1]/a.position[3];
            double bx=b.position[0]/b.position[3], by=b.position[1]/b.position[3];
            double cx=c.position[0]/c.position[3], cy=c.position[1]/c.position[3];
            area += std::abs((bx-ax)*(cy-ay)-(by-ay)*(cx-ax)) / 2;
        }
        REQUIRE(std::abs(area - 10000) < .02);
        for (const auto &tile : tiles)
            for (int i = tile.first; i < tile.first + tile.count; ++i)
            {
                const auto &vert = clipped[i];
                REQUIRE(vert.uv[0] >= 32.f / (tile.w + 64) - .00001f);
                REQUIRE(vert.uv[0] <= float(tile.w + 32) / (tile.w + 64) + .00001f);
                REQUIRE(vert.uv[1] >= 32.f / (tile.h + 64) - .00001f);
                REQUIRE(vert.uv[1] <= float(tile.h + 32) / (tile.h + 64) + .00001f);
                const float worldU = (vert.uv[0] * (tile.w + 64) + tile.x - 32) / 2048;
                const float worldV = 1 - (tile.y + tile.h + 32 - vert.uv[1] * (tile.h + 64)) / 4096;
                auto periodicError = [](float a, float b) { float d=a-b; return d-std::round(d); };
                REQUIRE(std::abs(periodicError(worldU, vert.color[0] + u)) < .00001f);
                REQUIRE(std::abs(periodicError(worldV, vert.color[1] + v)) < .00001f);
                REQUIRE(vert.color[2] == doctest::Approx(.7f));
                REQUIRE(vert.normal[0] == doctest::Approx(vert.color[0]));
                REQUIRE(vert.normal[1] == doctest::Approx(vert.color[1]));
                REQUIRE(vert.normal[2] == doctest::Approx(1.f));
            }
        for (int y = 7; y < 100; y += 11) for (int x = 3; x < 100; x += 13)
        {
            TorusPicking::Hit expected;
            REQUIRE(TorusPicking::mesh(mesh, 1, 1, x, y, expected));
            bool found = false;
            for (const auto &tile : tiles)
                for (int i = tile.first; i < tile.first + tile.count; i += 3)
                {
                    TorusPicking::Hit hit;
                    if (!TorusPicking::triangle(clipped[i], clipped[i+1], clipped[i+2], x, y, hit)) continue;
                    const float worldU = (hit.u * (tile.w + 64) + tile.x - 32) / 2048;
                    const float worldV = 1 - (tile.y + tile.h + 32 - hit.v * (tile.h + 64)) / 4096;
                    auto delta = [](float a, float b) { float d=a-b; return d-std::round(d); };
                    REQUIRE(std::abs(delta(worldU, expected.u + u)) < .00001f);
                    REQUIRE(std::abs(delta(worldV, expected.v + v)) < .00001f);
                    REQUIRE(std::abs(hit.depth - expected.depth) < .00001f);
                    found = true;
                }
            REQUIRE(found);
        }
    }
}
}
