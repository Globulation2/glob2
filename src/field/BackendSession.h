// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ComputeExecutor.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace gradient_kernel
{
bool reserveOpenCLHostBytes(std::size_t) noexcept;
void releaseOpenCLHostBytes(std::size_t) noexcept;
enum class Backend { CPU, OpenCL };
enum class Family { Generic, Materials, Markets, Guard, Clear, Forbidden, Count };
enum class Operation { CompleteField, ResumableSearch };
enum class Plan : unsigned { CPU, Frozen8, Count };
struct ExecutionPlan {
    Plan id; Backend backend; unsigned steps, threads; bool colored, frozen;
    unsigned tileWidth, tileHeight;
};
inline constexpr std::array<ExecutionPlan, unsigned(Plan::Count)> PLANS{{
    {Plan::CPU,Backend::CPU,0,0,false,false,0,0},
    {Plan::Frozen8,Backend::OpenCL,8,128,true,true,16,16}}};
// Plan ids identify CPU or the fixed GPU implementation. The backend
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
inline constexpr std::size_t BATCH_CATEGORIES=8;
inline bool validFamily(Family family) { return unsigned(family) < unsigned(Family::Count); }
inline void validateFamily(Family family) {
    if (!validFamily(family)) throw std::invalid_argument("Invalid gradient family");
}
// Execution is local configuration, never saved simulation state. The GUI or
// command-line entry point explicitly sets this binary choice before work starts.
inline std::atomic<Backend>& backendSetting() {
    static std::atomic<Backend> value{[] {
        const auto* name=std::getenv("GLOB2_GRADIENT_BACKEND");
        return name && std::strcmp(name,"opencl")==0 ? Backend::OpenCL : Backend::CPU;
    }()};return value;
}
inline Backend backend() {return backendSetting().load(std::memory_order_relaxed);}
inline void setBackend(Backend value) {backendSetting().store(value,std::memory_order_relaxed);}
inline constexpr Plan requestedOpenCLPlan() noexcept {return Plan::Frozen8;}
// Backend registration publishes only compiled, eligible plans. No read invokes
// initialization. State 1 means another worker is initializing; never wait on it.
inline std::atomic<unsigned> readyPlans{0}, initializationState{0};
inline void (*prepareAccelerator)() = nullptr;
inline std::atomic<std::uint64_t> backendPreparationNs{0};
inline std::uint64_t monotonicNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct PlanDecision { Plan plan=Plan::CPU; std::uint64_t generation=0; };
// A session carries only lifetime/generation and failure state. GPU selection is
// explicit; there are no learned profiles, comparisons or tuning dependencies.
class BackendSession : public ComputeExecutor::WorkerOnly
{
    inline static std::atomic<std::uint64_t> nextGeneration{1};
    std::atomic<std::uint64_t> generation{nextGeneration.fetch_add(1)};
    std::atomic<bool> externalInitialization{false},wantsGPU{false};
    std::atomic<std::uint64_t> initializationNs{0};
public:
    struct Metrics {std::uint64_t initializationNs=0;std::size_t retainedBytes=0;};
    std::atomic<bool> failed{false};
    void setExternalInitialization(bool value) {externalInitialization.store(value);}
    std::uint64_t currentGeneration() const {return generation.load(std::memory_order_acquire);}
    void invalidateConfiguration() noexcept {generation.store(nextGeneration.fetch_add(1),std::memory_order_release);}
    void configure(unsigned threads,bool = false) {
        invalidateConfiguration();wantsGPU.store(threads>=2 && backend()==Backend::OpenCL);
    }
    PlanDecision choose(Family family,std::size_t,Backend mode,Operation operation=Operation::CompleteField) const {
        validateFamily(family);
        PlanDecision result{Plan::CPU,currentGeneration()};
        if(operation==Operation::CompleteField && ComputeExecutor::workerSlot() &&
           mode==Backend::OpenCL && !failed.load() &&
           (readyPlans.load(std::memory_order_acquire)&(1u<<unsigned(Plan::Frozen8))))result.plan=Plan::Frozen8;
        return result;
    }
    void fail() {if(!failed.exchange(true))invalidateConfiguration();}
    bool pending() const noexcept override {
        return !externalInitialization.load() && wantsGPU.load() && prepareAccelerator && initializationState.load()==0;
    }
    void process() noexcept override {
        if(!ComputeExecutor::workerSlot() || externalInitialization.load() || !wantsGPU.load() || !prepareAccelerator)return;
        unsigned empty=0;if(!initializationState.compare_exchange_strong(empty,1))return;
        const auto start=monotonicNs();
        try {prepareAccelerator();}catch(...){readyPlans.store(0);}
        const auto elapsed=monotonicNs()-start;initializationNs.fetch_add(elapsed);backendPreparationNs.fetch_add(elapsed);
        initializationState.store(2,std::memory_order_release);
    }
    Metrics metrics() const {return {initializationNs.load(),sizeof(*this)};}
};
} // namespace gradient_kernel
