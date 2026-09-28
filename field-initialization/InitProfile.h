#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
namespace InitProfile {
using Clock=std::chrono::steady_clock;
struct Metrics {
 uint64_t calls[2]={}, cells[2]={}, ns[2]={}, fill[2]={};
 ~Metrics(){for(int i=0;i<2;++i) std::fprintf(stderr,"INIT_PROFILE virtual=%d calls=%llu cells=%llu ns=%llu fill_ns=%llu\n",i,(unsigned long long)calls[i],(unsigned long long)cells[i],(unsigned long long)ns[i],(unsigned long long)fill[i]);}
};
inline Metrics metrics;
struct Timer {
 int kind; Clock::time_point start=Clock::now(); bool stopped=false;
 Timer(bool virt, size_t cells):kind(virt){++metrics.calls[kind];metrics.cells[kind]+=cells;}
 void stop(){if(!stopped){metrics.ns[kind]+=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();stopped=true;}}
 ~Timer(){stop();}
};
}
