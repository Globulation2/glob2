#pragma once
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
namespace StageProbe {
inline std::atomic<bool> enabled{false};
inline std::atomic<unsigned long long> cpu[8]{}, calls[8]{};
inline unsigned long long now() { timespec t{}; clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t); return t.tv_sec*1000000000ULL+t.tv_nsec; }
struct Scope;
inline thread_local Scope* top = nullptr;
struct Scope {
 unsigned slot; unsigned long long start, children=0; Scope* parent=nullptr;
 explicit Scope(unsigned s):slot(s),start(enabled.load(std::memory_order_relaxed)?now():0) {if(start){parent=top;top=this;}}
 ~Scope(){if(start){const auto elapsed=now()-start;cpu[slot].fetch_add(elapsed-children,std::memory_order_relaxed);calls[slot].fetch_add(1,std::memory_order_relaxed);top=parent;if(parent)parent->children+=elapsed;}}
};
inline void begin(){enabled.store(std::getenv("GLOB2_STAGE_CPU")!=nullptr);}
inline void finish(){enabled.store(false);if(const char* p=std::getenv("GLOB2_STAGE_CPU")){if(FILE* f=std::fopen(p,"w")){std::fputs("{\"cpu_ns\":[",f);for(unsigned i=0;i<8;++i)std::fprintf(f,"%s%llu",i?",":"",cpu[i].load());std::fputs("],\"calls\":[",f);for(unsigned i=0;i<8;++i)std::fprintf(f,"%s%llu",i?",":"",calls[i].load());std::fputs("]}\n",f);std::fclose(f);}}}
}
