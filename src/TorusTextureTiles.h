// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TorusPicking.h"
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

// CPU-only capture layout and mesh partitioning, shared by native GL and WebGL.
namespace TorusTextureTiles
{
struct Tile
{
    int x, y, w, h; // Interior rectangle in world pixels, relative to capture origin.
    int textureW, textureH;
    unsigned texture = 0;
    int first = 0, count = 0;
};
inline std::vector<Tile> layout(int worldW, int worldH, int limit, int pixelsPerCell)
{
    const int cells = limit / pixelsPerCell - 2;
    if (cells < 1) return {};
    const int span = cells * 32;
    std::vector<Tile> result;
    for (int y = 0; y < worldH; y += span)
        for (int x = 0; x < worldW; x += span)
        {
            const int w = std::min(span, worldW - x), h = std::min(span, worldH - y);
            result.push_back({x, y, w, h, (w / 32 + 2) * pixelsPerCell,
                             (h / 32 + 2) * pixelsPerCell});
        }
    return result;
}
using Vertex = TorusPicking::Vertex;
inline Vertex interpolate(const Vertex &a, const Vertex &b, float t)
{
    Vertex r;
    for (int i = 0; i < 4; ++i) r.position[i] = a.position[i] + t * (b.position[i] - a.position[i]);
    for (int i = 0; i < 3; ++i)
    {
        r.color[i] = a.color[i] + t * (b.color[i] - a.color[i]);
        r.normal[i] = a.normal[i] + t * (b.normal[i] - a.normal[i]);
    }
    for (int i = 0; i < 2; ++i) r.uv[i] = a.uv[i] + t * (b.uv[i] - a.uv[i]);
    return r;
}
// A triangle clipped against a rectangle has at most seven vertices.
struct Polygon { std::array<Vertex, 8> v; int size = 0; };
inline Polygon clip(const Polygon &input, int axis, float boundary, bool lower)
{
    Polygon out;
    if (!input.size) return out;
    Vertex a = input.v[input.size - 1];
    bool insideA = lower ? a.uv[axis] >= boundary : a.uv[axis] <= boundary;
    for (int i = 0; i < input.size; ++i)
    {
        const Vertex &b = input.v[i];
        const bool insideB = lower ? b.uv[axis] >= boundary : b.uv[axis] <= boundary;
        if (insideA != insideB && a.uv[axis] != boundary && b.uv[axis] != boundary)
        {
            Vertex cut = interpolate(a, b, (boundary - a.uv[axis]) / (b.uv[axis] - a.uv[axis]));
            cut.uv[axis] = boundary;
            out.v[out.size++] = cut;
        }
        if (insideB) out.v[out.size++] = b;
        a = b; insideA = insideB;
    }
    return out;
}
inline std::vector<Vertex> partition(std::vector<Tile> &tiles, const std::vector<Vertex> &mesh,
                                    int columns, int rows, int worldW, int worldH,
                                    float offsetU, float offsetV)
{
    std::vector<std::vector<Vertex>> groups(tiles.size());
    // Work in unwrapped world UVs. Every intersecting periodic tile copy clips
    // the original triangle; no vertex-wise wrapping across a seam is allowed.
    auto triangle = [&](int ia, int ib, int ic)
    {
        Polygon tri;
        tri.size = 3;
        const int ids[] = {ia, ib, ic};
        for (int k = 0; k < 3; ++k)
        {
            tri.v[k] = mesh[ids[k]];
            tri.v[k].uv[0] += offsetU;
            tri.v[k].uv[1] += offsetV;
        }
        float lo[2], hi[2];
        for (int axis = 0; axis < 2; ++axis)
        {
            lo[axis] = std::min({tri.v[0].uv[axis], tri.v[1].uv[axis], tri.v[2].uv[axis]});
            hi[axis] = std::max({tri.v[0].uv[axis], tri.v[1].uv[axis], tri.v[2].uv[axis]});
        }
        for (size_t n = 0; n < tiles.size(); ++n)
        {
            const Tile &tile = tiles[n];
            // Texture V is bottom-up, while capture rectangles are top-down.
            const float x0 = float(tile.x) / worldW, x1 = float(tile.x + tile.w) / worldW;
            const float y0 = 1.f - float(tile.y + tile.h) / worldH, y1 = 1.f - float(tile.y) / worldH;
            for (int py = int(std::floor(lo[1] - y1)) + 1; py < hi[1] - y0; ++py)
                for (int px = int(std::floor(lo[0] - x1)) + 1; px < hi[0] - x0; ++px)
                {
                    Polygon p = clip(tri, 0, x0 + px, true);
                    p = clip(p, 0, x1 + px, false);
                    p = clip(p, 1, y0 + py, true);
                    p = clip(p, 1, y1 + py, false);
                    for (int k = 0; k < p.size; ++k)
                    {
                        p.v[k].uv[0] = ((p.v[k].uv[0] - px) * worldW - tile.x + 32) / (tile.w + 64);
                        p.v[k].uv[1] = ((p.v[k].uv[1] - py - y0) * worldH + 32) / (tile.h + 64);
                    }
                    for (int k = 1; k + 1 < p.size; ++k)
                        groups[n].insert(groups[n].end(), {p.v[0], p.v[k], p.v[k + 1]});
                }
        }
    };
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < columns; ++i)
        {
            int a = j * (columns + 1) + i, b = a + columns + 1;
            triangle(a, b, a + 1); triangle(a + 1, b, b + 1);
        }
    std::vector<Vertex> result;
    for (size_t n = 0; n < tiles.size(); ++n)
    {
        tiles[n].first = int(result.size()); tiles[n].count = int(groups[n].size());
        result.insert(result.end(), groups[n].begin(), groups[n].end());
    }
    return result;
}
}
