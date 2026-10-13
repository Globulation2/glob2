// Narrow standalone uniform AI BFS; no weighted/admission/stop/resume changes.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef K3_COUNTERS
static bool charge=false;static std::uint64_t visits=0,visitDigest=1469598103934665603ULL,allocations=0,bytes=0,pushes=0,pops=0,grows=0,wraps=0,peakPending=0;
void*operator new(std::size_t n){if(charge){++allocations;bytes+=n;}if(void*p=std::malloc(n))return p;throw std::bad_alloc();}void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#define K3_VISIT_EVENT(current) do{if(charge){++visits;visitDigest=(visitDigest^unsigned(current))*1099511628211ULL;}}while(0)
#define K3_POP_EVENT(wrapped) do{if(charge){++pops;wraps+=(wrapped);}}while(0)
#define K3_PUSH_EVENT(count,grew) do{if(charge){++pushes;grows+=(grew);peakPending=std::max<std::uint64_t>(peakPending,(count));}}while(0)
#else
#define K3_VISIT_EVENT(current) ((void)0)
#endif
#include "UniformTraversal.h"
#ifndef K3_BATCH
#define K3_BATCH 0
#endif
// This experiment-only function is the no-stop/no-filter AI expand_bfs body.
// Generic breadthFirst/traverse/expandDistances overloads remain unchanged.
template<std::size_t Batch>void batched(std::vector<std::int16_t>&values,field::Frontier&queue,const field::Grid&grid){
 const int width=grid.width(),maskX=grid.maskX(),cellMask=int(grid.cells())-1;
 const auto run=[&](auto masked){std::array<int,Batch> stack;
  while(!queue.empty()){std::size_t size=0;while(size<Batch&&!queue.empty()){stack[size++]=queue.front();queue.pop_front();}
   for(std::size_t k=0;k<size;++k){const int current=stack[k];K3_VISIT_EVENT(current);const auto nextValue=values[current]+1;
    const auto discover=[&](int next){if(values[next]==0){values[next]=nextValue;queue.push_back(next);}};
    if constexpr(decltype(masked)::value){const int x=current&maskX,row=current&~maskX;for(auto d:field::Surrounding)discover(((row+d.y*width)&cellMask)+((x+d.x)&maskX));}
    else grid.neighborIndices(current,field::Surrounding,discover);
   }
  }
 };
 if(grid.powerOfTwo())run(std::true_type{});else run(std::false_type{});
}
struct Fixture{std::string name;int w,h,swim;bool prewrap;std::vector<std::int16_t> input;std::vector<int> sources;};
std::uint64_t hash(const std::vector<std::int16_t>&a){std::uint64_t h=1469598103934665603ULL;for(auto v:a){h=(h^std::uint16_t(v))*1099511628211ULL;}return h;}
std::vector<Fixture>fixtures(){std::vector<Fixture>all;
 auto add=[&](int w,int h,int swim,std::string mode,unsigned sources,bool prewrap){Fixture f{std::to_string(w)+"x"+std::to_string(h)+"-s"+std::to_string(swim)+"-"+mode+"-n"+std::to_string(sources)+(prewrap?"-wrap":""),w,h,swim,prewrap,std::vector<std::int16_t>(std::size_t(w)*h,0),{}};std::mt19937 rng(0xAD8217u+unsigned(w*137+h*13+swim));
  for(int x=0;x<w;++x)for(int y=0;y<h;++y){const int i=y*w+x;bool water=rng()%4==0;bool obstacle=(swim==0&&water)||(mode=="barrier"&&((x==w/2&&y!=h/3)||(y==h/2&&x!=w/3)))||(mode=="sparse"&&rng()%19==0);if(obstacle)f.input[i]=1;}
  // Production source scan is x-major/y-minor; preserve this exact order.
  for(int x=0;x<w&&f.sources.size()<sources;++x)for(int y=0;y<h&&f.sources.size()<sources;++y){const int i=y*w+x;if(f.input[i]!=0)continue;if(mode=="clustered"||rng()%std::max(1u,unsigned(w*h)/std::max(1u,sources))==0){f.input[i]=2;f.sources.push_back(i);}}
  if(sources&&!f.sources.size()){f.input[0]=2;f.sources.push_back(0);}all.push_back(std::move(f));
 };
 for(int size:{128,256})for(int swim:{0,3,6})for(auto mode:{"sparse","clustered","barrier"})add(size,size,swim,mode,16,false);
 for(auto dims:{std::pair{1,1},{1,32},{32,1},{2,17},{17,2},{2,2},{7,13},{4,5}})for(int swim:{0,3,6})for(unsigned sources:{0u,1u,7u})add(dims.first,dims.second,swim,"clustered",sources,true);
 for(unsigned n:{63u,64u,65u,127u,128u,129u})add(128,128,3,"clustered",n,true);
 // Representable long-distance wrap path without signed16 overflow.
 Fixture longf{"1x8192-long-distance",1,8192,3,false,std::vector<std::int16_t>(8192,0),{0}};longf.input[0]=2;all.push_back(std::move(longf));return all;
}
struct Expected{std::vector<std::int16_t>values;std::uint64_t visits,digest;};
Expected oracle(const Fixture&f){field::Grid grid(f.w,f.h);Expected e{f.input,0,1469598103934665603ULL};std::deque<int>q(f.sources.begin(),f.sources.end());while(!q.empty()){const int current=q.front();q.pop_front();++e.visits;e.digest=(e.digest^unsigned(current))*1099511628211ULL;const auto next=e.values[current]+1;for(auto d:field::Surrounding){const auto i=grid.index(current%f.w+d.x,current/f.w+d.y);if(e.values[i]==0){e.values[i]=next;q.push_back(i);}}}return e;}
void seed(const Fixture&f,field::Frontier&q){q.clear();if(f.prewrap){for(int i=0;i<63;++i)q.push_back(0);for(int i=0;i<63;++i)q.pop_front();}for(auto i:f.sources)q.push_back(i);}
void kernel(const Fixture&f,std::vector<std::int16_t>&values,field::Frontier&q){
#if K3_BATCH == 0
 field::expandDistances(values,q,field::Grid(f.w,f.h),field::Surrounding,std::int16_t(0));
#else
 batched<K3_BATCH>(values,q,field::Grid(f.w,f.h));
#endif
 q.clear();}
void check(const Fixture&f,const std::vector<std::int16_t>&v,const Expected&e,const field::Frontier&q){if(v!=e.values||!q.empty())throw std::runtime_error("all-cell/frontier mismatch "+f.name);
#ifdef K3_COUNTERS
 if(visits!=e.visits||visitDigest!=e.digest)throw std::runtime_error("visitation order mismatch "+f.name);
#endif
}
int main(int argc,char**argv){try{auto all=fixtures();std::uint64_t cells=0,aggregate=1469598103934665603ULL;const bool benchmark=argc==4&&std::strcmp(argv[1],"--benchmark")==0;
 for(std::size_t idx=0;idx<all.size();++idx){if(benchmark&&idx!=std::strtoul(argv[2],nullptr,10))continue;auto&f=all[idx];auto e=oracle(f);auto values=f.input;field::Frontier q;seed(f,q);
#ifdef K3_COUNTERS
 visits=0;visitDigest=1469598103934665603ULL;charge=true;
#endif
 kernel(f,values,q);
#ifdef K3_COUNTERS
 charge=false;
#endif
 check(f,values,e,q);cells+=values.size();aggregate=(aggregate^hash(values))*1099511628211ULL;std::printf("ORACLE fixture=%s input=%016llx output=%016llx visits=%llu visitation=%016llx cells=%zu\n",f.name.c_str(),(unsigned long long)hash(f.input),(unsigned long long)hash(values),(unsigned long long)e.visits,(unsigned long long)e.digest,values.size());}
 std::printf("ORACLE_PASS fixtures=%zu cells=%llu aggregate=%016llx batch=%d\n",all.size(),(unsigned long long)cells,(unsigned long long)aggregate,K3_BATCH);
 if(argc<2)return 0;if(std::strcmp(argv[1],"--metrics")==0){
#ifdef K3_COUNTERS
 for(auto&f:all){auto e=oracle(f);field::Frontier q;auto values=f.input;for(int warm=0;warm<2;++warm){values=f.input;seed(f,q);visits=allocations=bytes=pushes=pops=grows=wraps=peakPending=0;visitDigest=1469598103934665603ULL;charge=true;kernel(f,values,q);charge=false;check(f,values,e,q);std::printf("METRICS fixture=%s warm=%d visits=%llu visitation=%016llx allocation_calls=%llu allocation_requested_bytes=%llu pushes=%llu pops=%llu grows=%llu wraps=%llu peak_pending=%llu retained_entries=%zu retained_bytes=%zu stack_bytes=%zu\n",f.name.c_str(),warm,(unsigned long long)visits,(unsigned long long)visitDigest,(unsigned long long)allocations,(unsigned long long)bytes,(unsigned long long)pushes,(unsigned long long)pops,(unsigned long long)grows,(unsigned long long)wraps,(unsigned long long)peakPending,q.capacity(),q.retainedBytes(),std::size_t(K3_BATCH)*sizeof(int));}}return 0;
#else
 throw std::runtime_error("metrics require separate instrumentation");
#endif
 }
 if(!benchmark)throw std::runtime_error("--benchmark index iterations");auto idx=std::strtoul(argv[2],nullptr,10),iterations=std::strtoul(argv[3],nullptr,10);if(idx>=all.size()||!iterations)throw std::runtime_error("invalid benchmark args");const auto&f=all[idx];auto e=oracle(f);auto values=f.input;field::Frontier q;seed(f,q);kernel(f,values,q);check(f,values,e,q);std::uint64_t wall=0,cpu=0;
 for(unsigned r=0;r<iterations;++r){values=f.input;seed(f,q);auto cs=std::clock();auto start=std::chrono::steady_clock::now();kernel(f,values,q);auto stop=std::chrono::steady_clock::now();auto ce=std::clock();wall+=std::chrono::duration_cast<std::chrono::nanoseconds>(stop-start).count();cpu+=std::uint64_t(ce-cs)*1000000000ULL/CLOCKS_PER_SEC;check(f,values,e,q);}
 std::printf("K3_TIMING fixture=%s batch=%d iterations=%lu input=%016llx output=%016llx wall_ns=%llu cpu_ns=%llu retained_entries=%zu retained_bytes=%zu stack_bytes=%zu\n",f.name.c_str(),K3_BATCH,iterations,(unsigned long long)hash(f.input),(unsigned long long)hash(values),(unsigned long long)wall,(unsigned long long)cpu,q.capacity(),q.retainedBytes(),std::size_t(K3_BATCH)*sizeof(int));return 0;
}catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
