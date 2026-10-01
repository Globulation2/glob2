#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Engine.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "FileManager.h"
#include "Stream.h"
#include "BinaryStream.h"
#include "Toolkit.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sys/resource.h>
#include <time.h>
#include "PerformanceTelemetry.h"
GlobalContainer *globalContainer=nullptr;
static double cpu(){rusage r{};getrusage(RUSAGE_SELF,&r);return r.ru_utime.tv_sec+r.ru_utime.tv_usec*1e-6+r.ru_stime.tv_sec+r.ru_stime.tv_usec*1e-6;}
static std::uint64_t cpuClock(){timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return std::uint64_t(t.tv_sec)*1000000000ull+t.tv_nsec;}
class TorusRenderBenchmark { public: static int run(int argc,char**argv){
 SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP,"1");
 globalContainer=new GlobalContainer;
 globalContainer->parseArgs(argc,argv);globalContainer->settings.mute=1;globalContainer->settings.autosaveGames=false;globalContainer->load();
 auto *gfx=globalContainer->gfx;if(!getenv("PROFILE_VISIBLE"))SDL_HideWindow(SDL_GetWindowFromID(gfx->windowID()));
 if(gfx->getOptionFlags() & GraphicContext::USEGPU)return 2;
 { GameGUI gui; const char* path=getenv("PROFILE_SAVE");
 BinaryInputStream stream(glob2OpenMapOrSaveInputStreamBackend(*Toolkit::getFileManager(),path));
 if(!gui.load(&stream,true))return 3;
 gui.localPlayer=gui.localTeamNo=0;gui.adjustLocalTeam();gui.adjustInitialViewport();gui.gamePaused=false;
 if(const char*z=getenv("PROFILE_ZOOM")){gui.updateCamera();gui.camera.setZoom(atof(z),300,300);gui.viewportX=gui.camera.tileX();gui.viewportY=gui.camera.tileY();}
 int units=0,buildings=0; for(int t=0;t<gui.game.mapHeader.getNumberOfTeams();t++){for(int i=0;i<Unit::MAX_COUNT;i++)units+=gui.game.teams[t]->myUnits[i]!=nullptr;for(int i=0;i<Building::MAX_COUNT;i++)buildings+=gui.game.teams[t]->myBuildings[i]!=nullptr;}
 printf("CAMERA zoom=%.4f fractional=%.4f,%.4f offset=%.4f,%.4f\n",gui.camera.zoom,gui.camera.fractionX(),gui.camera.fractionY(),gui.camera.offsetX,gui.camera.offsetY);
 if(getenv("PROFILE_FRACTION")){gui.updateCamera();gui.camera.originX+=.5;gui.viewportX=gui.camera.tileX();}
 const int frames=getenv("PROFILE_FRAMES")?atoi(getenv("PROFILE_FRAMES")):240;
 const char* mode=getenv("PROFILE_MODE"); if(!mode)mode="gui";
 const bool clouds=!(getenv("PROFILE_CLOUDS") && atoi(getenv("PROFILE_CLOUDS"))==0);
 if(!clouds)globalContainer->settings.optionFlags|=GlobalContainer::OPTION_LOW_SPEED_GFX;
 else globalContainer->settings.optionFlags&=~GlobalContainer::OPTION_LOW_SPEED_GFX;
 printf("READY save=%s tick=%u map=%dx%d teams=%d units=%d buildings=%d surface=%dx%d camera=%d,%d mode=%s clouds=%d frames=%d driver=%s\n",path,gui.game.stepCounter,gui.game.map.getW(),gui.game.map.getH(),gui.game.mapHeader.getNumberOfTeams(),units,buildings,gfx->getW(),gfx->getH(),gui.viewportX,gui.viewportY,mode,clouds,frames,SDL_GetCurrentVideoDriver());fflush(stdout);
 if(getenv("PROFILE_CPU_SCOPES")){PerformanceTelemetry::collector().clock=cpuClock;PerformanceTelemetry::collector().reset();}
 std::vector<double> draw,present; double c0=0;double freq=SDL_GetPerformanceFrequency();
 for(int i=-30;i<frames;i++){
  SDL_PumpEvents(); if(i==0){c0=cpu();PerformanceTelemetry::collector().reset();} Uint64 a=SDL_GetPerformanceCounter();
  if(std::string(mode)=="gui")gui.drawAll(0);
  else {gfx->setClipRect();gui.game.drawMap(0,0,gfx->getW()-160,gfx->getH(),0,0,gui.viewportX,gui.viewportY,0,gui.view,Game::DRAW_AREA);}
  Uint64 b=SDL_GetPerformanceCounter();if(!getenv("PROFILE_NO_PRESENT"))gfx->nextFrame();Uint64 c=SDL_GetPerformanceCounter();
  if(i>=0){draw.push_back(1000.*(b-a)/freq);present.push_back(1000.*(c-b)/freq);}
 }
 double totalCpu=cpu()-c0;
 auto output=[&](const char*label,std::vector<double> v){double sum=0;for(double x:v)sum+=x;std::sort(v.begin(),v.end());printf("%s mean_ms=%.4f median_ms=%.4f p95_ms=%.4f\n",label,sum/v.size(),v[v.size()/2],v[(v.size()-1)*95/100]);};output("draw",draw);output("present",present);printf("process_cpu_ms_per_frame=%.4f\n",totalCpu*1000/frames);fflush(stdout);
 PerformanceTelemetry::collector().write(std::cout,"profile",gui.game.stepCounter,false);
 if(const char*p=getenv("PROFILE_CAPTURE"))SDL_SaveBMP(gfx->getSDLSurface(),p);
 }delete globalContainer;globalContainer=nullptr;return 0;}};
int main(int argc,char**argv){return TorusRenderBenchmark::run(argc,argv);}
