#include "../src/TorusGeometry.h"
#include <cassert>
#include <iostream>
using namespace TorusGeometry;
float distance(Point a, Point b) { return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z)); }
int main() {
    assert(distance(point(0,0,0), {-4*pi,-pi*tubeRadius(1),0}) < 0.0001f);
    assert(distance(point(1,1,0), {4*pi,pi*tubeRadius(1),0}) < 0.0001f);
    for (int i=0;i<=100;++i) {
        float t=i/100.0f;
        assert(distance(point(0,t,1),point(1,t,1)) < 0.0001f);
        assert(distance(point(t,0,1),point(t,1,1)) < 0.0001f);
        for(int j=0;j<=100;++j) {
            auto p=point(t,j/100.0f,1);
            float radial=std::sqrt(p.x*p.x+p.z*p.z)-(4-tubeRadius(1));
            assert(std::abs(radial*radial+p.y*p.y-tubeRadius(1)*tubeRadius(1)) < 0.0001f);
            for (int k=0;k<100;++k) {
                auto a=point(t,j/100.0f,k/100.0f), b=point(t,j/100.0f,(k+1)/100.0f);
                assert(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z));
                assert(distance(a,b)<1.5f);
            }
        }
    }
    for (int w : {64,128,256,512}) for (int h : {64,128,256,512})
    for (int vx : {0,w-1,17}) {
        int vy=h-3, width=1119, height=799;
        auto f=mapFocus(w,h,vx,vy,width,height);
        float aspect=float(w)/h;
        auto anchor=point(f.u,f.v,0,aspect);
        for (int x : {0,560,1118}) for (int y : {16,408,798}) {
            float u=(((vx-f.originX)&(w-1))+x/32.0f)/w;
            float v=(((vy-f.originY)&(h-1))+y/32.0f)/h;
            auto p=point(u,v,0,aspect);
            float px=width*0.5f+(p.x-anchor.x)*w*32/(8*pi);
            float py=(height+16)*0.5f+(p.y-anchor.y)*w*32/(8*pi)*verticalScale(f.u,f.v,0,aspect);
            assert(std::abs(px-x)<0.003f && std::abs(py-y)<0.003f);
        }
    }
    for(float aspect : {0.125f,0.25f,0.5f,1.0f,2.0f,4.0f,8.0f}) {
        for (int j=0;j<=100;++j) {
            float v=j/100.0f;
            assert(distance(point(0,v,1,aspect),point(1,v,1,aspect))<0.0001f);
            assert(distance(point(v,0,1,aspect),point(v,1,1,aspect))<0.0001f);
            if (j>0 && j<100) {
                const float e=0.001f;
                auto du=subtract(point(0.3f+e,v,1,aspect),point(0.3f-e,v,1,aspect));
                auto dv=subtract(point(0.3f,v+e,1,aspect),point(0.3f,v-e,1,aspect));
                assert(std::abs(length(du)/(aspect*length(dv))-1)<0.015f);
                float dot=du.x*dv.x+du.y*dv.y+du.z*dv.z;
                assert(std::abs(dot)/(length(du)*length(dv))<0.01f);
            }
            float mapped=meshV(v,aspect);
            assert(mapped>=0 && mapped<=1);
            if(j) assert(mapped>=meshV((j-1)/100.0f,aspect));
        }
    }
    for(float aspect : {0.25f,1.0f,4.0f})
    for(float roll : {0.0f,0.1f,0.25f,0.5f,0.75f,1.0f})
    for(float u : {0.5f,0.505f}) for(float v : {0.5f,0.509f}) {
        const float e=0.001f;
        auto camera=lockedCamera(u,v,roll,aspect);
        auto du=rotate(subtract(point(u+e,v,roll,aspect),point(u-e,v,roll,aspect)),camera);
        auto dv=rotate(subtract(point(u,v+e,roll,aspect),point(u,v-e,roll,aspect)),camera);
        assert(du.x>0 && dv.y>0);
        assert(std::abs(du.y)/length(du)<0.002f && std::abs(du.z)/length(du)<0.002f);
        assert(std::abs(dv.x)/length(dv)<0.002f && std::abs(dv.z)/length(dv)<0.002f);
    }
    std::cout << "Planar endpoints, both periodic seams, torus radii continuous finite transition, and anchored viewport endpoints, conformal tile proportions, and locked screen orientation passed\n";
}
