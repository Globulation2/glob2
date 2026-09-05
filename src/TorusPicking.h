// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GLOB2_TORUS_PICKING_H
#define GLOB2_TORUS_PICKING_H
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace TorusPicking
{
// Homogeneous pixel coordinates are shared with the OpenGL vertex buffer.
struct Vertex
{
    float position[4], color[3], uv[2], normal[3];
};
struct Hit
{
    float u = 0, v = 0, depth = -std::numeric_limits<float>::infinity();
};
inline bool triangle(const Vertex &a, const Vertex &b, const Vertex &c, float x, float y, Hit &hit)
{
    if (a.position[3] <= 0 || b.position[3] <= 0 || c.position[3] <= 0)
        return false;
    const Vertex *vertices[] = {&a, &b, &c};
    float px[3], py[3], iw[3];
    for (int i = 0; i < 3; ++i)
    {
        iw[i] = 1 / vertices[i]->position[3];
        px[i] = vertices[i]->position[0] * iw[i];
        py[i] = vertices[i]->position[1] * iw[i];
    }
    if (x < std::min(px[0], std::min(px[1], px[2])) || x > std::max(px[0], std::max(px[1], px[2])) ||
        y < std::min(py[0], std::min(py[1], py[2])) || y > std::max(py[0], std::max(py[1], py[2])))
        return false;
    float det = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2]);
    if (std::abs(det) < 1e-8f)
        return false;
    float weights[3];
    weights[0] = ((py[1] - py[2]) * (x - px[2]) + (px[2] - px[1]) * (y - py[2])) / det;
    weights[1] = ((py[2] - py[0]) * (x - px[2]) + (px[0] - px[2]) * (y - py[2])) / det;
    weights[2] = 1 - weights[0] - weights[1];
    if (weights[0] < -1e-6f || weights[1] < -1e-6f || weights[2] < -1e-6f)
        return false;
    float depth = 0, reciprocalW = 0, u = 0, v = 0;
    for (int i = 0; i < 3; ++i)
    {
        float q = weights[i] * iw[i];
        depth += q * vertices[i]->position[2];
        reciprocalW += q;
        u += q * vertices[i]->uv[0];
        v += q * vertices[i]->uv[1];
    }
    // glOrtho uses a reversed Z axis here: the largest Z/W is nearest.
    if (depth < hit.depth)
        return false;
    hit = {u / reciprocalW, v / reciprocalW, depth};
    return true;
}
inline bool mesh(const std::vector<Vertex> &vertices, int columns, int rows, float x, float y, Hit &hit)
{
    if (vertices.size() != static_cast<unsigned>((columns + 1) * (rows + 1)))
        return false;
    bool found = false;
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < columns; ++i)
        {
            int a = j * (columns + 1) + i, b = a + columns + 1;
            found = triangle(vertices[a], vertices[b], vertices[a + 1], x, y, hit) || found;
            found = triangle(vertices[a + 1], vertices[b], vertices[b + 1], x, y, hit) || found;
        }
    return found;
}
inline int worldPixel(float coordinate, int origin, int size)
{
    int pixel = static_cast<int>(std::floor((origin + coordinate * size) * 32));
    return (pixel % (size * 32) + size * 32) % (size * 32);
}
} // namespace TorusPicking
#endif
