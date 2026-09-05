// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/TorusGeometry.h"
#include <cassert>
#include <iostream>
using namespace TorusGeometry;
float distance(Point a, Point b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
int main()
{
    assert(distance(point(0, 0, 0), {-4 * pi, -pi * tubeRadius(1), 0}) < 0.0001f);
    assert(distance(point(1, 1, 0), {4 * pi, pi * tubeRadius(1), 0}) < 0.0001f);
    for (int i = 0; i <= 100; ++i)
    {
        float t = i / 100.0f;
        assert(distance(point(0, t, 1), point(1, t, 1)) < 0.0001f);
        assert(distance(point(t, 0, 1), point(t, 1, 1)) < 0.0001f);
        for (int j = 0; j <= 100; ++j)
        {
            auto p = point(t, j / 100.0f, 1);
            float radial = std::sqrt(p.x * p.x + p.z * p.z) - (4 - tubeRadius(1));
            assert(std::abs(radial * radial + p.y * p.y - tubeRadius(1) * tubeRadius(1)) < 0.0001f);
            for (int k = 0; k < 100; ++k)
            {
                auto a = point(t, j / 100.0f, k / 100.0f), b = point(t, j / 100.0f, (k + 1) / 100.0f);
                assert(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z));
                assert(distance(a, b) < 1.5f);
            }
        }
    }
    for (int w : {64, 128, 256, 512})
        for (int h : {64, 128, 256, 512})
            for (int vx : {0, w - 1, 17})
            {
                int vy = h - 3, width = 1119, height = 799;
                auto f = mapFocus(w, h, vx, vy, width, height);
                float aspect = float(w) / h;
                auto anchor = point(f.u, f.v, 0, aspect);
                for (int x : {0, 560, 1118})
                    for (int y : {16, 408, 798})
                    {
                        float u = (((vx - f.originX) & (w - 1)) + x / 32.0f) / w;
                        float v = (((vy - f.originY) & (h - 1)) + y / 32.0f) / h;
                        auto p = point(u, v, 0, aspect);
                        float px = width * 0.5f + (p.x - anchor.x) * w * 32 / (8 * pi);
                        float py = (height + 16) * 0.5f +
                                   (p.y - anchor.y) * w * 32 / (8 * pi) * verticalScale(f.u, f.v, 0, aspect);
                        assert(std::abs(px - x) < 0.003f && std::abs(py - y) < 0.003f);
                    }
            }
    for (float aspect : {0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f})
    {
        for (int j = 0; j <= 100; ++j)
        {
            float v = j / 100.0f;
            assert(distance(point(0, v, 1, aspect), point(1, v, 1, aspect)) < 0.0001f);
            assert(distance(point(v, 0, 1, aspect), point(v, 1, 1, aspect)) < 0.0001f);
            auto p = point(0.3f, v, 1, aspect);
            float radial = std::sqrt(p.x * p.x + p.z * p.z) - 3;
            assert(std::abs(radial * radial + p.y * p.y - 1) < 0.0001f);
            float mapped = meshV(v, aspect);
            assert(mapped >= 0 && mapped <= 1);
            if (j)
                assert(mapped >= meshV((j - 1) / 100.0f, aspect));
        }
    }
    for (float aspect : {0.25f, 1.0f, 4.0f})
        for (float roll : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f})
            for (float u : {0.5f, 0.505f})
                for (float v : {0.5f, 0.509f})
                {
                    const float e = 0.001f;
                    auto camera = lockedCamera(u, v, roll, aspect);
                    auto du = rotate(subtract(point(u + e, v, roll, aspect), point(u - e, v, roll, aspect)),
                                     camera);
                    auto dv = rotate(subtract(point(u, v + e, roll, aspect), point(u, v - e, roll, aspect)),
                                     camera);
                    assert(du.x > 0 && dv.y > 0);
                    assert(std::abs(du.y) / length(du) < 0.002f && std::abs(du.z) / length(du) < 0.002f);
                    assert(std::abs(dv.x) / length(dv) < 0.002f && std::abs(dv.z) / length(dv) < 0.002f);
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
                    auto expected =
                        rotate(subtract(point(u + du, v + dv, 1), point(u, v, 1)), lockedCamera(u, v, 1, 1));
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
                        auto dx = subtract(focusedPoint(.001f, 0, roll, v), focusedPoint(-.001f, 0, roll, v));
                        auto dy = subtract(focusedPoint(0, .001f, roll, v), focusedPoint(0, -.001f, roll, v));
                        assert(dx.x > 0 && dy.y > 0);
                        assert(std::abs(dx.y) + std::abs(dx.z) < .00002f);
                        assert(std::abs(dy.x) + std::abs(dy.z) < .00002f);
                    }
                }
    // Stars must project like distant world geometry: same handedness, lens,
    // camera rotation, inner-wall tilt and zoom. Points behind the eye vanish.
    Point sky;
    assert(!projectSkyDirection({0,0,1}, {0,0}, 1, 80,80,18,sky));
    assert(!projectSkyDirection({0,0,-1}, {0,0}, 0, 80,80,18,sky));
    for (float yaw : {-.7f, 0.f, .7f})
        for (float pitch : {-.5f, 0.f, .5f})
            for (float roll : {.1f,.5f,1.f}) {
                Point worldDirection = {.12f,.08f,-1};
                CameraAngles camera = {yaw,pitch};
                assert(projectSkyDirection(worldDirection,camera,roll,80,65,18,sky));
                auto view=rotate(worldDirection,camera);
                float far=1000000000;
                float w=1-view.z*far*roll/18;
                assert(std::abs(sky.x-view.x*far*80/w)<.2f);
                assert(std::abs(sky.y-view.y*far*65/w)<.2f);
                Point zoomed;
                assert(projectSkyDirection(worldDirection,camera,roll,160,130,18,zoomed));
                assert(distance(zoomed,{sky.x*2,sky.y*2,0})<.001f);
            }
    // Turning toward screen-right makes fixed stars move left; pitch toward
    // screen-down makes them move up in our downward-positive screen space.
    assert(projectSkyDirection({0,0,-1},{.1f,0},1,80,80,18,sky) && sky.x<0);
    assert(projectSkyDirection({0,0,-1},{0,-.1f},1,80,80,18,sky) && sky.y<0);
    // Camera response is frame-rate independent, monotonic, and crosses map
    // seams by the shortest arc instead of rotating through a whole world.
    float fine = 0, coarse = 0;
    for (int i=0; i<100; ++i) fine=follow(fine,.25f,.01f);
    for (int i=0; i<25; ++i) coarse=follow(coarse,.25f,.04f);
    assert(std::abs(fine-coarse)<.000001f);
    assert(follow(.99f,.01f,.04f,true)>.99f);
    assert(follow(.01f,.99f,.04f,true)<.01f);
    assert(follow(1,2,.04f)>1 && follow(1,2,.04f)<2);
    // The inner-wall correction must never stall or reverse the orbit.
    for (int i=0; i<1000; ++i)
        for (float roll : {0.f,.5f,1.f}) {
            float v=float(i)/1000, step=.0001f;
            float a=latitude(v,1)+overviewTilt(v,roll);
            float b=latitude(v+step,1)+overviewTilt(v+step,roll);
            float slope=(b-a)/(step*2*pi);
            assert(slope>0.47f && slope<1.53f);
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
    std::cout << "Planar endpoints, both periodic seams, torus radii continuous finite transition, and "
                 "anchored viewport endpoints, uniform ring geometry, locked screen orientation, and shared "
                 "wrapped navigation, and fixed-world hovering camera passed\n";
}
