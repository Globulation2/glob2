// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>
#include <cstdint>
#include <ctime>

namespace MovementPlanning
{
#if defined(CLOCK_THREAD_CPUTIME_ID) && !defined(__EMSCRIPTEN__)
inline constexpr bool OwnerCpuAvailable = true;
inline std::uint64_t ownerCpuNow()
{
    timespec value{};
    return clock_gettime(CLOCK_THREAD_CPUTIME_ID,&value)==0
        ? std::uint64_t(value.tv_sec)*1000000000ULL+std::uint64_t(value.tv_nsec) : 0;
}
#else
inline constexpr bool OwnerCpuAvailable = false;
inline std::uint64_t ownerCpuNow() { return 0; }
#endif
enum class Phase : unsigned { Transitions, Displacement, Direction, Action, Visibility, Count };
struct Counters
{
    std::uint64_t calls = 0, opportunities = 0, searches = 0, visited = 0, failedSearches = 0;
    std::uint64_t searchNs = 0, routeExpansions = 0, destinationSearches = 0, directionChoices = 0;
    std::uint64_t phaseNs[unsigned(Phase::Count)]{}, phaseCpuNs[unsigned(Phase::Count)]{};
};
// Owner-thread diagnostics only. Workers publish their explicit batch metrics.
inline thread_local Counters* currentCounters = nullptr;
inline thread_local unsigned searchDepth = 0;
class DiagnosticBinding
{
    Counters* previous;
public:
    explicit DiagnosticBinding(Counters* value) : previous(currentCounters) { currentCounters=value; }
    ~DiagnosticBinding() { currentCounters=previous; }
};
class PhaseScope
{
    Counters* counters = currentCounters;
    Phase phase;
    std::uint64_t startedCpu = 0;
    std::chrono::steady_clock::time_point started;
public:
    explicit PhaseScope(Phase phase) : phase(phase) { if(counters) { started=std::chrono::steady_clock::now(); startedCpu=ownerCpuNow(); } }
    ~PhaseScope() {
        if(counters) {
            const auto cpu=ownerCpuNow();
            counters->phaseNs[unsigned(phase)]+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();
            if(startedCpu && cpu>=startedCpu)counters->phaseCpuNs[unsigned(phase)]+=cpu-startedCpu;
        }
    }
};
class SearchScope
{
    Counters* counters = currentCounters;
    std::chrono::steady_clock::time_point started;
public:
    SearchScope() { if(counters) { ++counters->searches; if(searchDepth++==0)started=std::chrono::steady_clock::now(); } }
    ~SearchScope() { if(counters && --searchDepth==0) counters->searchNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count(); }
};
inline void visitSearchCell() { if(currentCounters) ++currentCounters->visited; }
inline void routeExpansion(std::uint64_t cells) { if(currentCounters) { ++currentCounters->routeExpansions; currentCounters->visited+=cells; } }
inline void failedSearch() { if(currentCounters) ++currentCounters->failedSearches; }
}
