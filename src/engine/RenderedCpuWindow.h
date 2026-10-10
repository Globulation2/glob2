// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "common/ProcessCpuClock.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace RenderedCpuDiagnostics {
inline std::uint64_t parseTicks(std::string_view value) {
    if(value.empty())throw std::invalid_argument("Rendered CPU tick range must be a positive decimal integer");
    std::uint64_t result=0;
    for(const char c:value) {
        if(c<'0'||c>'9'||result>(std::numeric_limits<std::uint32_t>::max()-unsigned(c-'0'))/10)
            throw std::invalid_argument("Rendered CPU tick range must fit a simulation tick");
        result=result*10+unsigned(c-'0');
    }
    if(!result)throw std::invalid_argument("Rendered CPU tick range must be positive");
    return result;
}
struct Range {std::uint64_t initial=0,start=0,end=0;};
inline Range range(std::uint64_t initial,std::uint64_t warm,std::uint64_t measure,std::uint64_t target) {
    if(!warm||!measure||initial>target||warm>target-initial||measure>target-initial-warm
        ||target>std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Rendered CPU range must fit the fixed ending tick after the loaded initial tick");
    return {initial,initial+warm,initial+warm+measure};
}
// Fixed storage; only two successful metadata calls, no allocation, lock, I/O
// or statistics. Counters are independent completed-job lifetime observations.
template<class Counters> class Window {
public:
    using Clock=std::uint64_t(*)();
    struct Endpoint {
        std::uint64_t tick=0,processCpuNs=0,ownerCpuNs=0,wallNs=0,ownerTid=0;
        Counters counters{};
        bool captured=false;
    };
    Range ticks;
    std::array<Endpoint,2> endpoints{};
    bool missedBoundary=false,ownerChanged=false;
    explicit Window(Range r,Clock process=glob2::processCpuNs,Clock owner=glob2::threadCpuNs,
                    Clock wall=monotonicNs,Clock tid=glob2::nativeThreadId)
        :ticks(r),process(process),owner(owner),wall(wall),tid(tid){}
    template<class Metadata> void onTick(std::uint64_t tick,Metadata&& metadata) {
        const auto identity=currentOwnerIdentity();
        if(!ownerIdentity){ownerIdentity=identity;ownerTid=tid();}
        if(ownerIdentity!=identity)ownerChanged=true;
        for(unsigned i=0;i<2;++i) {
            auto& e=endpoints[i];const auto expected=i?ticks.end:ticks.start;
            if(e.captured||tick<expected)continue;
            if(tick!=expected){missedBoundary=true;continue;}
            e.tick=tick;
            if(!i) {
                e.counters=metadata();e.ownerTid=ownerTid;
                e.ownerCpuNs=owner();e.wallNs=wall();e.processCpuNs=process();
            } else {
                e.processCpuNs=process();e.wallNs=wall();e.ownerCpuNs=owner();
                e.ownerTid=ownerChanged?0:ownerTid;e.counters=metadata();
            }
            e.captured=true;
        }
    }
    bool processValid()const noexcept{return valid(&Endpoint::processCpuNs)&&!missedBoundary;}
    bool ownerValid()const noexcept{return valid(&Endpoint::ownerCpuNs)&&endpoints[0].ownerTid
        &&endpoints[0].ownerTid==endpoints[1].ownerTid&&!missedBoundary&&!ownerChanged;}
    bool wallValid()const noexcept{return valid(&Endpoint::wallNs)&&!missedBoundary;}
private:
    Clock process,owner,wall,tid;
    const void* ownerIdentity=nullptr;
    std::uint64_t ownerTid=0;
    static const void* currentOwnerIdentity(){static thread_local const unsigned char identity=0;return &identity;}
    bool valid(std::uint64_t Endpoint::* member)const noexcept {
        return endpoints[0].captured&&endpoints[1].captured&&endpoints[0].*member
            &&endpoints[1].*member>=endpoints[0].*member;
    }
    static std::uint64_t monotonicNs(){return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();}
};
}
