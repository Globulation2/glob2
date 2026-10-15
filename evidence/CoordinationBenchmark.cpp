// Development-only comparative harness; never linked into the game.
#include "ComputeExecutor.h"
#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdint>

struct alignas(64) Result { std::uint64_t value=0; unsigned calls=0; };
struct Work {
    std::vector<Result> result;
    std::array<std::uint64_t,32> laneState{};
    unsigned iterations=0, pattern=0, lane=0;
};
static std::uint64_t mix(std::uint64_t x,unsigned n) {
    for(unsigned i=0;i<n;++i) {x^=x>>12; x^=x<<25; x^=x>>27; x*=2685821657736338717ull;}
    return x;
}
static void runJob(void* input,std::size_t index) {
    auto& w=*static_cast<Work*>(input);
    unsigned count=w.iterations;
    if(w.pattern==2) {const unsigned factor[]={1,2,4,16,128};count*=factor[(index*17+index/7)%5];}
    auto x=0x9e3779b97f4a7c15ull+index;
    if(w.pattern>=3) x^=w.laneState[w.lane]++;
    w.result[index].value=mix(x,count);
    ++w.result[index].calls;
}
int main(int argc,char** argv) {
    if(argc!=7) {std::cerr<<"usage: benchmark participants policy pattern rounds jobs iterations\n";return 2;}
    const unsigned participants=std::stoul(argv[1]),rounds=std::stoul(argv[4]),jobs=std::stoul(argv[5]),iterations=std::stoul(argv[6]);
    const std::string policy=argv[2],pattern=argv[3];
    if(!participants || !rounds || !jobs || participants>256 || jobs>4096) return 2;
    setenv("GLOB2_DEFERRED_DISPATCH",policy.c_str(),1);
    ComputeExecutor executor; executor.configure(participants);
    const unsigned kind=pattern=="burst"?0:pattern=="balanced"?1:pattern=="skew"?2:pattern=="lanes"?3:pattern=="single-lane"?4:99;
    if(kind==99) return 2;
    unsigned laneCount=kind==3?8:1;
    std::vector<std::unique_ptr<Work>> contexts;
    std::vector<ComputeExecutor::Group> groups;
    for(unsigned lane=0;lane<laneCount;++lane) {
        auto w=std::make_unique<Work>();w->result.resize(jobs);w->iterations=iterations;w->pattern=kind;w->lane=lane;
        groups.push_back({jobs,{runJob,w.get()},kind>=3?lane:ComputeExecutor::NoLane});contexts.push_back(std::move(w));
    }
    const auto step=[&] {auto batch=executor.submit(groups);executor.join(batch);};
    for(unsigned i=0;i<32;++i) step();
    for(auto& w:contexts) {for(auto& cell:w->result) cell.calls=0;w->laneState={};}
    auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<rounds;++i) step();
    auto end=std::chrono::steady_clock::now();
    std::uint64_t digest=1469598103934665603ull;
    for(auto& w:contexts) {
        if(kind>=3 && w->laneState[w->lane]!=rounds*jobs) return 3;
        for(const auto& cell:w->result) {if(cell.calls!=rounds) return 3;digest=(digest^cell.value)*1099511628211ull;}
    }
    std::cout<<"COORDINATION_RESULT {\"participants\":"<<executor.threadCount()<<",\"requested_policy\":\""<<policy<<"\",\"pattern\":\""<<pattern<<"\",\"rounds\":"<<rounds<<",\"jobs_per_round\":"<<jobs*laneCount<<",\"iterations\":"<<iterations<<",\"wall_ns\":"<<std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()<<",\"digest\":"<<digest<<",\"jobs_checked\":"<<std::uint64_t(rounds)*jobs*laneCount<<"}\n";
}
