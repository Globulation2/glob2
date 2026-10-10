// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientWorkspace.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace gradient_kernel
{
class GradientDeviceService;
struct GradientDeviceState;
enum class GradientFallbackReason : unsigned {None,Unavailable,InvalidRequest,StaleGeneration,Duplicate,MemoryBudget,BackendDecline,DriverFailure,Shutdown,Count};
// The device service borrows neither a Map, a pipeline job nor a worker's
// scratch. Seeds move into this holder and stay unchanged on GPU decline.
struct OwnedGradientField
{
    std::unique_ptr<std::uint16_t[]> data;
    std::shared_ptr<const void> inputs;
    std::shared_ptr<BackendSession> session;
    field::Grid grid{1,1};
    CostIdentity identity;
    Family family = Family::Generic;
    PlanDecision decision;
    int limit = COST_LIMIT;
    unsigned cpuBuckets = 64;
    std::uint64_t due = 0;
    EntrySteps (*costAt)(const OwnedGradientField&,std::size_t) = nullptr;
    void (*cpu)(OwnedGradientField&) = nullptr;
    ComputeExecutor::CompletionTicket completion;
    std::exception_ptr error;
    std::uint64_t serviceNs = 0, hostCpuNs = 0, fallbackCpuNs = 0;
    bool executedGPU = false;
    GradientFallbackReason fallbackReason=GradientFallbackReason::None;
    std::size_t reservedHostBytes = 0, retainedInputBytes = 0;
    std::atomic<bool> admitted{false};
    std::shared_ptr<std::atomic<std::size_t>> serviceRetained;
    std::weak_ptr<GradientDeviceService> observer;
    std::shared_ptr<GradientDeviceState> executionState;
    WorkloadKey workload;
    std::uint64_t tick=0, seedCpuNs=0;
    ~OwnedGradientField();
    std::unique_ptr<std::uint16_t[]> takeData();
private:
    void releaseReservation() noexcept;
};

// Registrations share one process-owned, mostly sleeping driver thread. Map
// lifecycle cancels an owned registration without joining optional work.
class GradientDeviceService : public std::enable_shared_from_this<GradientDeviceService>
{
public:
    struct Hooks {
        // Test adapters must own captured state: cancellation can return before
        // an initializer exits. Each registration copies these callbacks.
        std::function<bool()> initialize;
        std::function<bool(std::span<const BackendRequest>,Plan)> execute;
    };
    struct Metrics {
        std::uint64_t submitted=0, completed=0, executed=0, trivial=0, fallbacks=0, declined=0;
        std::uint64_t batches=0, maxBatch=0, initializationNs=0, hostCpuNs=0;
        std::uint64_t stale=0, budgetDeclines=0, observationDrops=0;
        std::size_t queued=0, retainedHostBytes=0;
        bool running=false, ready=false;
        unsigned configuredMaxBatch=8, deviceConcurrency=1;
        unsigned coordinatorThreads=0;
        std::array<std::uint64_t,unsigned(GradientFallbackReason::Count)> fallbackReasons{};
    };
private:
    mutable std::mutex mutex;
    Hooks hooks;
    std::shared_ptr<GradientDeviceState> registration;
    std::shared_ptr<GradientDeviceState> state() const;
    void fallback(const std::shared_ptr<OwnedGradientField>&) noexcept;
public:
    explicit GradientDeviceService(Hooks hooks = {}):hooks(std::move(hooks)) {}
    ~GradientDeviceService() { stop(); }
    GradientDeviceService(const GradientDeviceService&) = delete;
    GradientDeviceService& operator=(const GradientDeviceService&) = delete;
    // Lifecycle only: all required pipeline batches must be drained first.
    void configure(unsigned computeThreads, Backend mode);
    void stop() noexcept;
    bool submit(const std::shared_ptr<OwnedGradientField>&) noexcept;
    void recoverOnWorker(const std::shared_ptr<OwnedGradientField>& field) noexcept { fallback(field); }
    Metrics metrics() const;
    // Required workers submit bounded metadata only; learning runs when the
    // coordinator has no required device work. Overflow drops optional learning.
    void recordAccepted(std::shared_ptr<BackendSession>,const WorkloadKey&,Plan,
                        std::uint64_t hostCpuNs,std::uint64_t tick,bool failed=false) noexcept;
};
}
