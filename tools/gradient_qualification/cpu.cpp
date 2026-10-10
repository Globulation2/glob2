// SPDX-License-Identifier: GPL-3.0-or-later
// Production prepared-bucket comparator for the development corpus. This is
// deliberately separate from the heap oracle and retains real queue capacity.
#include "field/TerrainGradient.h"
#include "common/ThreadCpuClock.h"
#include <chrono>
#include <memory>
#include <unordered_map>

namespace {
struct CPU {
    GradientWorkspace workspace;
    std::vector<std::uint8_t> classes;
    std::unique_ptr<gradient_kernel::PreparedTerrainCosts<256>> profile;
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
        if(!state.profile) {
            state.classes.resize(grid.cells());
            std::array<EntrySteps,256> entries;entries.fill(LAND_STEPS);
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
                    entries[id]={cardinal,diagonal};
                }
                state.classes[i]=found->second;
            }
            state.profile=std::make_unique<PreparedTerrainCosts<256>>(entries);
            state.classic=identities.size()==1 && entries[0].cardinal==LAND_STEPS.cardinal &&
                entries[0].diagonal==LAND_STEPS.diagonal;
        }
        if(state.classes.size()!=grid.cells())return -4;
        if(state.classic)
            propagateFieldCPU(output,0,int(cap),grid,state.workspace,[](std::size_t){return false;});
        else propagatePreparedTerrainFieldCPU(output,int(cap),grid,state.workspace,*state.profile,
            [&](std::size_t i){return state.profile->terrainClasses[state.classes[i]];});
    } catch(...) {return -5;}
    stats[0]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    stats[1]=double(glob2::threadCpuNs()-cpuStarted)/1e6;
    return 0;
}
