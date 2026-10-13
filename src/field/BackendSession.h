// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ComputeExecutor.h"
#include "GradientSelection.h"
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
enum class Family { Generic, Materials, Markets, Guard, Clear, Forbidden, Count };
enum class Operation { CompleteField, ResumableSearch };
enum class Plan : unsigned { CPU, Frozen8, VulkanFrozen8, VulkanJacobi, WebGPUFrozen8, WebGPUJacobi, Count };
struct ExecutionPlan {
    Plan id; Backend backend; unsigned steps, threads; bool colored, frozen;
    unsigned tileWidth, tileHeight;
};
inline constexpr std::array<ExecutionPlan, unsigned(Plan::Count)> PLANS{{
    {Plan::CPU,Backend::CPU,0,0,false,false,0,0},
    {Plan::Frozen8,Backend::OpenCL,8,128,true,true,16,16},
    {Plan::VulkanFrozen8,Backend::Vulkan,8,128,true,true,16,16},
    {Plan::VulkanJacobi,Backend::Vulkan,1,128,false,false,16,16},
    {Plan::WebGPUFrozen8,Backend::WebGPU,8,128,true,true,16,16},
    {Plan::WebGPUJacobi,Backend::WebGPU,1,128,false,false,16,16}}};
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
        } else if (!isGPU(plan.backend) || !plan.steps || !plan.threads ||
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
// command-line entry point requests a configuration; simulation ownership applies it.
inline constexpr Plan requestedOpenCLPlan() noexcept {return Plan::Frozen8;}
// Backend registration publishes only compiled, eligible plans. No read invokes
// initialization. State 1 means another worker is initializing; never wait on it.
inline std::atomic<unsigned> readyPlans{0}, initializationState{0};
inline void (*prepareAccelerator)() = nullptr;
inline bool (*prepareSelectedAccelerator)(Backend) = nullptr;
inline std::array<std::atomic<unsigned>,unsigned(Backend::Count)> selectedInitialization{};
inline std::atomic<std::uint64_t> backendPreparationNs{0};
inline std::uint64_t monotonicNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline Plan selectedPlan(Backend b,Algorithm a) {
    if(b==Backend::OpenCL)return Plan::Frozen8;
    if(b==Backend::Vulkan)return a==Algorithm::Jacobi?Plan::VulkanJacobi:Plan::VulkanFrozen8;
    if(b==Backend::WebGPU)return a==Algorithm::Jacobi?Plan::WebGPUJacobi:Plan::WebGPUFrozen8;
    return Plan::CPU;
}
struct PlanDecision { Plan plan=Plan::CPU; std::uint64_t generation=0; Configuration configuration{}; };
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
        invalidateConfiguration();wantsGPU.store(threads>=2 && isGPU(backend()));
    }
    PlanDecision choose(Family family,std::size_t,Configuration captured,Operation operation=Operation::CompleteField) const {
        validateFamily(family);
        const auto c=normalizedConfiguration(captured);
        const auto mode=c.backend;
        const auto plan=selectedPlan(mode,c.algorithm);
        PlanDecision result{Plan::CPU,currentGeneration(),c};
        if(operation==Operation::CompleteField && ComputeExecutor::workerSlot() &&
           isGPU(mode) && !failed.load() &&
           (readyPlans.load(std::memory_order_acquire)&(1u<<unsigned(plan))))result.plan=plan;
        return result;
    }
    // Direct callers may explicitly select a backend. Capture its compatible
    // algorithm once, rather than combining two separately loaded choices.
    PlanDecision choose(Family family,std::size_t count,Backend mode,Operation operation=Operation::CompleteField) const {
        auto captured=configuration();
        captured.backend=mode;
        return choose(family,count,captured,operation);
    }
    void fail() {if(!failed.exchange(true))invalidateConfiguration();}
    bool pending() const noexcept override {
        // Map-owned initialization is serviced externally. CPU jobs must not
        // poll global backend configuration from the executor's idle predicate.
        if (externalInitialization.load() || !wantsGPU.load()) return false;
        const auto selected=backend();
        return isGPU(selected) &&
            (selected==Backend::OpenCL ? prepareAccelerator && initializationState.load()==0 : prepareSelectedAccelerator && selectedInitialization[unsigned(selected)].load()==0);
    }
    void process() noexcept override {
        if(!ComputeExecutor::workerSlot() || externalInitialization.load() || !wantsGPU.load())return;
        const auto b=backend();if(!isGPU(b))return;auto& state=b==Backend::OpenCL?initializationState:selectedInitialization[unsigned(b)];
        unsigned empty=0;if(!state.compare_exchange_strong(empty,1))return;
        const auto start=monotonicNs();
        try {if(b==Backend::OpenCL && prepareAccelerator)prepareAccelerator();else if(prepareSelectedAccelerator)prepareSelectedAccelerator(b);}catch(...){}
        const auto elapsed=monotonicNs()-start;initializationNs.fetch_add(elapsed);backendPreparationNs.fetch_add(elapsed);
        state.store(2,std::memory_order_release);
    }
    Metrics metrics() const {return {initializationNs.load(),sizeof(*this)};}
};
} // namespace gradient_kernel
