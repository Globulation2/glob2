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
    bool classic=false;
};
}
extern "C" void* cpu_create() {try{return new CPU;}catch(...){return nullptr;}}
extern "C" void cpu_destroy(void* storage) {delete static_cast<CPU*>(storage);}
extern "C" unsigned cpu_cost_limit() {return gradient_kernel::COST_LIMIT;}
extern "C" int run_cpu(void* storage,const std::uint16_t* seeds,const std::uint32_t* costs,
 std::uint16_t* output,unsigned width,unsigned height,unsigned cap,double* stats)
{
    using namespace gradient_kernel;
    if(!storage)return -1;
    const auto started=std::chrono::steady_clock::now();
    const auto cpuStarted=glob2::threadCpuNs();
    try {
        auto& state=*static_cast<CPU*>(storage);
        const field::Grid grid(width,height);
        std::copy_n(seeds,grid.cells(),output);
        if(alreadyFixedGradient(std::span<const std::uint16_t>(output,grid.cells()))) {
            stats[0]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            stats[1]=double(glob2::threadCpuNs()-cpuStarted)/1e6;
            return 0;
        }
        if(!state.movement.prepared) {
            state.classes.resize(grid.cells());
            std::unordered_map<std::uint32_t,unsigned> identities;
            for(std::size_t i=0;i<grid.cells();++i) {
                const auto packed=costs[i];
                const unsigned cardinal=packed&65535,diagonal=packed>>16;
                // This comparator uses the same 64-bucket eager kernel as
                // production. Wider-cost runtime searches are another class.
                if(!cardinal || !diagonal || cardinal>=BUCKETS || diagonal>=BUCKETS)return -2;
                auto found=identities.find(packed);
                if(found==identities.end()) {
                    const auto id=identities.size();if(id==256)return -3;
                    found=identities.emplace(packed,unsigned(id)).first;
                    state.movement.profiles.push_back({cardinal,diagonal});
                }
                state.classes[i]=found->second;
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
            const auto first=state.movement.profiles.front();
            state.classic=identities.size()==1 && first.cardinal==LAND_STEPS.cardinal &&
                first.diagonal==LAND_STEPS.diagonal;
        }
        if(state.classes.size()!=grid.cells())return -4;
        if(state.classic)
            propagateFieldCPU(output,0,int(cap),grid,state.workspace,[](std::size_t){return false;});
        else runtime_terrain::propagate<BUCKETS,true,false>(output,int(cap),grid,state.workspace,
            [&](std::size_t i){return state.classes[i];},state.movement);
    } catch(...) {return -5;}
    stats[0]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    stats[1]=double(glob2::threadCpuNs()-cpuStarted)/1e6;
    return 0;
}
