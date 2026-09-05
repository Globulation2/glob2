#include "../src/World3DPlacement.h"
#include <cassert>
#include <iostream>
#include <algorithm>
using namespace World3DPlacement;
static bool near(float a,float b) { return std::abs(a-b)<.002f; }
int main() {
 // Both native asset bases must face the simulation target in every direction.
 for(bool warrior:{false,true})for(int dx:{-1,0,1})for(int dy:{-1,0,1}) {
  if(!dx&&!dy)continue;
  float a=modelYawDegrees(dx,dy,warrior)*3.14159265f/180;
  float nativeX=warrior?0.f:-1.f,nativeY=warrior?1.f:0.f;
  float x=std::cos(a)*nativeX-std::sin(a)*nativeY;
  float y=std::sin(a)*nativeX+std::cos(a)*nativeY;
  float length=std::sqrt(static_cast<float>(dx*dx+dy*dy));
  assert(near(x,dx/length)&&near(y,dy/length));
 }
 // An eastbound unit moving from tile 9 to tile 10 must remain between
 // those tile centers; a stopped/working unit stays at tile 10's center.
 assert(near(unitAxis(10,1,0,true,0,128,512),305));
 assert(near(unitAxis(10,1,128,true,0,128,512),321));
 assert(near(unitAxis(10,1,255,true,0,128,512),336));
 assert(near(unitAxis(10,1,0,false,0,128,512),336));
 assert(near(unitAxis(10,-1,0,true,0,128,512),368));
 // Interpolation remains local when the simulation destination wraps to 0.
 assert(near(unitAxis(0,1,128,true,127,128,0),33));
 assert(near(unitAxis(127,-1,128,true,127,128,0),32));
 // Artwork sits halfway from the footprint center toward the near edge.
 auto anchor=buildingAnchor(320,640,128,128,0);
 assert(near(anchor.x,384) && near(anchor.y,736));
 auto flag=buildingAnchor(320,640,32,32,0);
 assert(near(flag.x,336) && near(flag.y,664));
 // At every heading, artwork stays inside the box with no sideways drift.
 World3DCamera camera;
 for(int i=0;i<360;i+=5) {
  camera.yaw=i*3.14159265f/180;
  for(float pitch:{.35f,.72f,1.15f})for(float zoom:{.65f,1.45f,3.5f}) {
   camera.pitch=pitch;camera.zoom=zoom;
   auto a=buildingAnchor(320,640,128,64,camera.yaw);
   float sx,sy,centerX=0,centerY=0;
   camera.project(a.x,a.y,0,sx,sy);
   for(float x:{320.f,448.f})for(float y:{640.f,704.f}) {
    float cx,cy;camera.project(x,y,0,cx,cy);centerX+=cx*.25f;centerY+=cy*.25f;
   }
   assert(near(sx,centerX) && sy>centerY);
   // Twice the artwork offset reaches the rectangle boundary exactly.
   const float edgeX=384+2*(a.x-384),edgeY=672+2*(a.y-672);
   assert(edgeX>=320-.002f && edgeX<=448+.002f);
   assert(edgeY>=640-.002f && edgeY<=704+.002f);
   assert(near(edgeX,320)||near(edgeX,448)||near(edgeY,640)||near(edgeY,704));
   float x=unitAxis(11,1,128,true,0,128,512),y=unitAxis(22,-1,128,true,0,128,512);
   float rx,ry;camera.project(x,y,0,sx,sy);camera.unproject(sx,sy,rx,ry);
   assert(near(rx,x) && near(ry,y));
  }
 }
 std::cout<<"16 model headings, unit interpolation, torus seams, building anchors and 648 camera placement checks passed\n";
}
