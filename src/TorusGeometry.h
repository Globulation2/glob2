#ifndef GLOB2_TORUS_GEOMETRY_H
#define GLOB2_TORUS_GEOMETRY_H
#include <algorithm>
#include <cmath>
namespace TorusGeometry {
constexpr float pi = 3.14159265358979323846f;
struct Point { float x, y, z; };
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
// Isothermal coordinates: sqrt((R/r)^2-1) = map width / map height.
// This preserves local tile aspect ratios; area necessarily varies on a torus.
constexpr float defaultTilt = -0.95f;
inline float radiusRatio(float aspect) { return std::sqrt(1+aspect*aspect); }
inline float tubeRadius(float aspect) { return 4/(radiusRatio(aspect)+1); }
inline float revealTilt(float aspect) {
    // Thicker rings need a higher viewing angle to see into their aperture.
    return -std::min(1.53f,std::max(0.85f,std::asin(1/radiusRatio(aspect))+0.20f));
}
inline float phase(float aspect) {
    float q=radiusRatio(aspect);
    return std::atan2(std::sqrt(q-1)*std::sin(revealTilt(aspect)/2),
                      std::sqrt(q+1)*std::cos(revealTilt(aspect)/2))/pi;
}
inline float latitude(float v, float aspect, float roll=1) {
    float q=radiusRatio(aspect), t=pi*(v-0.5f+phase(aspect));
    float target=2*std::atan2(std::sqrt(q+1)*std::sin(t), std::sqrt(q-1)*std::cos(t));
    float flat=(v-0.5f)*2*pi;
    return flat+(target-flat)*smooth(roll);
}
// Even angular tessellation keeps the silhouette smooth despite uneven UVs.
inline float meshV(float row, float aspect) {
    float q=radiusRatio(aspect), angle=latitude(0,aspect)+2*pi*row;
    float t=std::atan2(std::sqrt(q-1)*std::sin(angle/2),
                       std::sqrt(q+1)*std::cos(angle/2))/pi;
    return std::max(0.0f,std::min(1.0f,t-phase(aspect)+0.5f));
}
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
// Match the two tangent scales at the camera anchor during the morph as well.
inline float verticalScale(float u, float v, float roll, float aspect) {
    if (roll==0) return 4/(tubeRadius(aspect)*aspect);
    if (roll==1) return 1;
    const float e=0.001f;
    Point du=subtract(point(u+e,v,roll,aspect),point(u-e,v,roll,aspect));
    Point dv=subtract(point(u,v+e,roll,aspect),point(u,v-e,roll,aspect));
    return length(du)/(aspect*length(dv));
}
}
#endif
