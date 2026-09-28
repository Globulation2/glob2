#pragma once
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <vector>
namespace GradientProfile {
inline std::uint64_t now() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct Metrics {
 std::uint64_t init=0, setup=0, seeds=0, weightedSeeds=0, propagation=0, growth=0;
 std::uint64_t builds=0, weightedBuilds=0, cells=0, weightedCells=0, pushes=0, growths=0, capacityBytes=0, pops=0, stale=0, layers=0;
 ~Metrics() { std::fprintf(stderr,"GRADIENT_COST_PROFILE init_ns=%llu setup_ns=%llu seeds_ns=%llu weighted_seeds_ns=%llu propagation_ns=%llu growth_ns=%llu builds=%llu weighted_builds=%llu cells=%llu weighted_cells=%llu pushes=%llu growths=%llu capacity_bytes_added=%llu pops=%llu stale=%llu layers=%llu\n",(unsigned long long)init,(unsigned long long)setup,(unsigned long long)seeds,(unsigned long long)weightedSeeds,(unsigned long long)propagation,(unsigned long long)growth,(unsigned long long)builds,(unsigned long long)weightedBuilds,(unsigned long long)cells,(unsigned long long)weightedCells,(unsigned long long)pushes,(unsigned long long)growths,(unsigned long long)capacityBytes,(unsigned long long)pops,(unsigned long long)stale,(unsigned long long)layers); }
};
inline Metrics metrics;
inline bool active=false;
struct Active { bool previous=active; Active(){active=true;} ~Active(){active=previous;} };
struct Timer {
 std::uint64_t &total, start=now(); bool running=true;
 explicit Timer(std::uint64_t &v):total(v){}
 void stop(){if(running){total+=now()-start;running=false;}}
 ~Timer(){stop();}
};
inline void push(std::vector<int> &v, int cell) {
 if (!active) {v.push_back(cell);return;}
 ++metrics.pushes;
 if(v.size()!=v.capacity()){v.push_back(cell);return;}
 auto capacity=v.capacity();auto start=now();v.push_back(cell);metrics.growth+=now()-start;
 ++metrics.growths;metrics.capacityBytes+=(v.capacity()-capacity)*sizeof(int);
}
}
