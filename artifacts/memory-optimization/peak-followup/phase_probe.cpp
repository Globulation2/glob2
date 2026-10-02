#include "GlobalContainer.h"
#include "Engine.h"
#include <BinaryStream.h>
#include <ChunkedStreamBackend.h>
#include <FileManager.h>
#include <ctime>
#include <chrono>
#include <iostream>
#include <sys/resource.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif
GlobalContainer* globalContainer = nullptr;
using namespace GAGCore;
struct CustomGameSetupHarness { static GameGUI& gui(Engine& engine) { return engine.gui; } };
uint64_t resident() {
#ifdef __APPLE__
 mach_task_basic_info_data_t info{}; mach_msg_type_number_t n=MACH_TASK_BASIC_INFO_COUNT;
 if(task_info(mach_task_self(),MACH_TASK_BASIC_INFO,reinterpret_cast<task_info_t>(&info),&n)==KERN_SUCCESS) return info.resident_size;
#endif
 return 0;
}
uint64_t peak() { rusage r{};getrusage(RUSAGE_SELF,&r);
#ifdef __APPLE__
 return r.ru_maxrss;
#else
 return uint64_t(r.ru_maxrss)*1024;
#endif
}
void report(const char* phase, std::clock_t start, std::chrono::steady_clock::time_point wall, const ChunkedBuffer* snapshot=nullptr) {
 std::cout<<"PHASE {\"phase\":\""<<phase<<"\",\"resident_bytes\":"<<resident()<<",\"peak_bytes\":"<<peak()<<",\"cpu_ns\":"<<uint64_t(double(std::clock()-start)*1e9/CLOCKS_PER_SEC)<<",\"wall_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-wall).count();
 if(snapshot)std::cout<<",\"snapshot_bytes\":"<<snapshot->size()<<",\"snapshot_capacity\":"<<snapshot->allocatedCapacity()<<",\"table_capacity\":"<<snapshot->tableCapacity();
 std::cout<<"}\n";
}
int main(int argc,char**argv) {
 if(argc!=3 && argc!=4)return 2;
 GlobalContainer globals("glob2-peak-followup-probe"); globalContainer=&globals;
 globals.runNoX=true; globals.structuredHeadless=true;globals.settings.rememberUnit=false;globals.load();
 auto cpu=std::clock();auto wall=std::chrono::steady_clock::now();
 Engine engine; if(engine.initCustom(argv[1])!=Engine::EE_NO_ERROR)return 3;
 report("load",cpu,wall);
 const bool legacy=argc==4 && std::string(argv[3])=="legacy";
 if(legacy) {
  auto* backend=new MemoryStreamBackend();BinaryOutputStream stream(backend);DeferredGameSHA1 hash;
  cpu=std::clock();wall=std::chrono::steady_clock::now();CustomGameSetupHarness::gui(engine).save(&stream,"final",&hash);auto snapshot=backend->takeContents();
  report("capture",cpu,wall);std::cout<<"LEGACY {\"snapshot_bytes\":"<<snapshot.size()<<",\"snapshot_capacity\":"<<snapshot.capacity()<<"}\n";
  cpu=std::clock();wall=std::chrono::steady_clock::now();hash.apply(snapshot);report("hash",cpu,wall);
  cpu=std::clock();wall=std::chrono::steady_clock::now();if(!globals.fileManager->writeGzipAtomic(argv[2],snapshot))return 4;
  report("compression",cpu,wall);
  std::string().swap(snapshot);report("released",std::clock(),std::chrono::steady_clock::now());
 } else {
  auto* backend=new ChunkedStreamBackend();BinaryOutputStream stream(backend);DeferredGameSHA1 hash;
  cpu=std::clock();wall=std::chrono::steady_clock::now();CustomGameSetupHarness::gui(engine).save(&stream,"final",&hash);auto snapshot=backend->takeContents();
  report("capture",cpu,wall,&snapshot);
  cpu=std::clock();wall=std::chrono::steady_clock::now();hash.apply(snapshot);report("hash",cpu,wall,&snapshot);
  cpu=std::clock();wall=std::chrono::steady_clock::now();if(!globals.fileManager->writeGzipAtomic(argv[2],snapshot))return 4;
  report("compression",cpu,wall,&snapshot);
  snapshot=ChunkedBuffer();report("released",std::clock(),std::chrono::steady_clock::now());
 }
}
