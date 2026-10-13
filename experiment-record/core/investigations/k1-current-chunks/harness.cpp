// Diagnostic standalone full-field kernel. Independent heap oracle derived
// unchanged from frozen src/field/OpenCLGradientTest.cpp::oracle.
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <queue>
#include <random>
#include <stdexcept>
#include <vector>
#include <string>
#ifdef K1_WORK_COUNTERS
#include <new>
static bool charging=false;
static std::uint64_t allocations=0,allocationBytes=0,reserveCalls=0,reserveRequested=0,grows=0;
void* operator new(std::size_t n){if(charging){++allocations;allocationBytes+=n;}if(void*p=std::malloc(n))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#define K1_RESERVE_EVENT(extra,growing) do{if(charging){++reserveCalls;reserveRequested+=(extra);grows+=(growing);}}while(0)
#endif
#include "GradientPropagation.h"
using namespace gradient_kernel;
struct Fixture{std::string name;int w,h,swim,cap;std::vector<std::uint16_t> seeds;std::vector<std::uint8_t> water;};
std::uint64_t hash(const std::vector<std::uint16_t>&x){std::uint64_t h=1469598103934665603ULL;for(auto v:x){h=(h^(v&255))*1099511628211ULL;h=(h^(v>>8))*1099511628211ULL;}return h;}
std::uint64_t inputHash(const Fixture&f){auto h=hash(f.seeds);for(auto v:f.water)h=(h^v)*1099511628211ULL;return h;}
std::vector<std::uint16_t> oracle(const Fixture&f){
 field::Grid grid(f.w,f.h);auto out=f.seeds;using Item=std::pair<unsigned,std::size_t>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>>q;
 for(std::size_t i=0;i<out.size();++i)if(out[i]>1)q.emplace(65535-out[i],i);
 while(!q.empty()){auto [cost,i]=q.top();q.pop();if(cost>unsigned(f.cap)||65535u-out[i]!=cost)continue;
  const auto step=weightedClass(f.swim)&&f.water[i]?entrySteps(WATER_STEP[f.swim]):LAND_STEPS;
  for(auto d:field::Surrounding){auto j=grid.index(int(i%f.w)+d.x,int(i/f.w)+d.y);const auto next=cost+(d.x&&d.y?step.diagonal:step.cardinal);if(out[j]&&next<=unsigned(f.cap)&&65535-next>out[j]){out[j]=std::uint16_t(65535-next);q.emplace(next,j);}}
 }return out;
}
std::vector<Fixture> fixtures(){std::vector<Fixture> all;
 auto add=[&](int w,int h,int swim,std::string mode,int cap){
  Fixture f{std::to_string(w)+"x"+std::to_string(h)+"-s"+std::to_string(swim)+"-"+mode+"-c"+std::to_string(cap),w,h,swim,cap,std::vector<std::uint16_t>(std::size_t(w)*h,1),std::vector<std::uint8_t>(std::size_t(w)*h)};
  std::mt19937 rng(0x71A6D19u+unsigned(w*257+h*19+swim*31));
  for(std::size_t i=0;i<f.seeds.size();++i){int x=i%w,y=i/w;f.water[i]=(rng()%4==0);bool forbidden=(swim==0&&f.water[i])||(mode=="barrier"&&((x==w/2&&y!=h/3)||(y==h/2&&x!=w/3)))||(mode=="sparse"&&rng()%19==0);
   if(forbidden)f.seeds[i]=0;
   else if(mode=="allgoal")f.seeds[i]=65535;
   else if(mode!="none"&&((mode=="clustered"&&x<w/8+1&&y<h/8+1&&rng()%3==0)||(mode!="clustered"&&rng()%1024==0)))f.seeds[i]=mode=="deferred"?std::uint16_t(65535-(rng()%1000)):65535;
  }
  if(mode!="none"&&mode!="allgoal"){f.seeds[0]=65535;f.water[0]=0;f.seeds.back()=std::uint16_t(65535-(mode=="deferred"?650:0));f.water.back()=0;}
  all.push_back(std::move(f));
 };
 for(int size:{128,256})for(int swim:{0,3,6}){for(auto mode:{"sparse","clustered","barrier"})add(size,size,swim,mode,COST_LIMIT);for(auto mode:{"deferred","none","allgoal"})add(size,size,swim,mode,120);}
 for(auto dims:{std::pair{1,1},{1,32},{32,1},{2,17},{17,2},{2,2},{7,13},{4,5}})for(int swim:{0,3,6})for(int cap:{0,42,300})add(dims.first,dims.second,swim,"deferred",cap);
 return all;
}
void propagate(const Fixture&f,std::vector<std::uint16_t>&out,GradientWorkspace&workspace){propagateFieldCPU(out.data(),f.swim,f.cap,field::Grid(f.w,f.h),workspace,[&](std::size_t i){return bool(f.water[i]);});}
void same(const std::vector<std::uint16_t>&a,const std::vector<std::uint16_t>&b,const Fixture&f){if(a!=b){std::size_t i=0;while(a[i]==b[i])++i;throw std::runtime_error("oracle mismatch "+f.name+" cell"+std::to_string(i));}}
std::pair<std::size_t,std::size_t>capacity(const GradientWorkspace&w){std::size_t size=0,cap=0;for(auto&b:w.buckets){size+=b.cells.size();cap+=b.cells.capacity();}return{size*4+w.deferredSeeds.size()*sizeof(std::pair<int,int>),cap*4+w.deferredSeeds.capacity()*sizeof(std::pair<int,int>)};}
int main(int argc,char**argv){try{
 auto all=fixtures();std::uint64_t cells=0,aggregate=1469598103934665603ULL;
 // Preparation verifies entire inventory. Each optional timed invocation verifies its selected fixture before starting any timer.
 for(std::size_t fi=0;fi<all.size();++fi){if(argc==4 && std::strcmp(argv[1],"--benchmark")==0 && fi!=std::strtoul(argv[2],nullptr,10))continue;auto&f=all[fi];auto expected=oracle(f);auto out=f.seeds;GradientWorkspace workspace;propagate(f,out,workspace);same(out,expected,f);cells+=out.size();aggregate=(aggregate^hash(out))*1099511628211ULL;std::printf("ORACLE fixture=%s input=%016llx output=%016llx cells=%zu\n",f.name.c_str(),(unsigned long long)inputHash(f),(unsigned long long)hash(out),out.size());}
 std::printf("ORACLE_PASS fixtures=%zu cells=%llu aggregate=%016llx chunk=%zu\n",all.size(),(unsigned long long)cells,(unsigned long long)aggregate,CHUNK);
 if(argc<2)return 0;
 std::string mode=argv[1];if(mode=="--metrics"){
#ifdef K1_WORK_COUNTERS
 for(auto&f:all){GradientWorkspace workspace;auto expected=oracle(f);auto out=f.seeds;
  for(int warm=0;warm<2;++warm){out=f.seeds;allocations=allocationBytes=reserveCalls=reserveRequested=grows=0;charging=true;propagate(f,out,workspace);charging=false;same(out,expected,f);auto [storage,cap]=capacity(workspace);std::printf("METRICS fixture=%s warm=%d allocation_calls=%llu allocation_requested_bytes=%llu reserve_calls=%llu reserve_requested_entries=%llu grows=%llu retained_storage_bytes=%zu retained_capacity_bytes=%zu deferred_capacity=%zu\n",f.name.c_str(),warm,(unsigned long long)allocations,(unsigned long long)allocationBytes,(unsigned long long)reserveCalls,(unsigned long long)reserveRequested,(unsigned long long)grows,storage,cap,workspace.deferredSeeds.capacity());}
 }
 return 0;
#else
 throw std::runtime_error("metrics require separate instrumented binary");
#endif
 }
 if(mode!="--benchmark"||argc!=4)throw std::runtime_error("use --benchmark fixture_index iterations");auto index=std::strtoul(argv[2],nullptr,10);auto iterations=std::strtoul(argv[3],nullptr,10);if(index>=all.size()||iterations<1)throw std::runtime_error("bad benchmark args");const auto&f=all[index];auto expected=oracle(f);auto out=f.seeds;GradientWorkspace workspace;propagate(f,out,workspace);same(out,expected,f);
 // Warm retained workspace; input copy, oracle, checks and output excluded.
 std::uint64_t wall=0,cpu=0;
 for(unsigned r=0;r<iterations;++r){out=f.seeds;auto cs=std::clock();auto start=std::chrono::steady_clock::now();propagate(f,out,workspace);auto end=std::chrono::steady_clock::now();auto ce=std::clock();wall+=std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();cpu+=std::uint64_t(ce-cs)*1000000000ULL/CLOCKS_PER_SEC;same(out,expected,f);}
 auto [storage,cap]=capacity(workspace);std::printf("K1_TIMING fixture=%s chunk=%zu iterations=%lu input=%016llx output=%016llx wall_ns=%llu cpu_ns=%llu retained_storage_bytes=%zu retained_capacity_bytes=%zu\n",f.name.c_str(),CHUNK,iterations,(unsigned long long)inputHash(f),(unsigned long long)hash(out),(unsigned long long)wall,(unsigned long long)cpu,storage,cap);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
