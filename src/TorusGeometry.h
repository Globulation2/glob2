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
// Classic ring geometry and uniform texture coordinates. Map aspect ratio
// does not change the shape or compress artwork toward the inner rim.
constexpr float defaultTilt = -0.65f;
inline float tubeRadius(float) { return 1; }
inline float latitude(float v, float, float roll = 1)
{
    return (v - 0.5f) * 2 * pi + defaultTilt * smooth(roll);
}
inline float meshV(float row, float) { return row; }
inline Point point(float u, float v, float roll, float aspect = 1)
{
    float minor = smooth(roll / 0.85f), major = smooth(roll), tube = tubeRadius(aspect);
    float theta = latitude(v, aspect, roll);
    Point p = {(u - 0.5f) * 8 * pi, tube * theta, 0};
    if (minor > 0.000001f)
    {
        p.y = tube * std::sin(theta * minor) / minor;
        float half = std::sin(theta * minor / 2);
        p.z = -2 * tube * half * half / minor;
    }
    if (major > 0.000001f)
    {
        float a = (u - 0.5f) * 2 * pi * major, r = 4 / major;
        p.x = (r + p.z) * std::sin(a);
        float half = std::sin(a / 2);
        p.z = -2 * r * half * half + p.z * std::cos(a) + 4 * major;
    }
    return p;
}
struct CameraAngles
{
    float yaw, pitch;
};
inline CameraAngles lockedCamera(float u, float v, float roll, float aspect)
{
    return {-(u - 0.5f) * 2 * pi * smooth(roll), latitude(v, aspect, roll) * smooth(roll / 0.85f)};
}
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
inline Point focusedPoint(float du, float dv, float roll, float anchorV)
{
    float minor = smooth(roll / 0.85f), major = smooth(roll);
    float phi = latitude(anchorV, 1), arc = dv * 2 * pi;
    float y = arc * std::cos(phi), z = -arc * std::sin(phi);
    if (minor > 0.000001f)
    {
        float half = arc * minor / 2, chord = 2 * std::sin(half) / minor;
        y = chord * std::cos(phi + half);
        z = -chord * std::sin(phi + half);
    }
    float x = du * 8 * pi;
    if (major > 0.000001f)
    {
        float a = du * 2 * pi * major, radius = (4 + (std::cos(phi) - 1) * major) / major;
        x = (radius + z) * std::sin(a);
        float half = std::sin(a / 2);
        z = z * std::cos(a) - 2 * radius * half * half;
    }
    return {x, y * std::cos(phi) - z * std::sin(phi), y * std::sin(phi) + z * std::cos(phi)};
}
// Keep the overview outside the ring at a fixed distance. Tilt over its
// inner wall rather than diving into the hole or changing the lens.
inline float hoverDistance(float) { return 18.0f; }
inline float overviewTilt(float anchorV, float roll)
{
    float inner = std::max(0.0f, -std::cos(latitude(anchorV, 1)));
    return -(pi / 3) * smooth(inner) * smooth(roll);
}
inline Point overviewPoint(float du, float dv, float roll, float anchorV)
{
    return rotate(focusedPoint(du, dv, roll, anchorV), {0, overviewTilt(anchorV, roll)});
}
// A direction at infinity uses the same camera rotation and perspective as
// the world mesh, without camera translation. The camera looks along -Z.
inline bool projectSkyDirection(Point direction, CameraAngles camera, float roll,
                                float sx, float sy, float distance, Point &screen)
{
    if (roll <= 0) return false;
    Point view = rotate(direction, camera);
    float depth = -view.z;
    if (depth <= 0.0001f) return false;
    screen = {view.x * sx * distance / (roll * depth),
              view.y * sy * distance / (roll * depth), 0};
    return true;
}
// Scale direct manipulation by the projected tangent at the current focus.
// A narrower inner circumference must not make the map suddenly drag faster.
inline Point surfaceDrag(float dx, float dy, float sx, float sy, float roll, float anchorV)
{
    const float e = .0001f;
    float tx = overviewPoint(e, 0, roll, anchorV).x / e;
    float ty = overviewPoint(0, e, roll, anchorV).y / e;
    return {-dx / std::max(1.0f, sx * tx), -dy / std::max(1.0f, sy * ty), 0};
}
// Fade from native 2D map dimensions to the uniform 3D ring mapping.
inline float verticalScale(float, float, float roll, float aspect)
{
    return std::exp((1 - smooth(roll)) * std::log(4 / aspect));
}
} // namespace TorusGeometry
#endif
