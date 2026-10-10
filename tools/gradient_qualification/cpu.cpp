// SPDX-License-Identifier: GPL-3.0-or-later
// Production prepared-bucket comparator for the development corpus. This is
// deliberately separate from the heap oracle and retains real queue capacity.
#include "field/RuntimeTerrainGradient.h"
#include "common/ThreadCpuClock.h"
#include <chrono>
#include <memory>
#include <unordered_map>

namespace {
struct CPU {
    GradientWorkspace workspace;
    std::vector<std::uint8_t> classes;
    TerrainRegistry::Movement movement;
    int classicSwim=-1;
    unsigned landProfile=256,queueBuckets=64;
    std::uint64_t preparationNs=0,seedCopiedBytes=0;
    bool noOp=false,preparedNow=false;
};
}
extern "C" void* cpu_create() {try{return new CPU;}catch(...){return nullptr;}}
extern "C" void cpu_destroy(void* storage) {delete static_cast<CPU*>(storage);}
extern "C" unsigned cpu_cost_limit() {return gradient_kernel::COST_LIMIT;}
extern "C" void cpu_metrics(void* storage,std::uint64_t* output) {
    const auto& state=*static_cast<CPU*>(storage);
    std::uint64_t bytes=sizeof(CPU)+state.classes.capacity()+
        state.movement.profiles.capacity()*sizeof(gradient_kernel::EntrySteps)+
        state.workspace.deferredSeeds.capacity()*sizeof(std::pair<int,int>);
    for(const auto& bucket:state.workspace.buckets)bytes+=bucket.cells.capacity()*sizeof(std::uint32_t);
    if(state.workspace.terrain){
        bytes+=sizeof(TerrainGradientWorkspace)+state.workspace.terrain->buckets.capacity()*sizeof(GradientBucket);
        for(const auto& bucket:state.workspace.terrain->buckets)bytes+=bucket.cells.capacity()*sizeof(std::uint32_t);
    }
    // The optional prepared variant is inline in sizeof(CPU), not an extra
    // allocation. Payload counts exclude allocator/control-block overhead;
    // output/caller input storage is separate from retained workspace.
    if(state.workspace.backendSession)bytes+=sizeof(gradient_kernel::BackendSession);
    output[0]=state.noOp ? 3 : state.classicSwim==0 ? 0 : state.classicSwim>0 ? 1 : 2;
    output[1]=state.classicSwim>=0 ? gradient_kernel::BUCKETS : state.queueBuckets;
    output[2]=state.movement.profiles.size();output[3]=bytes;
    output[4]=state.preparationNs;output[5]=state.seedCopiedBytes;output[6]=state.preparedNow;
    output[7]=state.classicSwim<0 ? 256 : unsigned(state.classicSwim);
}
extern "C" int run_cpu(void* storage,const std::uint16_t* seeds,const std::uint32_t* costs,
 std::uint16_t* output,unsigned width,unsigned height,unsigned cap,double* stats)
{
    using namespace gradient_kernel;
    if(!storage || !width || !height || cap>unsigned(COST_LIMIT))return -1;
    const auto started=std::chrono::steady_clock::now();
    const auto cpuStarted=glob2::threadCpuNs();
    try {
        auto& state=*static_cast<CPU*>(storage);
        state.preparationNs=0;state.seedCopiedBytes=std::uint64_t(width)*height*2;
        state.noOp=false;state.preparedNow=false;
        const field::Grid grid(width,height);
        std::copy_n(seeds,grid.cells(),output);
        if(alreadyFixedGradient(std::span<const std::uint16_t>(output,grid.cells()))) {
            state.noOp=true;
            stats[0]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            const auto cpuEnded=glob2::threadCpuNs();
            stats[2]=cpuStarted && cpuEnded && cpuEnded>=cpuStarted;
            stats[1]=stats[2] ? double(cpuEnded-cpuStarted)/1e6 : 0;
            return 0;
        }
        if(!state.movement.prepared) {
            const auto preparationStarted=std::chrono::steady_clock::now();
            state.preparedNow=true;
            state.classes.resize(grid.cells());
            std::unordered_map<std::uint32_t,unsigned> identities;
            for(std::size_t i=0;i<grid.cells();++i) {
                const auto packed=costs[i];
                const unsigned cardinal=packed&65535,diagonal=packed>>16;
                if(!cardinal || !diagonal || cardinal>=256 || diagonal>=256)return -2;
                while(state.queueBuckets<=std::max(cardinal,diagonal))state.queueBuckets*=2;
                auto found=identities.find(packed);
                if(found==identities.end()) {
                    const auto id=identities.size();if(id==256)return -3;
                    found=identities.emplace(packed,unsigned(id)).first;
                    state.movement.profiles.push_back({cardinal,diagonal});
                }
                state.classes[i]=found->second;
                if(cardinal==LAND_STEPS.cardinal && diagonal==LAND_STEPS.diagonal)state.landProfile=found->second;
            }
            // Match Movement::prepare exactly: duplicate the first present
            // profile for padding, and use the compact production variant.
            // Keep this paid cold construction inside the timed request.
            const auto prepare=[&]<std::size_t N>() {
                std::array<EntrySteps,N> entries;entries.fill(state.movement.profiles.front());
                std::copy(state.movement.profiles.begin(),state.movement.profiles.end(),entries.begin());
                state.movement.prepared=PreparedTerrainCosts<N>(entries);
            };
            if(state.movement.profiles.size()<=8) prepare.template operator()<8>();
            else prepare.template operator()<256>();
            // Preserve the production classic land/water specialization when
            // these paid profile contents can represent a real swim class.
            for(int swim=0;swim<int(std::size(WATER_STEP));++swim){
                const auto water=swim ? entrySteps(WATER_STEP[swim]) : LAND_STEPS;
                if(std::all_of(state.movement.profiles.begin(),state.movement.profiles.end(),[&](auto cost){
                    return (cost.cardinal==LAND_STEPS.cardinal && cost.diagonal==LAND_STEPS.diagonal) ||
                           (swim && cost.cardinal==water.cardinal && cost.diagonal==water.diagonal);
                })){state.classicSwim=swim;break;}
            }
            state.preparationNs=std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now()-preparationStarted).count();
        }
        if(state.classes.size()!=grid.cells())return -4;
        if(state.classicSwim>=0)
            propagateFieldCPU(output,state.classicSwim,int(cap),grid,state.workspace,
                [&](std::size_t i){return state.classes[i]!=state.landProfile;});
        else {
            const auto run=[&]<unsigned Buckets>(){runtime_terrain::propagate<Buckets,true,false>(
                output,int(cap),grid,state.workspace,[&](std::size_t i){return state.classes[i];},state.movement);};
            if(state.queueBuckets==64)run.template operator()<64>();
            else if(state.queueBuckets==128)run.template operator()<128>();
            else run.template operator()<256>();
        }
    } catch(...) {return -5;}
    stats[0]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    const auto cpuEnded=glob2::threadCpuNs();
    stats[2]=cpuStarted && cpuEnded && cpuEnded>=cpuStarted;
    stats[1]=stats[2] ? double(cpuEnded-cpuStarted)/1e6 : 0;
    return 0;
}
