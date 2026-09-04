#ifndef GLOB2_TORUS_GEOMETRY_H
#define GLOB2_TORUS_GEOMETRY_H
#include <algorithm>
#include <cmath>
namespace TorusGeometry {
constexpr float pi = 3.14159265358979323846f;
struct Point { float x, y, z; };
inline int wrappedDelta(int from, int to, int size) {
    return ((to-from+size/2)&(size-1))-size/2;
}
struct Viewport { int x, y; };
inline Viewport destination(int baseX, int baseY, float du, float dv, int w, int h) {
    return {(baseX+int(std::round(du*w)))&(w-1), (baseY+int(std::round(dv*h)))&(h-1)};
}
struct MapFocus { int originX, originY; float u, v; };
inline MapFocus mapFocus(int mapW, int mapH, int vx, int vy, int width, int height) {
    float cx=width*0.5f, cy=(height+16)*0.5f;
    MapFocus f;
    f.originX=(vx+int(cx/32)-mapW/2)&(mapW-1);
    f.originY=(vy+int(cy/32)-mapH/2)&(mapH-1);
    f.u=(((vx-f.originX)&(mapW-1))+cx/32)/mapW;
    f.v=(((vy-f.originY)&(mapH-1))+cy/32)/mapH;
    return f;
}
inline float smooth(float x) { x = std::max(0.0f, std::min(1.0f, x)); return x*x*(3-2*x); }
// Classic ring geometry and uniform texture coordinates. Map aspect ratio
// does not change the shape or compress artwork toward the inner rim.
constexpr float defaultTilt = -0.65f;
inline float tubeRadius(float) { return 1; }
inline float latitude(float v, float, float roll=1) {
    return (v-0.5f)*2*pi+defaultTilt*smooth(roll);
}
inline float meshV(float row, float) { return row; }
inline Point point(float u, float v, float roll, float aspect=1) {
    float minor=smooth(roll/0.85f), major=smooth(roll), tube=tubeRadius(aspect);
    float theta=latitude(v,aspect,roll);
    Point p={(u-0.5f)*8*pi, tube*theta, 0};
    if (minor>0.000001f) {
        p.y=tube*std::sin(theta*minor)/minor;
        float half=std::sin(theta*minor/2);
        p.z=-2*tube*half*half/minor;
    }
    if (major>0.000001f) {
        float a=(u-0.5f)*2*pi*major, r=4/major;
        p.x=(r+p.z)*std::sin(a);
        float half=std::sin(a/2);
        p.z=-2*r*half*half+p.z*std::cos(a)+4*major;
    }
    return p;
}
struct CameraAngles { float yaw, pitch; };
inline CameraAngles lockedCamera(float u, float v, float roll, float aspect) {
    return {-(u-0.5f)*2*pi*smooth(roll), latitude(v,aspect,roll)*smooth(roll/0.85f)};
}
inline Point rotate(Point p, CameraAngles camera) {
    float x=p.x*std::cos(camera.yaw)+p.z*std::sin(camera.yaw);
    float z=-p.x*std::sin(camera.yaw)+p.z*std::cos(camera.yaw);
    return {x,p.y*std::cos(camera.pitch)-z*std::sin(camera.pitch),
            p.y*std::sin(camera.pitch)+z*std::cos(camera.pitch)};
}
inline float length(Point p) { return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z); }
inline Point subtract(Point a, Point b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
// Fade from native 2D map dimensions to the uniform 3D ring mapping.
inline float verticalScale(float, float, float roll, float aspect) {
    return std::exp((1-smooth(roll))*std::log(4/aspect));
}
}
#endif
