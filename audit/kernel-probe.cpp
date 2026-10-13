#include "field/GradientRelaxation.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace gradient_kernel;
__attribute__((noinline)) void AuditSolve(unsigned short* g, int swim, field::Grid grid,
                                        std::array<GradientBucket, BUCKETS>& queues) {
#ifdef AUDIT_SCOPE
    CPUSelectionScope choice(Backend::CPU);
#endif
    for(auto& q:queues) q.clear();
    std::size_t pending=0;
    for(std::size_t i=0;i<grid.cells();++i) if(g[i]>1){queues[0].push(i);++pending;}
    const auto water=[](std::size_t i){return ((i*17+i/512)%23)<7;};
    auto sweep=[&](auto weighted){
      for(int c=0;pending&&c<=COST_LIMIT;++c)
#ifdef AUDIT_HOIST
        expandBucketForCPU<decltype(weighted)::value,true>(g,queues.data(),pending,c,COST_LIMIT,grid,entrySteps(WATER_STEP[swim]),water);
#else
        expandBucket<decltype(weighted)::value>(g,queues.data(),pending,c,COST_LIMIT,grid,entrySteps(WATER_STEP[swim]),water);
#endif
    };
    if(weightedClass(swim)) sweep(std::true_type{}); else sweep(std::false_type{});
}
int main(int argc,char**argv){
    const int n=argc>1?std::atoi(argv[1]):512, rounds=argc>2?std::atoi(argv[2]):8;
    std::array<GradientBucket, BUCKETS> queues;
    std::vector<unsigned short> original(n*n,1),data;
    for(int i=0;i<n*n;++i) {if(i%97==0)original[i]=0;if(i%8191==17)original[i]=65535;}
    unsigned long long hash=0;
    auto begin=std::chrono::steady_clock::now();
    for(int r=0;r<rounds;++r) for(int s=0;s<8;++s){data=original;AuditSolve(data.data(),s,{n,n},queues);for(auto v:data)hash=hash*33+v;}
    std::printf("%llu %lld\n",hash,(long long)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count());
}
