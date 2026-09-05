#include "../src/World3DCamera.h"
#include <cassert>
#include <iostream>
int main() {
 World3DCamera camera;
 unsigned checks=0;
 for(int w:{640,1280,2560})for(float angle:{-3.0f,0.0f,.65f,2.9f})
 for(float tilt:{.35f,.72f,1.15f})for(float scale:{.65f,1.45f,3.5f}) {
  camera.width=w;camera.height=800;camera.yaw=angle;camera.pitch=tilt;camera.zoom=scale;
  camera.offsetX=13.25f;camera.offsetY=-11.75f;
  for(float dx:{-320.f,0.f,320.f})for(float dy:{-320.f,0.f,320.f}) {
   float wx,wy,sx,sy,cx,cy;
   camera.screenDeltaToGround(dx,dy,wx,wy);
   camera.project(500,400,0,cx,cy);camera.project(500+wx,400+wy,0,sx,sy);
   assert(std::abs(sx-cx-dx)<.003f && std::abs(sy-cy-dy)<.003f);++checks;
  }
  for(float x:{-1000.f,0.f,16.f,512.f,2500.f})for(float y:{-200.f,0.f,16.f,800.f,3500.f}) {
   float sx,sy,rx,ry;camera.project(x,y,0,sx,sy);camera.unproject(sx,sy,rx,ry);
   assert(std::abs(rx-x)<.003f && std::abs(ry-y)<.003f);++checks;
  }
 }
 // Fractional viewport movement must translate every rendered object by
 // exactly the requested screen displacement, including at oblique angles.
 for(float angle:{0.f,.9f,1.57f,3.14f}) {
  camera.yaw=angle;camera.zoom=1.45f;camera.pitch=.72f;
  float sx,sy,tx,ty,dx,dy;
  camera.project(600,450,0,sx,sy);
  camera.screenDeltaToGround(32,0,dx,dy);
  int tileX=std::lround((camera.offsetX+dx)/32),tileY=std::lround((camera.offsetY+dy)/32);
  camera.offsetX+=dx-tileX*32;camera.offsetY+=dy-tileY*32;
  camera.project(600-tileX*32,450-tileY*32,0,tx,ty);
  assert(std::abs(tx-sx+32)<.003f && std::abs(ty-sy)<.003f);++checks;
 }
 for(int size:{32,64,128,256})for(int viewport:{0,15,31,127,255})for(int x=0;x<size;x++) {
  float d=World3DCamera::wrappedOffset(x,viewport,size,12);
  assert(d>=12-size*.5f && d<12+size*.5f);
  int picked=(static_cast<int>(std::lround(d))+viewport)&(size-1);
  assert(picked==x);++checks;
 }
 std::cout<<checks<<" projection and torus wrap checks passed\n";
}
