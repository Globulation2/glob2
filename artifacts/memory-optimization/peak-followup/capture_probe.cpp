#include "GlobalContainer.h"
#include "Engine.h"
#include <BinaryStream.h>
#include <ChunkedStreamBackend.h>
#include <ctime>
#include <chrono>
#include <iostream>
#include <type_traits>
GlobalContainer* globalContainer=nullptr;
struct CustomGameSetupHarness { static GameGUI& gui(Engine& e) { return e.gui; } };
using namespace GAGCore;
size_t previousSize=0;
template<class Backend> void capture(GameGUI& gui,int pair,const char* variant,bool record,bool reserve=false) {
 auto cpu=std::clock();auto wall=std::chrono::steady_clock::now();
 auto* backend=new Backend();
 if constexpr(std::is_same_v<Backend,MemoryStreamBackend>)if(reserve)backend->reserve(previousSize+previousSize/8);
 BinaryOutputStream stream(backend);DeferredGameSHA1 hash;
 gui.save(&stream,"capture benchmark",&hash);auto snapshot=backend->takeContents();
 auto cpuNs=uint64_t(double(std::clock()-cpu)*1e9/CLOCKS_PER_SEC);
 auto wallNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-wall).count();
 if(previousSize && previousSize!=snapshot.size())throw std::runtime_error("snapshot size changed");
 previousSize=snapshot.size();
 if(record)std::cout<<"CAPTURE {\"pair\":"<<pair<<",\"variant\":\""<<variant<<"\",\"cpu_ns\":"<<cpuNs<<",\"wall_ns\":"<<wallNs<<",\"bytes\":"<<snapshot.size()<<"}\n";
}
int main(int argc,char**argv) {
 if(argc!=2)return 2;
 GlobalContainer globals("glob2-peak-followup-capture");globalContainer=&globals;globals.runNoX=true;globals.structuredHeadless=true;globals.settings.rememberUnit=false;globals.load();
 Engine engine;if(engine.initCustom(argv[1])!=Engine::EE_NO_ERROR)return 3;
 auto& gui=CustomGameSetupHarness::gui(engine);
 capture<MemoryStreamBackend>(gui,-1,"growing",false);
 capture<ChunkedStreamBackend>(gui,-1,"chunked",false);
 for(int pair=0;pair<7;++pair) {
  if(pair%2==0) {
   capture<MemoryStreamBackend>(gui,pair,"growing",true);
   capture<MemoryStreamBackend>(gui,pair,"reserved",true,true);
   capture<ChunkedStreamBackend>(gui,pair,"chunked",true);
  } else {
   capture<ChunkedStreamBackend>(gui,pair,"chunked",true);
   capture<MemoryStreamBackend>(gui,pair,"reserved",true,true);
   capture<MemoryStreamBackend>(gui,pair,"growing",true);
  }
 }
}
