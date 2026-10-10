// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "common/ThreadCpuClock.h"
#include <array>
#include <cstdint>

namespace AIEngine {
// Unsaved, independently cumulative diagnostic components nested within
// DecisionAndCommandCapture. Never add them to inclusive decision/process CPU.
enum class ResourceGradientMetric : unsigned {
    Requests,PublishedHits,PrivateHits,ColdAttempts,ColdCompleted,ColdAborted,
    AttemptedCells,AllocationAndSeedCpuNs,PropagationCpuNs,InvalidClocks,
    CompletedJobs,FailedJobs,OwnerJobsExcluded,Count
};
constexpr unsigned ResourceGradientMetricCount=unsigned(ResourceGradientMetric::Count);
using ResourceGradientValues=std::array<std::uint64_t,ResourceGradientMetricCount>;
struct ResourceGradientCounters {
    bool enabled=false;
    ResourceGradientValues values{};
    std::uint64_t get(ResourceGradientMetric metric)const noexcept{return values[unsigned(metric)];}
};
inline const char* resourceGradientMetricName(ResourceGradientMetric metric) noexcept {
    constexpr std::array names{"requests","published_hits","private_hits","cold_attempts",
        "cold_completed","cold_aborted","attempted_cells","allocation_and_seed_cpu_ns",
        "propagation_cpu_ns","invalid_clocks","completed_jobs","failed_jobs","owner_jobs_excluded"};
    return unsigned(metric)<names.size()?names[unsigned(metric)]:"unknown";
}
struct ResourceGradientDiagnosticSink {
    using Clock=std::uint64_t(*)(); // Injected failures are caught, never AI failures.
    ResourceGradientValues values{};
    Clock clock=glob2::threadCpuNs;
    void add(ResourceGradientMetric m,std::uint64_t amount=1) noexcept {values[unsigned(m)]+=amount;}
    std::uint64_t readClock()const noexcept {
        try{return clock?clock():0;}catch(...){return 0;}
    }
};
static_assert(noexcept(glob2::threadCpuNs()));
inline thread_local ResourceGradientDiagnosticSink* activeResourceGradientDiagnostics=nullptr;
// Installed only on a decision worker and destroyed before merging its local
// counters. WorldQueries is stack-bound to that synchronous decision; controller
// query pointers are cleared on success and exceptions before getOrder returns.
class ResourceGradientDiagnosticScope {
    ResourceGradientDiagnosticSink* previous;
public:
    explicit ResourceGradientDiagnosticScope(ResourceGradientDiagnosticSink& sink) noexcept
        :previous(activeResourceGradientDiagnostics){activeResourceGradientDiagnostics=&sink;}
    ~ResourceGradientDiagnosticScope(){activeResourceGradientDiagnostics=previous;}
    ResourceGradientDiagnosticScope(const ResourceGradientDiagnosticScope&)=delete;
    ResourceGradientDiagnosticScope& operator=(const ResourceGradientDiagnosticScope&)=delete;
};
class ResourceGradientColdMeasurement {
    ResourceGradientDiagnosticSink& sink;
    std::uint64_t start=0,seedEnd=0;
    bool finished=false,clockValid=true;
public:
    ResourceGradientColdMeasurement(ResourceGradientDiagnosticSink& s,std::uint64_t cells) noexcept:sink(s) {
        sink.add(ResourceGradientMetric::ColdAttempts);sink.add(ResourceGradientMetric::AttemptedCells,cells);
        start=sink.readClock();clockValid=start!=0;
    }
    ~ResourceGradientColdMeasurement(){if(!finished){sink.add(ResourceGradientMetric::ColdAborted);
        if(!clockValid)sink.add(ResourceGradientMetric::InvalidClocks);}}
    void seeded()noexcept{seedEnd=sink.readClock();clockValid=clockValid&&seedEnd&&seedEnd>=start;}
    void complete()noexcept {
        const auto end=sink.readClock();finished=true;sink.add(ResourceGradientMetric::ColdCompleted);
        if(!clockValid||!end||end<seedEnd) {
            sink.add(ResourceGradientMetric::InvalidClocks);return;
        }
        sink.add(ResourceGradientMetric::AllocationAndSeedCpuNs,seedEnd-start);
        sink.add(ResourceGradientMetric::PropagationCpuNs,end-seedEnd);
    }
    ResourceGradientColdMeasurement(const ResourceGradientColdMeasurement&)=delete;
    ResourceGradientColdMeasurement& operator=(const ResourceGradientColdMeasurement&)=delete;
};
} // namespace AIEngine
