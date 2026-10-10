// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "OpenCLGradient.h"
#include "ThreadCpuClock.h"
#include <limits>
namespace gradient_kernel {
// Fixed lane-local storage. A scope subtracts completed nested scopes, so
// preparation can contain uploads/arguments without counting their CPU twice.
// Invalid children invalidate their enclosing scope, preserving valid siblings
// but explicitly preventing a complete conservation claim for that batch.
class OpenCLCpuAttribution {
    OpenCLApiCpuCounters& counters;
    std::uint64_t charged=0;
    unsigned depth=0;
    void* clockContext=nullptr;
    std::uint64_t (*clock)(void*) noexcept=[](void*) noexcept {return glob2::threadCpuNs();};
    std::uint64_t now() noexcept {++counters.clockReads;return clock(clockContext);}
    bool add(std::uint64_t& target,std::uint64_t value) noexcept {
        if(value>std::numeric_limits<std::uint64_t>::max()-target){++counters.reconciliationErrors;return false;}
        target+=value;return true;
    }
public:
    unsigned mode=0;
    explicit OpenCLCpuAttribution(OpenCLApiCpuCounters& counters):counters(counters){}
    void setClockForTesting(void* context,std::uint64_t (*read)(void*) noexcept){clockContext=context;clock=read;}
    class Scope {
        OpenCLCpuAttribution* owner=nullptr;
        OpenCLCpuCategory category;
        std::uint64_t start=0,before=0,invalidBefore=0,reconciliationBefore=0;
        bool outer=false;
    public:
        Scope(OpenCLCpuAttribution& owner,OpenCLCpuCategory category) noexcept:category(category) {
            if(!owner.mode)return;
            this->owner=&owner;outer=owner.depth++==0;
            before=owner.charged;invalidBefore=owner.counters.invalidScopes;
            reconciliationBefore=owner.counters.reconciliationErrors;
            ++owner.counters.calls[unsigned(category)];start=owner.now();
            if(owner.mode==2){
                const auto end=owner.now();
                if(!start || end<start)++owner.counters.invalidScopes;
                else owner.add(owner.counters.controlBracketNs,end-start);
            }
        }
        Scope(const Scope&)=delete;
        ~Scope(){close();}
        void close() noexcept {
            if(!owner)return;
            auto& state=*owner;owner=nullptr;--state.depth;
            if(state.mode==2)return;
            const auto end=state.now();
            if(!start || end<start || state.counters.invalidScopes!=invalidBefore){++state.counters.invalidScopes;return;}
            if(state.counters.reconciliationErrors!=reconciliationBefore){++state.counters.reconciliationErrors;return;}
            const auto elapsed=end-start;
            const auto nested=state.charged-before;
            if(nested>elapsed){++state.counters.reconciliationErrors;return;}
            const auto exclusive=elapsed-nested;
            if(!state.add(state.counters.ns[unsigned(category)],exclusive) || !state.add(state.charged,exclusive))return;
            if(outer)state.add(state.counters.coveredNs,elapsed);
        }
    };
};
}
