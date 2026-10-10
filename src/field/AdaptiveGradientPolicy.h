// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ComputeExecutor.h"
#include "ThreadCpuClock.h"
#include "CpuSavingPolicy.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace gradient_kernel
{
enum class Backend { Automatic, CPU, OpenCL };
enum class Family { Generic, Materials, Markets, Guard, Clear, Forbidden, Count };
enum class Operation { CompleteField, ResumableSearch };
enum class Plan : unsigned { CPU, Jacobi4, Colored2, Colored4, Colored8, Frozen8, Frozen16, Count };
struct ExecutionPlan {
    Plan id; Backend backend; unsigned steps, threads; bool colored, frozen;
    unsigned tileWidth, tileHeight;
};
inline constexpr std::array<ExecutionPlan, unsigned(Plan::Count)> PLANS{{
    {Plan::CPU,Backend::CPU,0,0,false,false,0,0},
    {Plan::Jacobi4,Backend::OpenCL,4,256,false,false,16,16},
    {Plan::Colored2,Backend::OpenCL,2,128,true,false,16,16},
    {Plan::Colored4,Backend::OpenCL,4,128,true,false,16,16},
    {Plan::Colored8,Backend::OpenCL,8,256,true,false,16,16},
    {Plan::Frozen8,Backend::OpenCL,8,128,true,true,16,16},
    {Plan::Frozen16,Backend::OpenCL,16,64,true,true,16,16}}};
// Plan ids are persisted in atomic decisions and readiness masks. The backend
// derives its compilation and lane descriptors from this same ordered table.
static_assert(PLANS.size() < 32);
static_assert([] {
    for (std::size_t i = 0; i < PLANS.size(); ++i) {
        const auto& plan = PLANS[i];
        if (unsigned(plan.id) != i) return false;
        if (i == 0) {
            if (plan.backend != Backend::CPU || plan.steps || plan.threads ||
                plan.tileWidth || plan.tileHeight || plan.colored || plan.frozen) return false;
        } else if (plan.backend != Backend::OpenCL || !plan.steps || !plan.threads ||
                   !plan.tileWidth || !plan.tileHeight || (plan.frozen && !plan.colored)) return false;
    }
    return true;
}());
inline constexpr std::size_t BATCH_CATEGORIES = 8, CATEGORIES = std::size_t(Family::Count)*BATCH_CATEGORIES;
inline std::size_t batchCategory(std::size_t count) { return std::clamp<std::size_t>(count,1,8)-1; }
inline bool validFamily(Family family) { return unsigned(family) < unsigned(Family::Count); }
inline void validateFamily(Family family) {
    if (!validFamily(family)) throw std::invalid_argument("Invalid gradient family");
}
inline std::size_t category(Family family, std::size_t count) {
    validateFamily(family);
    return std::size_t(family)*BATCH_CATEGORIES+batchCategory(count);
}
inline std::atomic<Backend>& backendSetting() {
    static std::atomic<Backend> value{[] {
        const auto* name=std::getenv("GLOB2_GRADIENT_BACKEND");
        return name && std::strcmp(name,"cpu")==0 ? Backend::CPU :
            name && std::strcmp(name,"opencl")==0 ? Backend::OpenCL : Backend::Automatic;
    }()}; return value;
}
inline Backend backend() { return backendSetting().load(std::memory_order_relaxed); }
inline void setBackend(Backend value) { backendSetting().store(value,std::memory_order_relaxed); }
// Experimental overrides are explicit and reproducible. An invalid value is
// rejected before device selection rather than silently selecting another plan.
inline Plan requestedOpenCLPlan() {
    static const Plan selected=[] {
        const auto* name=std::getenv("GLOB2_GRADIENT_PLAN");
        if(!name || !*name || std::strcmp(name,"frozen8")==0) return Plan::Frozen8;
        constexpr std::array<const char*,unsigned(Plan::Count)> names{
            "cpu","jacobi4","colored2","colored4","colored8","frozen8","frozen16"};
        for(unsigned i=0;i<names.size();++i) if(std::strcmp(name,names[i])==0) return Plan(i);
        throw std::invalid_argument("Invalid GLOB2_GRADIENT_PLAN");
    }();
    return selected;
}
inline bool accountingRequested() {
    const auto* value=std::getenv("GLOB2_GRADIENT_ACCOUNTING"); return value && std::strcmp(value,"1")==0;
}
inline bool learningRequested() {
    // Experimental opt-in until the complete tuning-enabled configuration passes
    // process-CPU, presentation and compatibility qualification.
    const auto* value=std::getenv("GLOB2_GRADIENT_TUNING");
    return value && std::strcmp(value,"1")==0;
}
// Backend registration publishes only compiled, eligible plans. No read invokes
// initialization. State 1 means another worker is initializing; never wait on it.
inline std::atomic<unsigned> readyPlans{0}, initializationState{0};
inline void (*prepareAccelerator)() = nullptr;
inline std::atomic<std::uint64_t> backendPreparationNs{0};
inline std::uint64_t monotonicNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct PlanDecision { Plan plan=Plan::CPU; std::uint64_t version=0, generation=0; };
struct StageTiming { std::uint64_t preparationNs=0, uploadNs=0, dispatchNs=0, readbackNs=0; };
inline thread_local StageTiming* activeStageTiming=nullptr;
struct StageTimingScope {
    StageTiming* previous;
    explicit StageTimingScope(StageTiming* value):previous(std::exchange(activeStageTiming,value)) {}
    ~StageTimingScope() { activeStageTiming=previous; }
};
// A pipeline supplies already-known scheduling boundaries. Synchronous calls
// have no queue-delay observation. Resumable searches never enter this scope.
struct JobTiming {
    std::uint64_t submitted=0, started=0, preparation=0;
    inline static thread_local JobTiming* current=nullptr;
};
class JobTimingScope {
    JobTiming* previous;
public:
    explicit JobTimingScope(JobTiming& timing):previous(std::exchange(JobTiming::current,&timing)) {}
    ~JobTimingScope() { JobTiming::current=previous; }
};
struct GradientObservation {
    PlanDecision decision;
    Family family=Family::Generic;
    unsigned batch=1, width=0, height=0, limit=0, threads=1, cpuBuckets=0;
    std::uint64_t movement=0, costRevision=0, queueNs=0, executionNs=0, serviceNs=0, seedPreparationNs=0;
    bool hasQueue=false;
    std::uint64_t hostCpuNs=0, seedPreparationCpuNs=0;
    StageTiming stages;
};
class AdaptiveGradientPolicy : public ComputeExecutor::WorkerOnly
{
public:
    static constexpr unsigned WorkerSlots=32, BufferSize=32, SamplePeriod=32, PassLimit=8;
    struct Metrics {
        std::uint64_t recorded=0,dropped=0,accepted=0,stale=0,processingNs=0,passes=0,initializationNs=0;
        std::uint64_t queueNs=0,executionNs=0,serviceNs=0,seedPreparationNs=0;
        StageTiming stages;
        std::size_t retainedBytes=0;
    };
private:
    std::array<std::atomic<std::uint64_t>, CATEGORIES> decisions{};
    inline static std::atomic<std::uint64_t> nextGeneration{1};
    std::atomic<std::uint64_t> generation{nextGeneration.fetch_add(1)};
    std::atomic<unsigned> configurationThreads{1};
    std::atomic<bool> wantsGPU{false};
    std::atomic<bool> externalInitialization{false};
    std::shared_ptr<CpuSavingPolicy> learning;
    struct alignas(64) Buffer {
        std::array<GradientObservation,BufferSize> entries;
        std::atomic<unsigned> written{0}, read{0};
        unsigned samples=0; // one producer: this executor worker slot
    };
    struct Profile {
        // Single maintenance consumer, bounded recent EWMA; never selects a plan.
        GradientObservation context{};
        std::uint64_t count=0, recentExecutionNs=0;
    };
    struct Accounting {
        std::array<Buffer,WorkerSlots> buffers;
        std::array<Profile,CATEGORIES*unsigned(Plan::Count)> profiles;
        std::atomic<std::uint64_t> recorded{0},dropped{0},accepted{0},stale{0},processingNs{0},passes{0};
        std::atomic<std::uint64_t> queueNs{0},executionNs{0},serviceNs{0},seedPreparationNs{0};
        std::atomic<std::uint64_t> preparationNs{0},uploadNs{0},dispatchNs{0},readbackNs{0};
        std::atomic<bool> pending{false};
        unsigned cursor=1;
    };
    std::unique_ptr<Accounting> accounting;
    std::atomic<std::uint64_t> initializationNs{0};
public:
    std::atomic<bool> failed{false};
    // A device service owns compilation and initialization on its coordinator.
    // Legacy synchronous harnesses retain their worker-maintenance path.
    void setExternalInitialization(bool value) { externalInitialization.store(value); }
    std::uint64_t currentGeneration() const { return generation.load(std::memory_order_acquire); }
    // Configured with required work drained. Optional learning has no owner-loop
    // processing pass and never adds a dependency to a required completion.
    void configureLearning(bool enable) {
        if(enable && !learning) learning=std::make_shared<CpuSavingPolicy>();
        if(!enable) learning.reset();
    }
    std::shared_ptr<CpuSavingPolicy> learningPolicy() const { return learning; }
    PlanDecision chooseWorkload(const WorkloadKey& key, Backend mode,
                                Operation operation=Operation::CompleteField) const {
        validateFamily(key.family);
        PlanDecision result{Plan::CPU,0,currentGeneration()};
        if(operation!=Operation::CompleteField || !ComputeExecutor::workerSlot() ||
           mode==Backend::CPU || failed.load()) return result;
        if(mode==Backend::OpenCL) result.plan=requestedOpenCLPlan();
        else if(learning) {
            const auto accepted=learning->lookup(key);
            result.plan=accepted.plan; result.version=accepted.version;
        }
        if(result.plan!=Plan::CPU && !(readyPlans.load(std::memory_order_acquire)&(1u<<unsigned(result.plan)))) result.plan=Plan::CPU;
        return result;
    }
    // Configure only after stopping the executor (including maintenance passes).
    // Replacing the map creates a new policy; reconfiguration invalidates all
    // previous observations while preserving established plans.
    void configure(unsigned threads, bool enable) {
        configurationThreads.store(threads);
        generation.store(nextGeneration.fetch_add(1),std::memory_order_release);
        if(learning) learning->invalidate();
        if(enable && !accounting) accounting=std::make_unique<Accounting>();
        if(!enable) accounting.reset();
        const auto mode=backend();
        wantsGPU.store(mode==Backend::OpenCL || (mode==Backend::Automatic &&
            std::any_of(decisions.begin(),decisions.end(),[](const auto& word) {
                return (word.load(std::memory_order_relaxed)&255)!=unsigned(Plan::CPU);
            })));
    }
    void establish(Family family,std::size_t count,Plan plan) {
        if(unsigned(plan)>=PLANS.size()) throw std::invalid_argument("Invalid gradient plan");
        auto& word=decisions[category(family,count)]; auto old=word.load();
        while(!word.compare_exchange_weak(old,((old>>8)+1)*256+unsigned(plan))) {}
        if(plan!=Plan::CPU) wantsGPU.store(true);
    }
    PlanDecision decision(Family family,std::size_t count) const {
        const auto word=decisions[category(family,count)].load(std::memory_order_acquire);
        return {Plan(word&255),word>>8,generation.load(std::memory_order_acquire)};
    }
    PlanDecision choose(Family family,std::size_t count,Backend mode,Operation operation=Operation::CompleteField) const {
        validateFamily(family);
        // Semantic and owner eligibility precede any performance-policy lookup.
        if(operation!=Operation::CompleteField || !ComputeExecutor::workerSlot()) return {};
        auto result=decision(family,count);
        if(mode==Backend::CPU || failed.load()) result.plan=Plan::CPU;
        else if(mode==Backend::OpenCL) result.plan=requestedOpenCLPlan();
        if(result.plan!=Plan::CPU && !(readyPlans.load(std::memory_order_acquire)&(1u<<unsigned(result.plan)))) result.plan=Plan::CPU;
        return result;
    }
    void fail() {
        if(!failed.exchange(true)) generation.store(nextGeneration.fetch_add(1),std::memory_order_release);
    }
    bool accountingEnabled() const { return bool(accounting); }
    bool sample() {
        const auto slot=ComputeExecutor::workerSlot();
        return accounting && slot && slot<WorkerSlots && accounting->buffers[slot].samples++%SamplePeriod==0;
    }
    void record(const GradientObservation& observation) noexcept {
        const auto slot=ComputeExecutor::workerSlot();
        if(!accounting || !slot || slot>=WorkerSlots) return;
        // Telemetry is nonthrowing and optional. Malformed external observations
        // are dropped before they can reach profile indexing or readiness shifts.
        if (!validFamily(observation.family) || unsigned(observation.decision.plan) >= PLANS.size()) {
            ++accounting->dropped; return;
        }
        auto& b=accounting->buffers[slot]; const auto written=b.written.load(std::memory_order_relaxed);
        if(written-b.read.load(std::memory_order_acquire)==BufferSize) { ++accounting->dropped; return; }
        b.entries[written%BufferSize]=observation;
        b.written.store(written+1,std::memory_order_release);
        accounting->pending.store(true,std::memory_order_release);
        ++accounting->recorded;
    }
    bool pending() const noexcept override {
        return (!externalInitialization.load() && wantsGPU.load() && prepareAccelerator && initializationState.load()==0) ||
            (accounting && accounting->pending.load(std::memory_order_relaxed));
    }
    void process() noexcept override {
        if(!ComputeExecutor::workerSlot()) return; // explicit owner exclusion, also for direct callers
        if(!externalInitialization.load() && wantsGPU.load() && prepareAccelerator) {
            unsigned empty=0;
            if(initializationState.compare_exchange_strong(empty,1)) {
                const auto start=monotonicNs();
                try { prepareAccelerator(); } catch(...) { readyPlans.store(0); }
                const auto elapsed=monotonicNs()-start;
                initializationNs.fetch_add(elapsed); backendPreparationNs.fetch_add(elapsed);
                initializationState.store(2,std::memory_order_release);
                return; // driver initialization is accounted separately from passive processing
            }
        }
        if(!accounting) return;
        // Clear only at entry. A producer publishing during this pass keeps
        // its notification; an exhausted pass rearms itself. No busy-wait on
        // a producer preempted between publishing its ring and notification.
        accounting->pending.exchange(false,std::memory_order_acquire);
        const auto start=monotonicNs(); unsigned consumed=0;
        for(unsigned visited=0;visited<WorkerSlots-1 && consumed<PassLimit;++visited) {
            auto& b=accounting->buffers[accounting->cursor];
            accounting->cursor=accounting->cursor%(WorkerSlots-1)+1;
            auto read=b.read.load(std::memory_order_relaxed);
            const auto written=b.written.load(std::memory_order_acquire);
            while(read!=written && consumed<PassLimit) {
                const auto observation=b.entries[read%BufferSize]; ++read; ++consumed;
                const auto current=decision(observation.family,observation.batch);
                if(current.generation!=observation.decision.generation || current.version!=observation.decision.version ||
                   observation.threads!=configurationThreads.load() || (observation.decision.plan!=Plan::CPU && (failed.load() || !(readyPlans.load()&(1u<<unsigned(observation.decision.plan)))))) ++accounting->stale;
                else {
                    auto& p=accounting->profiles[category(observation.family,observation.batch)*unsigned(Plan::Count)+unsigned(observation.decision.plan)];
                    const auto& old=p.context;
                    if(old.decision.generation!=observation.decision.generation || old.decision.version!=observation.decision.version ||
                       old.width!=observation.width || old.height!=observation.height || old.limit!=observation.limit || old.movement!=observation.movement || old.costRevision!=observation.costRevision || old.cpuBuckets!=observation.cpuBuckets || old.threads!=observation.threads) p.count=0;
                    p.recentExecutionNs=p.count ? p.recentExecutionNs-p.recentExecutionNs/8+observation.executionNs/8 : observation.executionNs;
                    p.context=observation; ++p.count; ++accounting->accepted;
                    accounting->queueNs+=observation.queueNs; accounting->executionNs+=observation.executionNs;
                    accounting->serviceNs+=observation.serviceNs; accounting->seedPreparationNs+=observation.seedPreparationNs;
                    accounting->preparationNs+=observation.stages.preparationNs; accounting->uploadNs+=observation.stages.uploadNs;
                    accounting->dispatchNs+=observation.stages.dispatchNs; accounting->readbackNs+=observation.stages.readbackNs;
                }
            }
            b.read.store(read,std::memory_order_release);
        }
        if(consumed==PassLimit) accounting->pending.store(true,std::memory_order_release);
        ++accounting->passes; accounting->processingNs+=monotonicNs()-start;
    }
    Metrics metrics() const {
        Metrics m; m.initializationNs=initializationNs.load(); m.retainedBytes=sizeof(*this);
        if(!accounting) return m;
        m.retainedBytes+=sizeof(Accounting);
#define READ(name) m.name=accounting->name.load(std::memory_order_relaxed)
        READ(recorded);READ(dropped);READ(accepted);READ(stale);READ(processingNs);READ(passes);
        READ(queueNs);READ(executionNs);READ(serviceNs);READ(seedPreparationNs);
#undef READ
        m.stages={accounting->preparationNs.load(),accounting->uploadNs.load(),accounting->dispatchNs.load(),accounting->readbackNs.load()};
        return m;
    }
};
using BackendSession=AdaptiveGradientPolicy;
} // namespace gradient_kernel
