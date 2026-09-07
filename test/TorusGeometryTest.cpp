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
    Shape shape;
    return {(shape.majorRadius + shape.tubeRadius * std::cos(b)) * std::sin(a),
            shape.tubeRadius * std::sin(b),
            (shape.majorRadius + shape.tubeRadius * std::cos(b)) * std::cos(a)};
}
int main()
{
    // Exercise the surface actually sent to the GPU throughout the morph.
    for (float anchor : {0.f, .25f, .5f, .75f, 1.f})
        for (int u = 0; u <= 20; ++u)
            for (int v = 0; v <= 20; ++v)
            {
                float du = u / 20.f - .5f, dv = v / 20.f - .5f;
                assert(distance(overviewPoint(du, dv, 0, anchor, 1.6f), {du * 8 * pi, dv * 2 * pi * Shape().tubeRadius, 0}) < .00002f);
                assert(distance(overviewPoint(-.5f, dv, 1, anchor, 1.6f), overviewPoint(.5f, dv, 1, anchor, 1.6f)) <
                       .00002f);
                assert(distance(overviewPoint(du, -.5f, 1, anchor, 1.6f), overviewPoint(du, .5f, 1, anchor, 1.6f)) <
                       .00002f);
                for (int r = 0; r < 100; ++r)
                {
                    auto a = overviewPoint(du, dv, r / 100.f, anchor, 1.6f);
                    auto b = overviewPoint(du, dv, (r + 1) / 100.f, anchor, 1.6f);
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
                    assert(distance(focusedPoint(du, dv, 0, v), {du * 8 * pi, dv * 2 * pi * Shape().tubeRadius, 0}) < 0.00002f);
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
    // The ring keeps one attitude: at full roll the camera pitch is the tilt
    // fitting the view whatever the anchor's latitude, and the flat map is level.
    for (int i = 0; i < 1000; ++i)
    {
        float v = float(i) / 1000;
        assert(std::abs(latitude(v, 1) + overviewTilt(v, 1, 1.6f) + fitTilt(1.6f)) < .00001f);
        assert(overviewTilt(v, 0, 1.6f) == 0);
    }
    // Wider views lay the ring flatter, within the range that keeps it readable.
    for (float aspect : {1.0f, 1.6f, 1.78f, 2.4f})
    {
        assert(fitTilt(aspect) >= 0 && fitTilt(aspect) <= pi / 2);
        assert(fitTilt(aspect) <= fitTilt(aspect / 2));
    }
    // At the ring's latitude, navigation cannot change distance, magnification,
    // or produce a lens singularity.
    {
        const float v = .5f;
        assert(hoverDistance(v) == 18);
        assert(std::abs(overviewTilt(v, 1, 1.6f)) <= pi / 3 + .00001f);
        assert(length(overviewPoint(0, 0, 1, v, 1.6f)) < .00001f);
        auto north = overviewPoint(0, .0001f, 1, v, 1.6f);
        assert(north.y > 0);
        for (float du : {-.5f, -.25f, 0.0f, .25f, .5f})
            for (float dv : {-.5f, -.25f, 0.0f, .25f, .5f})
            {
                auto p = overviewPoint(du, dv, 1, v, 1.6f);
                assert(1 - p.z / hoverDistance(v) > .5f);
                assert(distance(overviewPoint(du, dv, 0, v, 1.6f), {du * 8 * pi, dv * 2 * pi * Shape().tubeRadius, 0}) < .00002f);
                assert(distance(p, overviewPoint(du, dv, 1, v + .0001f, 1.6f)) < .02f);
            }
    }
    // Rectangular maps must not stretch a tile in one tangent direction.
    // Compare independently measured edge derivatives, not the radius formula.
    for (float mapAspect : {.125f, .5f, 1.f, 2.f, 8.f})
    {
        Shape shape(mapAspect);
        assert(shape.majorRadius > shape.tubeRadius);
        assert(std::abs(shape.majorRadius + shape.tubeRadius - 4) < .00001f);
        // Match the window silhouette when possible; otherwise take the
        // nearest attainable height without stretching the world itself.
        for (float viewAspect : {1.f, 1.6f, 2.4f})
        {
            float height = 2 * shape.majorRadius * std::sin(fitTilt(viewAspect, shape))
                           + 2 * shape.tubeRadius;
            float target = std::max(2 * shape.tubeRadius, 8 / viewAspect);
            assert(std::abs(height - target) < .00001f);
        }
        float previous = -.5f;
        for (int row = 0; row <= 160; ++row)
        {
            float dv = meshOffset(float(row) / 160, .5f, shape);
            assert(dv >= previous - .00001f);
            assert(std::abs(latitude(.5f + dv, shape) - latitude(0, shape) - row * 2 * pi / 160) < .0001f);
            previous = dv;
        }
        for (int row = 0; row <= 32; ++row)
            for (int col = 0; col <= 8; ++col)
            {
                float u = float(col) / 8 - .5f, v = float(row) / 32 - .5f;
                // The inner wall of a fat torus has very small derivatives.
                // Use a resolvable interval for float positions even on targets
                // without fused multiply-add; tiny differences lose significance.
                const float e = .001f;
                auto dx = subtract(focusedPoint(u + e, v, 1, .5f, shape), focusedPoint(u - e, v, 1, .5f, shape));
                auto dy = subtract(focusedPoint(u, v + e, 1, .5f, shape), focusedPoint(u, v - e, 1, .5f, shape));
                float tileAspect = length(dx) / (mapAspect * length(dy));
                assert(std::abs(tileAspect - 1) < .015f);
                for (float roll : {0.f, .1f, .5f, .9f, 1.f})
                {
                    auto p = overviewPoint(u, v, roll, .5f, 1.6f, shape);
                    assert(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
                    assert(1 - p.z * roll / hoverDistance(.5f) > .25f);
                }
                assert(distance(overviewPoint(-.5f, v, 1, .5f, 1.6f, shape),
                                overviewPoint(.5f, v, 1, .5f, 1.6f, shape)) < .00002f);
                assert(distance(overviewPoint(u, -.5f, 1, .5f, 1.6f, shape),
                                overviewPoint(u, .5f, 1, .5f, 1.6f, shape)) < .00002f);
            }
        // The 2D endpoint still maps one cell to exactly 32 pixels on either axis.
        float sx = 128 * 32 / (8 * pi);
        float sy = sx * verticalScale(.5f, .5f, 0, mapAspect);
        auto p = overviewPoint(1.f / 128, mapAspect / 128, 0, .5f, 1.6f, shape);
        assert(std::abs(p.x * sx - 32) < .0001f && std::abs(p.y * sy - 32) < .0001f);
    }
    std::cout
        << "Planar endpoints, both periodic seams, torus radii continuous finite transition, and "
           "anchored viewport endpoints, map-proportioned ring geometry and locally square tiles, locked screen orientation, and shared "
           "wrapped navigation, and fixed-world hovering camera passed\n";
}
