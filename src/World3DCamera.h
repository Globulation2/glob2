// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GLOB2_WORLD3D_CAMERA_H
#define GLOB2_WORLD3D_CAMERA_H
#include <cmath>
// Orthographic camera in map-pixel units, with Z up. Inversion intersects Z=0.
struct World3DCamera {
 int width=1000,height=720;
 float yaw=0,pitch=.72f,zoom=1.45f;
 float offsetX=0,offsetY=0;
 void project(float x,float y,float z,float& sx,float& sy) const {
  float dx=x-width*.5f-offsetX,dy=y-height*.5f-offsetY,c=std::cos(yaw),s=std::sin(yaw);
  sx=width*.5f+zoom*(c*dx-s*dy);
  sy=height*.5f+zoom*(std::sin(pitch)*(s*dx+c*dy)-std::cos(pitch)*z);
 }
 void unproject(float sx,float sy,float& x,float& y) const {
  float rx=(sx-width*.5f)/zoom,ry=(sy-height*.5f)/(zoom*std::sin(pitch));
  x=width*.5f+offsetX+std::cos(yaw)*rx+std::sin(yaw)*ry;
  y=height*.5f+offsetY-std::sin(yaw)*rx+std::cos(yaw)*ry;
 }
 void screenDeltaToGround(float dx,float dy,float& x,float& y) const {
  float rx=dx/zoom,ry=dy/(zoom*std::sin(pitch));
  x=std::cos(yaw)*rx+std::sin(yaw)*ry;
  y=-std::sin(yaw)*rx+std::cos(yaw)*ry;
 }
 static float wrappedOffset(float x,int viewport,int size,float center) {
  float d=x-viewport;return d-std::floor((d-center+size*.5f)/size)*size;
 }
};
#endif
