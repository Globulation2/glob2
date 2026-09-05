// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/TorusPicking.h"
#include "../src/TorusGeometry.h"
#include <cassert>
#include <iostream>
using namespace TorusPicking;
int main()
{
    Vertex a = {{0, 0, 0, 1}, {1, 1, 1}, {0, 0}};
    Vertex b = {{20, 0, 2, 2}, {1, 1, 1}, {1, 0}};
    Vertex c = {{0, 40, 4, 4}, {1, 1, 1}, {0, 1}};
    Hit hit;
    assert(triangle(a, b, c, 2, 3, hit));
    assert(std::abs(hit.u - 0.1f / 0.675f) < 1e-6f);
    assert(std::abs(hit.v - 0.075f / 0.675f) < 1e-6f);
    assert(!triangle(a, b, c, 20, 20, hit));
    Vertex back = a;
    back.position[2] = -10;
    assert(!triangle(back, b, c, 2, 3, hit));
    assert(worldPixel(-0.01f, 0, 128) == 4055);
    assert(worldPixel(1.0f, 7, 128) == 224);
    assert(worldPixel(0.5f, 0, 128) == 2048);
    // Project actual rendered triangles, then recover their surface UVs at
    // interior points. This covers both winding orders and perspective as the
    // map unfolds, including the inner side of the torus.
    for (float roll : {0.f, .2f, .5f, .8f, 1.f})
        for (float focus : {0.f, .25f, .5f, .75f, 1.f})
        {
            std::vector<Vertex> vertices;
            const int n = 40;
            for (int j = 0; j <= n; ++j)
                for (int i = 0; i <= n; ++i)
                {
                    float u = float(i) / n - .5f, v = float(j) / n - .5f;
                    auto p = TorusGeometry::overviewPoint(u, v, roll, focus);
                    float w = 1 - p.z * roll / 18;
                    vertices.push_back(
                        {{500 * w + p.x * 80, 400 * w + p.y * 80, p.z * 80, w}, {1, 1, 1}, {u, -v}});
                }
            Hit center;
            assert(mesh(vertices, n, n, 500, 400, center));
            assert(std::isfinite(center.u) && std::isfinite(center.v));
            Hit outside;
            assert(!mesh(vertices, n, n, -10000, -10000, outside));
            for (int index : {87, 330, 899, 1200})
            {
                const Vertex &v0 = vertices[index], &v1 = vertices[index + 1],
                             &v2 = vertices[index + n + 1];
                float x = (v0.position[0] + v1.position[0] + v2.position[0]) / 3;
                float y = (v0.position[1] + v1.position[1] + v2.position[1]) / 3;
                float w = (v0.position[3] + v1.position[3] + v2.position[3]) / 3;
                Hit recovered;
                assert(triangle(v0, v1, v2, x / w, y / w, recovered));
                assert(std::abs(recovered.u - (v0.uv[0] + v1.uv[0] + v2.uv[0]) / 3) < .0001f);
                assert(std::abs(recovered.v - (v0.uv[1] + v1.uv[1] + v2.uv[1]) / 3) < .0001f);
                Hit front;
                assert(mesh(vertices, n, n, x / w, y / w, front));
                assert(front.depth >= recovered.depth - .001f);
            }
        }
    std::cout
        << "Perspective picking, nearest surface, empty sky, seams and unfolding round trips passed\n";
}
