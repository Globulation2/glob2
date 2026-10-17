// Temporary local experiment: instrumentation only, never simulation input.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <algorithm>
#include <stdexcept>
#include <array>
#include <map>
#include <tuple>

namespace BuildingGradientCpuProbe {
inline std::atomic<std::uint64_t> cpuNs{0}, elapsedNs{0}, jobs{0}, maximumJobNs{0};
inline std::uint64_t maxBatch=0, maxPending=0, maxQueuedAfterBoundary=0, deferredBoundaries=0;
inline std::uint64_t boundary=0, delaySamples=0, delayTotal=0, delayMaximum=0, delayedRequests=0, maxTeamBatch=0;
inline std::array<std::uint64_t,4096> delayHistogram{};
using RequestKey=std::tuple<int,unsigned,unsigned,int,unsigned>;
inline std::map<RequestKey,std::uint64_t> firstBoundary;
template<class R> RequestKey key(const R& r) { return {r.team,r.building,r.identity,r.slot,r.epoch}; }
template<class R> void requested(const R& r) { firstBoundary[key(r)]=boundary+1; }
template<class R> void discardRequest(const R& r) { firstBoundary.erase(key(r)); }
template<class R> void admittedRequest(const R& r) {
    auto it=firstBoundary.find(key(r));
    if(it==firstBoundary.end()) throw std::logic_error("Untracked benchmark request");
    const auto age=boundary-it->second;
    firstBoundary.erase(it);
    ++delaySamples; delayTotal+=age; delayMaximum=std::max(delayMaximum,age); delayedRequests+=age!=0;
    ++delayHistogram[std::min<std::uint64_t>(age,delayHistogram.size()-1)];
}
inline void maximum(std::atomic<std::uint64_t>& value, std::uint64_t candidate) {
    auto old=value.load(std::memory_order_relaxed);
    while (old<candidate && !value.compare_exchange_weak(old,candidate,std::memory_order_relaxed)) {}
}
inline std::uint64_t threadCpuNs() {
#ifdef CLOCK_THREAD_CPUTIME_ID
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)!=0) std::terminate();
    return std::uint64_t(t.tv_sec)*1000000000+std::uint64_t(t.tv_nsec);
#else
#error This local CPU experiment requires a thread CPU clock
#endif
}
struct Work {
    const std::uint64_t cpu=threadCpuNs();
    const std::chrono::steady_clock::time_point begin=std::chrono::steady_clock::now();
    ~Work() {
        const auto spent=threadCpuNs()-cpu;
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
        cpuNs.fetch_add(spent,std::memory_order_relaxed);
        elapsedNs.fetch_add(elapsed,std::memory_order_relaxed);
        jobs.fetch_add(1,std::memory_order_relaxed);
        maximum(maximumJobNs,elapsed);
    }
};
}
