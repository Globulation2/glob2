// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GLOB2_TORUS_GEOMETRY_H
#define GLOB2_TORUS_GEOMETRY_H
#include <algorithm>
#include <cmath>
namespace TorusGeometry
{
constexpr float pi = 3.14159265358979323846f;
struct Point
{
    float x, y, z;
};
inline int wrappedDelta(int from, int to, int size)
{
    return ((to - from + size / 2) & (size - 1)) - size / 2;
}
struct Viewport
{
    int x, y;
};
inline Viewport destination(int baseX, int baseY, float du, float dv, int w, int h)
{
    return {(baseX + int(std::round(du * w))) & (w - 1), (baseY + int(std::round(dv * h))) & (h - 1)};
}
struct MapFocus
{
    int originX, originY;
    float u, v;
};
inline MapFocus mapFocus(int mapW, int mapH, int vx, int vy, int width, int height)
{
    float cx = width * 0.5f, cy = (height + 16) * 0.5f;
    MapFocus f;
    f.originX = (vx + int(cx / 32) - mapW / 2) & (mapW - 1);
    f.originY = (vy + int(cy / 32) - mapH / 2) & (mapH - 1);
    f.u = (((vx - f.originX) & (mapW - 1)) + cx / 32) / mapW;
    f.v = (((vy - f.originY) & (mapH - 1)) + cy / 32) / mapH;
    return f;
}
inline float smooth(float x)
{
    x = std::max(0.0f, std::min(1.0f, x));
    return x * x * (3 - 2 * x);
}
// Follow wrapped map coordinates without taking the long way across a seam.
// Exponential response gives the same settling time at different frame rates.
inline float follow(float current, float target, float dt, bool wrapped = false)
{
    float delta = target - current;
    if (wrapped)
        delta -= std::round(delta);
    return current + delta * -std::expm1(-16.0f * dt);
}
// Isothermal torus coordinates: sqrt((R/r)^2 - 1) = map width / height.
// This makes equal map steps equally long in both tangent directions. Tile
// area varies with curvature, but a rectangular map adds no aspect distortion.
constexpr float defaultTilt = -0.65f;
struct Shape
{
    float aspect, majorRadius, tubeRadius, latitudeScale, phase;
    explicit Shape(float mapAspect = 1) : aspect(mapAspect)
    {
        const float ratio = std::sqrt(1 + aspect * aspect);
        tubeRadius = 4 / (ratio + 1);
        majorRadius = 4 - tubeRadius; // Keep the outer radius and camera range fixed.
        latitudeScale = (ratio + 1) / aspect;
        phase = std::atan2(std::sin(defaultTilt / 2), latitudeScale * std::cos(defaultTilt / 2)) / pi;
    }
};
// Unwrap atan2 so folding and navigation stay continuous across either seam.
inline float latitude(float v, const Shape &shape)
{
    const float t = v - 0.5f + shape.phase;
    const float turns = std::floor(t + 0.5f), angle = pi * (t - turns);
    return 2 * std::atan2(shape.latitudeScale * std::sin(angle), std::cos(angle)) + turns * 2 * pi;
}
inline float latitude(float v, float aspect) { return latitude(v, Shape(aspect)); }
// Even angular spacing keeps fat rings smooth without increasing mesh size.
// The inverse mapping supplies UVs; rendering, clouds and picking share them.
inline float meshOffset(float row, float anchorV, const Shape &shape)
{
    const float angle = latitude(anchorV - 0.5f, shape) + row * 2 * pi;
    const float turns = std::floor((angle + pi) / (2 * pi));
    const float half = angle / 2 - turns * pi;
    const float v = std::atan2(std::sin(half), shape.latitudeScale * std::cos(half)) / pi
                    + turns + 0.5f - shape.phase;
    return std::max(-0.5f, std::min(0.5f, v - anchorV));
}
struct CameraAngles
{
    float yaw, pitch;
};
inline Point rotate(Point p, CameraAngles camera)
{
    float x = p.x * std::cos(camera.yaw) + p.z * std::sin(camera.yaw);
    float z = -p.x * std::sin(camera.yaw) + p.z * std::cos(camera.yaw);
    return {x, p.y * std::cos(camera.pitch) - z * std::sin(camera.pitch),
            p.y * std::sin(camera.pitch) + z * std::cos(camera.pitch)};
}
inline float length(Point p) { return std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z); }
inline Point subtract(Point a, Point b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
// A camera-local chart around a point on a fixed torus. At full roll this
// is exactly the original world surface transformed into the local tangent
// frame; moving the anchor changes the camera, never a tile's place on the ring.
// At zero roll it is the ordinary flat map centered on that same tile.
inline Point focusedPoint(float du, float dv, float roll, float anchorV, const Shape &shape = Shape())
{
    float minor = smooth(roll / 0.85f), major = smooth(roll);
    float phi = latitude(anchorV, shape);
    float arc = dv * 2 * pi;
    arc += (latitude(anchorV + dv, shape) - phi - arc) * smooth(roll);
    float y = shape.tubeRadius * arc * std::cos(phi), z = -shape.tubeRadius * arc * std::sin(phi);
    if (minor > 0.000001f)
    {
        float half = arc * minor / 2, chord = 2 * shape.tubeRadius * std::sin(half) / minor;
        y = chord * std::cos(phi + half);
        z = -chord * std::sin(phi + half);
    }
    float x = du * 8 * pi;
    if (major > 0.000001f)
    {
        float a = du * 2 * pi * major;
        float radius = 4 / major + shape.majorRadius + shape.tubeRadius * std::cos(phi) - 4;
        x = (radius + z) * std::sin(a);
        float half = std::sin(a / 2);
        z = z * std::cos(a) - 2 * radius * half * half;
    }
    return {x, y * std::cos(phi) - z * std::sin(phi), y * std::sin(phi) + z * std::cos(phi)};
}
// Keep the overview outside the ring at a fixed distance.
inline float hoverDistance(float) { return 18.0f; }
// The ring-plane tilt whose silhouette, about 2(R+r) wide and
// 2(R+r) sin t + 2r cos t high, has the aspect ratio of the view.
inline float fitTilt(float aspect, const Shape &shape = Shape())
{
    const float outer = 2 * (shape.majorRadius + shape.tubeRadius), tube = 2 * shape.tubeRadius;
    float target = outer / std::max(0.1f, aspect);
    float t = std::asin(std::min(1.0f, target / std::sqrt(outer * outer + tube * tube)))
              - std::atan2(tube, outer);
    // A fat ring needs a steeper view to keep its inner opening visible.
    float apertureTilt = std::asin(shape.tubeRadius / shape.majorRadius) + 0.08f;
    return std::min(1.55f, std::max(apertureTilt, t));
}
// Pitch the camera so the folded ring lies at the fitting tilt; the flat map stays level.
inline float overviewTilt(float anchorV, float roll, float aspect, const Shape &shape = Shape())
{
    return (-fitTilt(aspect, shape) - latitude(anchorV, shape)) * smooth(roll);
}
inline Point overviewPoint(float du, float dv, float roll, float anchorV, float aspect, const Shape &shape = Shape())
{
    return rotate(focusedPoint(du, dv, roll, anchorV, shape), {0, overviewTilt(anchorV, roll, aspect, shape)});
}
// A direction at infinity uses the same camera rotation and perspective as
// the world mesh, without camera translation. The camera looks along -Z.
inline bool projectSkyDirection(Point direction, CameraAngles camera, float roll, float sx, float sy,
                                float distance, Point &screen)
{
    if (roll <= 0)
        return false;
    Point view = rotate(direction, camera);
    float depth = -view.z;
    if (depth <= 0.0001f)
        return false;
    screen = {view.x * sx * distance / (roll * depth), view.y * sy * distance / (roll * depth), 0};
    return true;
}
// Fade from native 2D dimensions to the locally square 3D surface mapping.
inline float verticalScale(float, float, float roll, float aspect)
{
    return std::exp((1 - smooth(roll)) * std::log(4 / (Shape(aspect).tubeRadius * aspect)));
}
} // namespace TorusGeometry
#endif
