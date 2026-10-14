// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientWorkspace.h"
#include "OpenCLGradient.h"
#include <functional>
#include <mutex>
#include <thread>

namespace gradient_kernel
{
class GradientDeviceService;
struct GradientDeviceState;
// Application/test owner barrier after games and their executor work drain.
// Join driver initialization before driver exit handlers can tear it down.
// Later registrations may start a fresh coordinator; setup and shutdown must
// remain serialized on the owner thread.
void shutdownGradientDeviceCoordinator() noexcept;
enum class GradientFallbackReason : unsigned {
    None,Unavailable,InvalidRequest,StaleGeneration,Duplicate,MemoryBudget,
    BackendDecline,DriverFailure,Shutdown,Cancellation,Count
};
// Only owned immutable inputs and private seeds cross the worker/coordinator boundary.
struct OwnedGradientField
{
    std::shared_ptr<OpenCLRequiredControl> requiredControl=makeOpenCLRequiredControl();
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
    bool executedGPU = false;
    // Diagnostic completion proof survives later rollback. Publication
    // still uses executedGPU, which means the GPU result actually committed.
    bool deviceExecutionObserved=false;
    GradientFallbackReason fallbackReason=GradientFallbackReason::None;
    std::size_t reservedHostBytes = 0, retainedInputBytes = 0;
    enum class Phase { Private, Queued, Running, Recovering, Complete };
    std::atomic<Phase> phase{Phase::Private};
    std::shared_ptr<std::atomic<std::size_t>> serviceRetained;
    std::shared_ptr<GradientDeviceState> executionState;
    ~OwnedGradientField();
    std::unique_ptr<std::uint16_t[]> takeData();
private:
    void releaseReservation() noexcept;
};

// One process-owned coordinator runs required work only. A Map registration can
// cancel without joining the coordinator; original tickets still resolve once.
class GradientDeviceService : public std::enable_shared_from_this<GradientDeviceService>
{
public:
    struct Hooks {
        std::function<bool()> initialize;
        std::function<bool(std::span<const BackendRequest>,Plan)> execute;
    };
    struct Metrics {
        std::uint64_t submitted=0, completed=0, executed=0, trivial=0, fallbacks=0, declined=0;
        std::uint64_t batches=0, maxBatch=0;
        // Fields with at least one confirmed completed GPU dispatch. This is
        // distinct from a fully converged, successfully committed GPU result.
        std::uint64_t deviceCompletedFields=0,fallbackAfterDeviceCompletionFields=0;
        std::uint64_t stale=0,budgetDeclines=0;
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
    ~GradientDeviceService() {stop();}
    GradientDeviceService(const GradientDeviceService&) = delete;
    GradientDeviceService& operator=(const GradientDeviceService&) = delete;
    // Reconfiguration follows a required-work drain; cancellation never joins.
    void configure(unsigned computeThreads,Backend mode);
    void stop() noexcept;
    bool submit(const std::shared_ptr<OwnedGradientField>&) noexcept;
    void recoverOnWorker(const std::shared_ptr<OwnedGradientField>& field) noexcept {fallback(field);}
    Metrics metrics() const;
};
}
