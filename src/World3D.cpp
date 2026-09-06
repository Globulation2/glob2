// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "World3D.h"
#include "World3DCamera.h"
#include "World3DPlacement.h"
#include "Game.h"
#include "Map.h"
#include "Unit.h"
#include "Team.h"
#include "Building.h"
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "GlobalContainer.h"
#include "Ressource.h"
#include "Brush.h"
#include "RessourceType.h"
#include <GraphicContext.h>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <iostream>
#include <SDL_image.h>
#ifdef HAVE_OPENGL
#include <epoxy/gl.h>
#endif
namespace World3D {
static bool on = std::getenv("GLOB2_3D") != NULL;
static const Map* currentMap=NULL;
static World3DCamera camera;
static int& width=camera.width;static int& height=camera.height;
static float& yaw=camera.yaw;static float& pitch=camera.pitch;static float& zoom=camera.zoom;
static Building* hovered=NULL;
bool enabled() { return on && globalContainer && globalContainer->gfx && (globalContainer->gfx->getOptionFlags() & GAGCore::GraphicContext::USEGPU); }
bool active(const Map* map) { return enabled() && currentMap==map; }
void configure(const Map* map,int w,int h) { currentMap=map;width=w;height=h; }
void detach(const Map* map) { if(currentMap==map) {currentMap=NULL;hovered=NULL;camera.offsetX=camera.offsetY=0;} }
Building* hoveredBuilding() { return hovered; }
void zoomBy(int steps) { zoom=std::max(0.65f,std::min(3.5f,zoom*static_cast<float>(std::pow(1.12f,steps)))); }
void panBy(float dx,float dy,int* vx,int* vy) {
 if(dx==0 && dy==0)return;
 float x,y;camera.screenDeltaToGround(dx,dy,x,y);
 x+=camera.offsetX;y+=camera.offsetY;
 int tx=std::lround(x/32),ty=std::lround(y/32);
 *vx+=tx;*vy+=ty;camera.offsetX=x-tx*32;camera.offsetY=y-ty*32;
}
void panFrom(float dx,float dy,int startX,int startY,int* vx,int* vy) {
 float x,y;camera.screenDeltaToGround(dx,dy,x,y);
 int tx=std::lround(x/32),ty=std::lround(y/32);
 *vx=startX+tx;*vy=startY+ty;camera.offsetX=x-tx*32;camera.offsetY=y-ty*32;
}
bool key(SDL_Keysym k) {
 if(k.sym==SDLK_F8 || (k.sym==SDLK_8 && (k.mod&KMOD_CTRL))) {on=!on;return true;}
 if(!enabled()) return false;
 if(k.sym==SDLK_LEFTBRACKET) yaw-=0.15f;
 else if(k.sym==SDLK_RIGHTBRACKET) yaw+=0.15f;
 else if(k.sym==SDLK_PAGEUP) pitch=std::min(1.15f,pitch+0.08f);
 else if(k.sym==SDLK_PAGEDOWN) pitch=std::max(0.35f,pitch-0.08f);
 else if(k.sym==SDLK_HOME && (k.mod&KMOD_CTRL)) {yaw=0;pitch=0.72f;zoom=1.45f;camera.offsetX=camera.offsetY=0;}
 else return false;
 return true;
}
void groundToScreen(float x,float y,float z,float& sx,float& sy) {camera.project(x,y,z,sx,sy);}
void screenToGround(float sx,float sy,float& x,float& y) {camera.unproject(sx,sy,x,y);}
static float relative(float x,int viewport,int size,float center) {return World3DCamera::wrappedOffset(x,viewport,size,center);}
void mapToScreen(const Map& map,float x,float y,int vx,int vy,int* sx,int* sy) {
 float px=relative(x,vx,map.getW(),width/64.f)*32,py=relative(y,vy,map.getH(),height/64.f)*32,a,b;
 groundToScreen(px,py,0,a,b);*sx=std::lround(a);*sy=std::lround(b);
}
void screenToMap(const Map& map,int sx,int sy,int vx,int vy,int* x,int* y,bool nearest) {
 float a,b;screenToGround(sx,sy,a,b); float bias=nearest?16:0;
 *x=(static_cast<int>(std::floor((a+bias)/32))+vx)&map.getMaskW();
 *y=(static_cast<int>(std::floor((b+bias)/32))+vy)&map.getMaskH();
}
#ifdef HAVE_OPENGL
static void begin() {
 auto gfx=globalContainer->gfx;
 glPushAttrib(GL_ALL_ATTRIB_BITS);
 glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,gfx->getW(),gfx->getH(),0,-8192,8192);
 glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
 float c=std::cos(yaw),s=std::sin(yaw),sp=std::sin(pitch),cp=std::cos(pitch),cx=width*.5f+camera.offsetX,cy=height*.5f+camera.offsetY;
 float m[]={zoom*c,zoom*sp*s,zoom*cp*s,0, -zoom*s,zoom*sp*c,zoom*cp*c,0, 0,-zoom*cp,zoom*sp,0,
 width*.5f-zoom*(c*cx-s*cy),height*.5f-zoom*sp*(s*cx+c*cy),-zoom*cp*(s*cx+c*cy),1};
 glLoadMatrixf(m); glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);
 glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING);
 gfx->setClipRect(0,16,width,height-16);
}
static void end() {
 glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);
 glPopAttrib();globalContainer->gfx->restore2DState();
}
static void billboard(GAGCore::Sprite* sprite,int frame,float x,float y,float z,unsigned char alpha=255) {
 auto gfx=globalContainer->gfx;
 gfx->finishDrawingSprite(sprite,255);
 glPushMatrix();glTranslatef(x,y,z);
 float c=std::cos(yaw),s=std::sin(yaw);
 float m[]={c,-s,0,0, 0,0,-1,0, s,c,0,0, 0,0,0,1};glMultMatrixf(m);
 glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,.15f);
 const auto bounds=sprite->getVisibleBounds(frame);
 gfx->drawSprite(-bounds.x-bounds.w/2,-bounds.y-bounds.h,sprite,frame,alpha);
 gfx->finishDrawingSprite(sprite,alpha);glDisable(GL_ALPHA_TEST);glPopMatrix();
}
static void lineRect(float x,float y,float w,float h,const GAGCore::Color& color) {
 glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_LINE_BIT);glDisable(GL_TEXTURE_2D);glDisable(GL_TEXTURE_RECTANGLE);glDisable(GL_LIGHTING);
 glColor4ub(color.r,color.g,color.b,color.a);glLineWidth(2);
 glBegin(GL_LINE_LOOP);glVertex3f(x,y,1);glVertex3f(x+w,y,1);glVertex3f(x+w,y+h,1);glVertex3f(x,y+h,1);glEnd();glPopAttrib();
}
static void groundShadow(float x,float y,bool flying) {
 glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 glDisable(GL_TEXTURE_2D);glDisable(GL_TEXTURE_RECTANGLE);glDisable(GL_LIGHTING);
 glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
 glDepthMask(GL_FALSE);glColor4ub(0,0,0,flying?55:90);
 glBegin(GL_TRIANGLE_FAN);glVertex3f(x,y,.1f);
 for(int i=0;i<=24;i++) {
  float a=i*6.2831853f/24;
  glVertex3f(x+std::cos(a)*10,y+std::sin(a)*8,.1f);
 }
 glEnd();glPopAttrib();
}
struct Vertex {float x,y,z,nx,ny,nz,r,g,b,tint;};
struct Clip {std::vector<std::vector<Vertex> > frames;std::vector<float> heights;std::map<unsigned,std::vector<GLuint> > lists;};
static std::map<std::string,Clip> clips;
static Clip& load(const std::string& name) {
 static SDL_GLContext context=NULL;
 if(context!=SDL_GL_GetCurrentContext()){for(auto& pair:clips)pair.second.lists.clear();context=SDL_GL_GetCurrentContext();}
 auto found=clips.find(name);if(found!=clips.end())return found->second;
 Clip& clip=clips[name];std::ifstream in(std::string(PACKAGE_SOURCE_DIR)+"/data/models3d/"+name+".g3d",std::ios::binary);
 char magic[4];unsigned count=0;in.read(magic,4);in.read(reinterpret_cast<char*>(&count),4);
 if(!in || std::string(magic,4)!="G3D1" || count>64) {std::cerr<<"3D clip unavailable: "<<name<<"\n";return clip;}
 for(unsigned i=0;i<count;i++) {unsigned n=0;in.read(reinterpret_cast<char*>(&n),4);if(n>500000 || !in){clip.frames.clear();break;}
  std::vector<Vertex> frame(n);in.read(reinterpret_cast<char*>(frame.data()),n*sizeof(Vertex));if(!in){clip.frames.clear();break;}
  float top=0;for(const auto& v:frame)top=std::max(top,v.z);clip.heights.push_back(top);
  clip.frames.push_back(std::move(frame));}
 return clip;
}
static float unitMesh(Unit* u,float x,float y) {
 std::string name;
 if(u->typeNum==EXPLORER)name="explorer";
 else if(u->typeNum==WARRIOR)name=u->action==SWIM||u->action==STOP_SWIM?"warrior-swim":u->action==ATTACK_SPEED?"warrior-fight":"warrior-walk";
 else name=u->action==SWIM||u->action==STOP_SWIM?"worker-swim":u->action>=BUILD?"worker-harvest":"worker-walk";
 Clip& clip=load(name);if(clip.frames.empty())return 16;
 unsigned f=u->action<=STOP_FLY?0:((unsigned)u->delta*clip.frames.size()/256)%clip.frames.size();
 float z=u->typeNum==EXPLORER?28.f:0.f;
 glPushAttrib(GL_ENABLE_BIT|GL_CURRENT_BIT|GL_LIGHTING_BIT);
 glDisable(GL_TEXTURE_2D);glDisable(GL_TEXTURE_RECTANGLE);glDisable(GL_BLEND);
 glEnable(GL_LIGHTING);glEnable(GL_LIGHT0);glEnable(GL_COLOR_MATERIAL);glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);glEnable(GL_NORMALIZE);
 GLfloat light[]={-.4f,-.6f,1,0},ambient[]={.4f,.4f,.4f,1},diffuse[]={.85f,.85f,.85f,1};
 glLightfv(GL_LIGHT0,GL_POSITION,light);glLightfv(GL_LIGHT0,GL_DIFFUSE,diffuse);glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);
 glPushMatrix();glTranslatef(x,y,z);
 int dx=0,dy=-1;if(u->direction<8)Unit::dxDyFromDirection(u->direction,&dx,&dy);
 glRotatef(World3DPlacement::modelYawDegrees(dx,dy,u->typeNum==WARRIOR),0,0,1);
 GAGCore::Color color=u->owner->color;
 unsigned colorKey=(color.r<<16)|(color.g<<8)|color.b;
 auto& lists=clip.lists[colorKey];if(lists.empty())lists.resize(clip.frames.size(),0);
 if(!lists[f]) { lists[f]=glGenLists(1);glNewList(lists[f],GL_COMPILE);
 glBegin(GL_TRIANGLES);
 for(const Vertex& v:clip.frames[f]) {
  if(v.tint>.5f)glColor3f(.18f+.82f*color.r/255.f,.18f+.82f*color.g/255.f,.18f+.82f*color.b/255.f);
  else glColor3f(v.r,v.g,v.b);
  glNormal3f(v.nx,v.ny,v.nz);glVertex3f(v.x,v.y,v.z);
 }glEnd();glEndList();}
 glCallList(lists[f]);glPopMatrix();glPopAttrib();
 return z+clip.heights[f];
}

struct TexturedVertex {float x,y,z,nx,ny,nz,u,v;};
struct SwarmModel {
 std::vector<TexturedVertex> vertices;
 std::vector<Uint8> pixels;
 float low[3]={0,0,0},high[3]={0,0,0};
 int textureWidth=0,textureHeight=0;
 SDL_GLContext context=nullptr;
 GLuint list=0;
 std::map<unsigned,GLuint> textures;
 bool attempted=false;
};
static SwarmModel swarm;
struct ScreenBounds {float left,top,right,bottom;};

static bool loadSwarm() {
 if(swarm.context!=SDL_GL_GetCurrentContext()) {
  swarm.context=SDL_GL_GetCurrentContext();swarm.list=0;swarm.textures.clear();
 }
 if(swarm.attempted)return !swarm.vertices.empty() && !swarm.pixels.empty();
 swarm.attempted=true;
 const std::string folder=std::string(PACKAGE_SOURCE_DIR)+"/data/models3d/";
 std::ifstream in(folder+"swarm.g3t",std::ios::binary);
 char magic[4];unsigned count=0;
 in.read(magic,4);in.read(reinterpret_cast<char*>(&count),4);
 if(!in || std::string(magic,4)!="G3T1" || !count || count%3 || count>900000) {
  std::cerr<<"3D swarm mesh unavailable; using its sprite\n";return false;
 }
 swarm.vertices.resize(count);
 in.read(reinterpret_cast<char*>(swarm.vertices.data()),count*sizeof(TexturedVertex));
 if(!in){swarm.vertices.clear();return false;}
 for(const auto& v:swarm.vertices) {
  if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||
     !std::isfinite(v.nx)||!std::isfinite(v.ny)||!std::isfinite(v.nz)||
     !std::isfinite(v.u)||!std::isfinite(v.v)) {swarm.vertices.clear();return false;}
  const float p[]={v.x,v.y,v.z};
  for(int a=0;a<3;a++){swarm.low[a]=std::min(swarm.low[a],p[a]);swarm.high[a]=std::max(swarm.high[a],p[a]);}
 }
 SDL_Surface* source=IMG_Load((folder+"swarm-basecolor.png").c_str());
 if(!source){std::cerr<<"3D swarm texture unavailable; using its sprite\n";return false;}
 // Keep the original bake resolution: its many UV islands need their gutters.
 swarm.textureWidth=source->w;swarm.textureHeight=source->h;
 SDL_Surface* pixels=SDL_CreateRGBSurfaceWithFormat(0,swarm.textureWidth,swarm.textureHeight,32,SDL_PIXELFORMAT_RGBA32);
 if(!pixels){SDL_FreeSurface(source);return false;}
 SDL_SetSurfaceBlendMode(source,SDL_BLENDMODE_NONE);SDL_BlitScaled(source,nullptr,pixels,nullptr);
 swarm.pixels.resize(swarm.textureWidth*swarm.textureHeight*4);
 for(int y=0;y<swarm.textureHeight;y++)
  std::copy_n(static_cast<Uint8*>(pixels->pixels)+y*pixels->pitch,swarm.textureWidth*4,swarm.pixels.data()+y*swarm.textureWidth*4);
 SDL_FreeSurface(pixels);SDL_FreeSurface(source);
 return true;
}

static GLuint swarmTexture(const GAGCore::Color& team) {
 const unsigned key=(team.r<<16)|(team.g<<8)|team.b;
 auto found=swarm.textures.find(key);if(found!=swarm.textures.end())return found->second;
 auto pixels=swarm.pixels;
 for(size_t i=0;i<pixels.size();i+=4) {
  // This monochrome green asset has no separate team mask. Retain its baked
  // brightness/detail and use the same team palette as the animated units.
  const float value=std::pow(std::max(pixels[i],std::max(pixels[i+1],pixels[i+2]))/255.f,.65f);
  pixels[i]=static_cast<Uint8>(255*value*(.18f+.82f*team.r/255.f));
  pixels[i+1]=static_cast<Uint8>(255*value*(.18f+.82f*team.g/255.f));
  pixels[i+2]=static_cast<Uint8>(255*value*(.18f+.82f*team.b/255.f));
 }
 GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
 glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,swarm.textureWidth,swarm.textureHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
 swarm.textures[key]=texture;return texture;
}

static bool swarmMesh(BuildingType* type,const GAGCore::Color& team,float x,float y,
                      ScreenBounds* bounds=nullptr,unsigned char alpha=255,float health=1.f) {
 if(type->shortTypeNum!=IntBuildingType::SWARM_BUILDING || !loadSwarm())return false;
 const float cx=x+type->width*16,cy=y+type->height*16;
 const float size=.8f*std::min(type->width,type->height)*32;
 glPushAttrib(GL_ALL_ATTRIB_BITS);
 glDisable(GL_TEXTURE_RECTANGLE);glEnable(GL_TEXTURE_2D);
 glBindTexture(GL_TEXTURE_2D,swarmTexture(team));glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
 if(alpha<255){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}
 else glDisable(GL_BLEND);
 glEnable(GL_LIGHTING);glEnable(GL_LIGHT0);glEnable(GL_NORMALIZE);glEnable(GL_COLOR_MATERIAL);
 glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,GL_TRUE);
 glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
 GLfloat light[]={-.4f,-.6f,1,0},ambient[]={.5f,.5f,.5f,1},diffuse[]={.7f,.7f,.7f,1};
 glLightfv(GL_LIGHT0,GL_POSITION,light);glLightfv(GL_LIGHT0,GL_DIFFUSE,diffuse);glLightModelfv(GL_LIGHT_MODEL_AMBIENT,ambient);
 GLfloat specular[]={.22f,.22f,.22f,1};
 glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,specular);glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,24);
 glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL,GL_SEPARATE_SPECULAR_COLOR);
 const float shade=.6f+.4f*std::max(0.f,std::min(1.f,health));glColor4f(shade,shade,shade,alpha/255.f);
 glPushMatrix();glTranslatef(cx,cy,0);glScalef(size,size,size);
 if(!swarm.list) {
  swarm.list=glGenLists(1);glNewList(swarm.list,GL_COMPILE);glBegin(GL_TRIANGLES);
  for(const auto& v:swarm.vertices) {
   glNormal3f(v.nx,v.ny,v.nz);glTexCoord2f(v.u,1-v.v);glVertex3f(v.x,v.y,v.z);
  }
  glEnd();glEndList();
 }
 glCallList(swarm.list);glPopMatrix();glPopAttrib();
 if(bounds) {
  *bounds={1e9f,1e9f,-1e9f,-1e9f};
  for(int a=0;a<8;a++) {
   float sx,sy;groundToScreen(cx+(a&1?swarm.high[0]:swarm.low[0])*size,
    cy+(a&2?swarm.high[1]:swarm.low[1])*size,(a&4?swarm.high[2]:swarm.low[2])*size,sx,sy);
   bounds->left=std::min(bounds->left,sx);bounds->right=std::max(bounds->right,sx);
   bounds->top=std::min(bounds->top,sy);bounds->bottom=std::max(bounds->bottom,sy);
  }
 }
 return true;
}

static int buildingFrame(Game& g,Building* b) {
 auto t=b->type;
 if(!t->crossConnectMultiImage) {int hp=std::min(b->hp,t->hpMax);return t->gameSpriteImage+t->gameSpriteCount-(hp*t->gameSpriteCount)/(t->hpMax+1)-1;}
 int add=0,dx[]={0,0,-1,t->width},dy[]={-1,t->height,0,0},bits[]={8,4,2,1};
 for(int i=0;i<4;i++){unsigned id=g.map.getBuilding(b->posXLocal+dx[i],b->posYLocal+dy[i]);
 if(id!=NOGBID && Building::GIDtoTeam(id)==b->owner->teamNumber && b->owner->myBuildings[Building::GIDtoID(id)]->type==t)add|=bits[i];}
 return t->gameSpriteImage+add;
}
struct BuildingIndicator {
 Building* building;
 float x, top, w, h;
};
struct UnitIndicator {Unit* unit;float x,top,feet;};

static void drawUnitIndicators(Game& game,const std::vector<UnitIndicator>& indicators,unsigned options) {
 auto gfx=globalContainer->gfx;
 for(const auto& indicator:indicators) {
  auto u=indicator.unit;
  const int x=std::lround(indicator.x),top=std::lround(indicator.top),feet=std::lround(indicator.feet);
  if(x<-32 || x>width+32 || feet<16 || top>height-52)continue;
  game.drawUnitStatus(u,x-16,feet-24,top-8,options);
  if(u->levelUpAnimation) {
   auto font=globalContainer->standardFont;
   std::string level=std::to_string(u->experienceLevel);
   font->pushStyle(GAGCore::Font::Style(GAGCore::Font::STYLE_NORMAL,242,131,14));
   gfx->drawString(x-font->getStringWidth(level)/2,top-20-2*(LEVEL_UP_ANIMATION_FRAME_COUNT-u->levelUpAnimation),font,level,0,(255*u->levelUpAnimation)/LEVEL_UP_ANIMATION_FRAME_COUNT);
   font->popStyle();
  }
  if(u->magicActionAnimation) {
   auto effect=globalContainer->magiceffect;
   int size=std::max(2,(MAGIC_ACTION_ANIMATION_FRAME_COUNT-u->magicActionAnimation)*effect->getW(0)/MAGIC_ACTION_ANIMATION_FRAME_COUNT);
   gfx->drawSprite(x-size/2,feet-size/2,size,size,effect,0,u->magicActionAnimation*255/MAGIC_ACTION_ANIMATION_FRAME_COUNT);
  }
  if(options&Game::DRAW_ACCESSIBILITY) {
   auto label=std::to_string(u->owner->teamNumber);
   gfx->drawString(x-globalContainer->littleFont->getStringWidth(label)/2,top-12,globalContainer->littleFont,label);
  }
  if(game.highlightUnitType&(1<<u->typeNum))gfx->drawSprite(x-16,top-40,globalContainer->gamegui,36);
 }
 gfx->finishDrawingSprite(globalContainer->ressourceMini,255);
 gfx->finishDrawingSprite(globalContainer->magiceffect,255);
 gfx->finishDrawingSprite(globalContainer->gamegui,255);
}

static void drawBuildingIndicators(Game& game,const std::vector<BuildingIndicator>& indicators,unsigned vision,unsigned options) {
 for(const auto& indicator:indicators) {
  const int x=std::lround(indicator.x-indicator.w*.5f),y=std::lround(indicator.top)-3;
  const int w=std::lround(indicator.w),h=std::lround(indicator.h)+3;
  if(x+w<0 || x>=width || y+h<16 || y>=height-52)continue;
  game.drawBuildingStatus(indicator.building,x,y,w,h,vision,options);
 }
}

#endif
void draw(Game& g,int vx,int vy,int team,unsigned options) {
#ifdef HAVE_OPENGL
 if(!enabled())return;
 auto gfx=globalContainer->gfx;auto& map=g.map;
 bool reveal=(options&Game::DRAW_WHOLE_MAP)!=0;unsigned vision=globalContainer->replaying?globalContainer->replayVisibleTeams:g.teams[team]->me;
 g.mouseUnit=NULL;hovered=NULL;float bestDepth=-1e20f;
 begin();glClear(GL_DEPTH_BUFFER_BIT);
 float corners[4][2]={{0,0},{(float)width,0},{0,(float)height},{(float)width,(float)height}};
 float minX=1e9,minY=1e9,maxX=-1e9,maxY=-1e9;
 for(auto& p:corners){float x,y;screenToGround(p[0],p[1],x,y);minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x);maxY=std::max(maxY,y);}
 int left=std::floor(minX/32)-5,right=std::ceil(maxX/32)+5,top=std::floor(minY/32)-5,bottom=std::ceil(maxY/32)+8;
 glDepthMask(GL_FALSE);
 gfx->drawFilledRect(left*32,top*32,(right-left+1)*32,(bottom-top+1)*32,GAGCore::Color(10,18,23));
 // Shore tiles contain transparent water pixels. Keep the original 512px
 // water texture beneath them, without writing coplanar background depth.
 int waterX=-((vx*32+static_cast<int>(SDL_GetTicks()/80))%512);
 int waterY=-((vy*32)%512);
 while(waterX>left*32)waterX-=512;
 while(waterY>top*32)waterY-=512;
 for(int y=waterY;y<=(bottom+1)*32;y+=512)
  for(int x=waterX;x<=(right+1)*32;x+=512)
   gfx->drawSprite(x,y,globalContainer->terrainWater,0);
 gfx->finishDrawingSprite(globalContainer->terrainWater,255);
 glDepthMask(GL_TRUE);
 std::set<unsigned> buildings,units;
 std::vector<BuildingIndicator> indicators;
 std::vector<UnitIndicator> unitIndicators;
 for(int y=top;y<=bottom;y++)for(int x=left;x<=right;x++) {
  int mx=x+vx,my=y+vy;
  if(!reveal&&!map.isMapDiscovered(mx,my,vision)) {
   gfx->drawFilledRect(x*32,y*32,32,32,GAGCore::Color(10,18,23));
   continue;
  }
  int terrain=map.getTerrain(mx,my);
  if(terrain<256)gfx->drawSprite(x*32,y*32,globalContainer->terrain,terrain);
  unsigned b=map.getBuilding(mx,my);if(b!=NOGBID) buildings.insert(b);
  unsigned u=map.getGroundUnit(mx,my);if(u!=NOGUID)units.insert(u);
  u=map.getAirUnit(mx,my);if(u!=NOGUID)units.insert(u);
 }
 gfx->finishDrawingSprite(globalContainer->terrainWater,255);gfx->finishDrawingSprite(globalContainer->terrain,255);
 glDepthMask(GL_FALSE);glPushMatrix();glTranslatef(0,0,.5f);
 for(int y=top;y<=bottom;y++)for(int x=left;x<=right;x++) {
  int mx=x+vx,my=y+vy;if(!reveal&&!map.isMapDiscovered(mx,my,vision))continue;
  if(!reveal&&!map.isFOWDiscovered(mx,my,vision))gfx->drawFilledRect(x*32,y*32,32,32,GAGCore::Color(0,0,0,135));
  if(map.isForbidden(mx,my,g.teams[team]->me))gfx->drawFilledRect(x*32,y*32,32,32,GAGCore::Color(230,40,40,85));
  if(map.isGuardArea(mx,my,g.teams[team]->me))gfx->drawFilledRect(x*32,y*32,32,32,GAGCore::Color(70,100,255,85));
  if(map.isClearArea(mx,my,g.teams[team]->me))gfx->drawFilledRect(x*32,y*32,32,32,GAGCore::Color(250,205,50,85));
 }glPopMatrix();glDepthMask(GL_TRUE);
 for(int y=top;y<=bottom;y++)for(int x=left;x<=right;x++) {
  if(!reveal&&!map.isMapDiscovered(x+vx,y+vy,vision))continue;
  const auto& r=map.getRessource(x+vx,y+vy);if(r.type==NO_RES_TYPE)continue;
  const auto* rt=globalContainer->ressourcesTypes.get(r.type);int frame=rt->gfxId+r.variety*rt->sizesCount+r.amount-(rt->eternal?0:1);
  billboard(globalContainer->ressources,frame,x*32+16,y*32+16,0);
 }
 for(auto* t:g.teams) {if(!t)continue;if(t->teamNumber==team || globalContainer->replaying) for(auto b:t->virtualBuildings)buildings.insert(b->gid);}
 for(unsigned id:buildings) {
  Building* b=g.teams[Building::GIDtoTeam(id)]->myBuildings[Building::GIDtoID(id)];if(!b)continue;
  auto* t=b->type;
  if(!t->isVirtual&&!reveal&&!map.isFOWDiscovered(b->posX,b->posY,vision) && !(b->owner->allies&vision))continue;
  float x=relative(b->posXLocal,vx,map.getW(),width/64.f)*32,y=relative(b->posYLocal,vy,map.getH(),height/64.f)*32;
  const auto anchor=World3DPlacement::buildingAnchor(x,y,t->width*32,t->height*32,yaw);
  float bx=anchor.x,by=anchor.y;
  int frame=buildingFrame(g,b);auto sprite=t->gameSpritePtr;sprite->setBaseColor(b->owner->color);
  ScreenBounds meshBounds;
  const bool solid=!t->isBuildingSite && swarmMesh(t,b->owner->color,x,y,&meshBounds,255,t->hpMax?static_cast<float>(b->hp)/t->hpMax:1.f);
  if(!solid)billboard(sprite,frame,bx,by,1);
  if(b==g.selectedBuilding || (options&Game::DRAW_BUILDING_RECT))lineRect(x,y,t->width*32,t->height*32,GAGCore::Color(245,220,100));
  if(t->isVirtual && b==g.selectedBuilding)lineRect(x+t->width*16-b->unitStayRange*32,y+t->height*16-b->unitStayRange*32,b->unitStayRange*64,b->unitStayRange*64,GAGCore::Color(100,180,250));
  float sx,sy;groundToScreen(bx,by,1,sx,sy);
  const auto bounds=sprite->getVisibleBounds(frame);
  float w=bounds.w*zoom,h=bounds.h*zoom*std::cos(pitch);
  if(solid) {
   sx=(meshBounds.left+meshBounds.right)*.5f;sy=meshBounds.bottom;
   w=meshBounds.right-meshBounds.left;h=meshBounds.bottom-meshBounds.top;
   bx=x+t->width*16;by=y+t->height*16;
  }
  if((options&Game::DRAW_HEALTH_FOOD_BAR) && (b->owner->sharedVisionOther&vision))
   indicators.push_back({b,sx,sy-h,w,h});
  float d=std::sin(yaw)*bx+std::cos(yaw)*by;
  if(g.mouseX>=sx-w/2&&g.mouseX<=sx+w/2&&g.mouseY>=sy-h&&g.mouseY<=sy && d>bestDepth){hovered=b;bestDepth=d;}
 }
 for(unsigned id:units) {
  Unit* u=g.teams[Unit::GIDtoTeam(id)]->myUnits[Unit::GIDtoID(id)];if(!u||u->isDead)continue;
  if(!reveal&&!map.isFOWDiscovered(u->posX,u->posY,vision)&&!map.isFOWDiscovered(u->posX-u->dx,u->posY-u->dy,vision))continue;
  float x=World3DPlacement::unitAxis(u->posX,u->dx,u->delta,u->action<BUILD,vx,map.getW(),width*.5f);
  float y=World3DPlacement::unitAxis(u->posY,u->dy,u->delta,u->action<BUILD,vy,map.getH(),height*.5f);
  groundShadow(x,y,u->typeNum==EXPLORER);
  float meshTop=unitMesh(u,x,y),headX,headY,feetX,feetY;
  groundToScreen(x,y,meshTop,headX,headY);
  groundToScreen(x,y,u->typeNum==EXPLORER?28:0,feetX,feetY);
  unitIndicators.push_back({u,headX,headY,feetY});
  if(u==g.selectedUnit)lineRect(x-13,y-13,26,26,GAGCore::Color(255,230,100));
  float sx,sy;groundToScreen(x,y,u->typeNum==EXPLORER?35:8,sx,sy);
  float d=std::sin(yaw)*x+std::cos(yaw)*y;
  if(std::abs(g.mouseX-sx)<16*zoom&&std::abs(g.mouseY-sy)<16*zoom && d>=bestDepth){g.mouseUnit=u;hovered=NULL;bestDepth=d;}
 }
 end();gfx->setClipRect(0,16,width,height-16);
 drawUnitIndicators(g,unitIndicators,options);
 drawBuildingIndicators(g,indicators,vision,options);
 gfx->drawFilledRect(10,height-48,width-20,36,GAGCore::Color(10,17,24,210));
 gfx->drawString(20,height-42,globalContainer->littleFont,"3D TRIAL   F8: 2D/3D   Wheel: zoom   [ ]: rotate   PgUp/PgDn: tilt   Ctrl+Home: reset");
 gfx->drawString(20,height-27,globalContainer->littleFont,"Live game simulation | Original animated globs | Upright building sprites");
#endif
}
void brushPreview(Game& g,int mx,int my,int vx,int vy,int figure,int ox,int oy,const GAGCore::Color& color) {
#ifdef HAVE_OPENGL
 int x,y;screenToMap(g.map,mx,my,vx,vy,&x,&y);
 if(ox==-1)ox=x;if(oy==-1)oy=y;
 begin();glDepthMask(GL_FALSE);
 for(int dy=0;dy<BrushTool::getBrushHeight(figure);dy++)for(int dx=0;dx<BrushTool::getBrushWidth(figure);dx++)
 if(BrushTool::getBrushValue(figure,dx,dy,x,y,ox,oy)){
 float px=relative(x+dx-BrushTool::getBrushDimXMinus(figure),vx,g.map.getW(),width/64.f)*32;
 float py=relative(y+dy-BrushTool::getBrushDimYMinus(figure),vy,g.map.getH(),height/64.f)*32;lineRect(px+1,py+1,30,30,color);}
 end();
#endif
}
void preview(Game& g,BuildingType* t,int x,int y,int vx,int vy,int team,bool valid,unsigned char alpha) {
#ifdef HAVE_OPENGL
 if(!active(&g.map))return;
 float px=relative(x,vx,g.map.getW(),width/64.f)*32,py=relative(y,vy,g.map.getH(),height/64.f)*32;
 begin();glDepthMask(GL_FALSE);
 const auto anchor=World3DPlacement::buildingAnchor(px,py,t->width*32,t->height*32,yaw);
 if(!swarmMesh(t,g.teams[team]->color,px,py,nullptr,alpha)) {
  t->gameSpritePtr->setBaseColor(g.teams[team]->color);billboard(t->gameSpritePtr,t->gameSpriteImage,anchor.x,anchor.y,1,alpha);
 }
 lineRect(px,py,t->width*32,t->height*32,valid?GAGCore::Color(100,255,130):GAGCore::Color(255,70,60));end();
#endif
}
}
