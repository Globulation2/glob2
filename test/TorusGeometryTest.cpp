// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/TorusGeometry.h"
#include <cassert>
#include <iostream>
using namespace TorusGeometry;
float distance(Point a, Point b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
// Independent, ordinary ring embedding used to check the camera-local chart.
Point worldTorus(float u, float v)
{
    float a = (u - .5f) * 2 * pi, b = latitude(v, 1);
    return {(3 + std::cos(b)) * std::sin(a), std::sin(b), (3 + std::cos(b)) * std::cos(a)};
}
int main()
{
    // Exercise the surface actually sent to the GPU throughout the morph.
    for (float anchor : {0.f, .25f, .5f, .75f, 1.f})
        for (int u = 0; u <= 20; ++u)
            for (int v = 0; v <= 20; ++v)
            {
                float du = u / 20.f - .5f, dv = v / 20.f - .5f;
                assert(distance(overviewPoint(du, dv, 0, anchor), {du * 8 * pi, dv * 2 * pi, 0}) < .00002f);
                assert(distance(overviewPoint(-.5f, dv, 1, anchor), overviewPoint(.5f, dv, 1, anchor)) <
                       .00002f);
                assert(distance(overviewPoint(du, -.5f, 1, anchor), overviewPoint(du, .5f, 1, anchor)) <
                       .00002f);
                for (int r = 0; r < 100; ++r)
                {
                    auto a = overviewPoint(du, dv, r / 100.f, anchor);
                    auto b = overviewPoint(du, dv, (r + 1) / 100.f, anchor);
                    assert(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z));
                    assert(distance(a, b) < 1);
                }
            }
    // Navigation uses the same wrapped tile viewport in both presentations.
    for (int w : {64, 128, 256})
        for (int h : {64, 128, 256})
            for (int bx : {0, w - 1, 17})
                for (int by : {0, h - 1, 23})
                {
                    float du = 0.237f, dv = 0.815f;
                    auto old = destination(bx, by, du, dv, w, h);
                    int wantedX = (old.x + 13) & (w - 1), wantedY = (old.y - 7) & (h - 1);
                    du += float(wrappedDelta(old.x, wantedX, w)) / w;
                    dv += float(wrappedDelta(old.y, wantedY, h)) / h;
                    du -= std::floor(du);
                    dv -= std::floor(dv);
                    auto moved = destination(bx, by, du, dv, w, h);
                    assert(moved.x == wantedX && moved.y == wantedY);
                    du = std::round(du * w) / w;
                    dv = std::round(dv * h) / h;
                    auto flat = destination(bx, by, du, dv, w, h);
                    assert(flat.x == moved.x && flat.y == moved.y);
                    auto focus = mapFocus(w, h, bx, by, 1119, 799);
                    float mapX = focus.originX + (focus.u + du) * w - 1119 / 64.0f;
                    float mapY = focus.originY + (focus.v + dv) * h - (799 + 16) / 64.0f;
                    assert((int(std::round(mapX)) & (w - 1)) == flat.x);
                    assert((int(std::round(mapY)) & (h - 1)) == flat.y);
                }
    // Moving the camera preserves the absolute embedding of every map point,
    // including travel around the tube and across both periodic seams.
    for (float u : {-0.2f, 0.0f, 0.4f, 0.9f, 1.2f})
        for (float v : {-0.3f, 0.0f, 0.25f, 0.5f, 0.85f, 1.3f})
            for (float du : {-0.45f, -0.1f, 0.0f, 0.37f})
                for (float dv : {-0.45f, -0.1f, 0.0f, 0.37f})
                {
                    auto expected = rotate(subtract(worldTorus(u + du, v + dv), worldTorus(u, v)),
                                           {-(u - .5f) * 2 * pi, latitude(v, 1)});
                    assert(distance(focusedPoint(du, dv, 1, v), expected) < 0.00002f);
                    assert(distance(focusedPoint(du, dv, 0, v), {du * 8 * pi, dv * 2 * pi, 0}) < 0.00002f);
                    assert(distance(focusedPoint(du, dv, 1, v), focusedPoint(du, dv, 1, v + 1)) < 0.00002f);
                    for (float roll : {0.0f, 0.1f, 0.4f, 0.7f, 1.0f})
                    {
                        assert(length(focusedPoint(0, 0, roll, v)) < 0.00001f);
                        assert(distance(focusedPoint(du, dv, roll, v), focusedPoint(du, dv, roll, v + 1)) <
                               0.00004f);
                        if (roll < 1)
                            assert(distance(focusedPoint(du, dv, roll, v),
                                            focusedPoint(du, dv, roll + .001f, v)) < .1f);
                        auto dx =
                            subtract(focusedPoint(.001f, 0, roll, v), focusedPoint(-.001f, 0, roll, v));
                        auto dy =
                            subtract(focusedPoint(0, .001f, roll, v), focusedPoint(0, -.001f, roll, v));
                        assert(dx.x > 0 && dy.y > 0);
                        assert(std::abs(dx.y) + std::abs(dx.z) < .00002f);
                        assert(std::abs(dy.x) + std::abs(dy.z) < .00002f);
                    }
                }
    // Stars must project like distant world geometry: same handedness, lens,
    // camera rotation, inner-wall tilt and zoom. Points behind the eye vanish.
    Point sky;
    assert(!projectSkyDirection({0, 0, 1}, {0, 0}, 1, 80, 80, 18, sky));
    assert(!projectSkyDirection({0, 0, -1}, {0, 0}, 0, 80, 80, 18, sky));
    for (float yaw : {-.7f, 0.f, .7f})
        for (float pitch : {-.5f, 0.f, .5f})
            for (float roll : {.1f, .5f, 1.f})
            {
                Point worldDirection = {.12f, .08f, -1};
                CameraAngles camera = {yaw, pitch};
                assert(projectSkyDirection(worldDirection, camera, roll, 80, 65, 18, sky));
                auto view = rotate(worldDirection, camera);
                float far = 1000000000;
                float w = 1 - view.z * far * roll / 18;
                assert(std::abs(sky.x - view.x * far * 80 / w) < .2f);
                assert(std::abs(sky.y - view.y * far * 65 / w) < .2f);
                Point zoomed;
                assert(projectSkyDirection(worldDirection, camera, roll, 160, 130, 18, zoomed));
                assert(distance(zoomed, {sky.x * 2, sky.y * 2, 0}) < .001f);
            }
    // Turning toward screen-right makes fixed stars move left; pitch toward
    // screen-down makes them move up in our downward-positive screen space.
    assert(projectSkyDirection({0, 0, -1}, {.1f, 0}, 1, 80, 80, 18, sky) && sky.x < 0);
    assert(projectSkyDirection({0, 0, -1}, {0, -.1f}, 1, 80, 80, 18, sky) && sky.y < 0);
    // Camera response is frame-rate independent, monotonic, and crosses map
    // seams by the shortest arc instead of rotating through a whole world.
    float fine = 0, coarse = 0;
    for (int i = 0; i < 100; ++i)
        fine = follow(fine, .25f, .01f);
    for (int i = 0; i < 25; ++i)
        coarse = follow(coarse, .25f, .04f);
    assert(std::abs(fine - coarse) < .000001f);
    assert(follow(.99f, .01f, .04f, true) > .99f);
    assert(follow(.01f, .99f, .04f, true) < .01f);
    assert(follow(1, 2, .04f) > 1 && follow(1, 2, .04f) < 2);
    // The inner-wall correction must never stall or reverse the orbit.
    for (int i = 0; i < 1000; ++i)
        for (float roll : {0.f, .5f, 1.f})
        {
            float v = float(i) / 1000, step = .0001f;
            float a = latitude(v, 1) + overviewTilt(v, roll);
            float b = latitude(v + step, 1) + overviewTilt(v + step, roll);
            float slope = (b - a) / (step * 2 * pi);
            assert(slope > 0.47f && slope < 1.53f);
        }
    // Navigation cannot change distance, magnification, or produce a lens
    // singularity. The inner-wall tilt remains periodic and below 90 degrees.
    for (int i = -100; i <= 200; ++i)
    {
        float v = i / 100.0f;
        assert(hoverDistance(v) == 18);
        assert(std::abs(overviewTilt(v, 1) - overviewTilt(v + 1, 1)) < .00001f);
        assert(std::abs(overviewTilt(v, 1)) <= pi / 3 + .00001f);
        assert(length(overviewPoint(0, 0, 1, v)) < .00001f);
        auto north = overviewPoint(0, .0001f, 1, v);
        assert(north.y > 0);
        for (float du : {-.5f, -.25f, 0.0f, .25f, .5f})
            for (float dv : {-.5f, -.25f, 0.0f, .25f, .5f})
            {
                auto p = overviewPoint(du, dv, 1, v);
                assert(1 - p.z / hoverDistance(v) > .5f);
                assert(distance(overviewPoint(du, dv, 0, v), {du * 8 * pi, dv * 2 * pi, 0}) < .00002f);
                assert(distance(p, overviewPoint(du, dv, 1, v + .0001f)) < .02f);
            }
    }
    for (float v : {0.0f, .25f, .5f, .75f, 1.0f})
        for (float roll : {0.0f, .5f, 1.0f})
        {
            auto movement = surfaceDrag(.1f, .1f, 100, 100, roll, v);
            assert(movement.x < 0 && movement.y < 0);
            // Small pointer movements produce matching local screen distances.
            auto px = overviewPoint(movement.x, 0, roll, v);
            auto py = overviewPoint(0, movement.y, roll, v);
            assert(std::abs(px.x * 100 + .1f) < .001f);
            assert(std::abs(py.y * 100 + .1f) < .001f);
        }
    std::cout
        << "Planar endpoints, both periodic seams, torus radii continuous finite transition, and "
           "anchored viewport endpoints, uniform ring geometry, locked screen orientation, and shared "
           "wrapped navigation, and fixed-world hovering camera passed\n";
}
