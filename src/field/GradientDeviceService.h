// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientWorkspace.h"
#include "GradientDeadlineBatch.h"
#include "GradientSeedShape.h"
#include "OpenCLGradient.h"
#include "GradientCpuCapture.h"
#include "GradientCpuCaptureGroup.h"
#include "CaptureProbeReplay.h"
#include "CaptureBatchProbeReplay.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace gradient_kernel
{
class GradientDeviceService;
struct GradientDeviceState;
bool gradientDiagnosticsRequested() noexcept;
enum class GradientFallbackReason : unsigned {None,Unavailable,InvalidRequest,StaleGeneration,Duplicate,MemoryBudget,BackendDecline,DriverFailure,Shutdown,CpuClockUnavailable,Cancellation,BatchNotReady,Count};
// The device service borrows neither a Map, a pipeline job nor a worker's
// scratch. Seeds move into this holder and stay unchanged on GPU decline.
struct GradientBatchChoice
{
    PlanDecision decision;
    unsigned minimumCount=1;
    std::uint64_t maximumElapsedNs=0;
    explicit operator bool() const noexcept {return minimumCount>1 && decision.plan!=Plan::CPU && maximumElapsedNs;}
};
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
    // An independently accepted batch can win while singleton stays CPU.
    // Such an offer must fall back immediately unless enough fields are ready.
    unsigned minimumBatchCount=1;
    std::uint64_t offeredBatchElapsedNs=0;
    int limit = COST_LIMIT;
    unsigned cpuBuckets = 64;
    std::uint64_t due = 0;
    std::uint64_t publicationTick=0,cadenceRevision=0,cadenceFloorNs=0;
    GradientSeedShape seedShape;
    EntrySteps (*costAt)(const OwnedGradientField&,std::size_t) = nullptr;
    void (*cpu)(OwnedGradientField&) = nullptr;
    ComputeExecutor::CompletionTicket completion;
    std::exception_ptr error;
    std::uint64_t serviceNs = 0, hostCpuNs = 0, fallbackCpuNs = 0;
    // Published before the completion ticket resolves; owner reads after join.
    std::uint64_t deviceStartedWallNs=0,deviceCompletedWallNs=0,fallbackCleanupCpuNs=0;
    bool diagnostics=false;
    // No preparation CPU reference is assumed unless its producer measured it.
    bool preparationCpuClockValid=false,hostCpuClockValid=false;
    unsigned executedBatchCount=1;
    bool executedGPU = false;
    // Diagnostic completion proof survives later rollback. Policy/publication
    // still use executedGPU, which means the GPU result actually committed.
    bool deviceExecutionObserved=false;
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
    class GroupCapture {
        friend class GradientDeviceService;
        std::unique_ptr<GradientCpuCaptureGroup::Member> member;
        std::shared_ptr<GradientCpuCaptureGroup::Record> record;
        std::weak_ptr<GradientDeviceState> registration;
        GradientCpuCapture::Receipt retired;
    public:
        ~GroupCapture();
        bool complete(std::span<const std::uint16_t>,const GradientCpuCapture::RequiredReceipt&) noexcept;
        GradientCpuCapture::Receipt callingReceipt() const noexcept;
        GradientCpuCapture::Receipt retire() noexcept;
    };
    struct Hooks {
        // Test adapters must own captured state: cancellation can return before
        // an initializer exits. Each registration copies these callbacks.
        std::function<bool()> initialize;
        std::function<bool(std::span<const BackendRequest>,Plan)> execute;
        std::function<OpenCLStatus()> status; // Owned fake configuration, tests only.
        std::function<std::uint64_t()> cpuClock; // Empty native default; owned test clock.
        CaptureProbeReplay::Hooks probe; // Empty production default; fully owned test backend.
        CaptureBatchProbeReplay::Hooks batchProbe; // Same owned, diagnostic-only test contract.
    };
    struct Metrics {
        std::uint64_t submitted=0, completed=0, executed=0, trivial=0, fallbacks=0, declined=0;
        std::uint64_t batches=0, maxBatch=0, initializationNs=0, hostCpuNs=0;
        std::uint64_t tuningStartupUpperCpuNs=0;bool tuningStartupClockValid=false;
        // Fields with at least one confirmed completed GPU dispatch. This is
        // distinct from a fully converged, successfully committed GPU result.
        std::uint64_t deviceCompletedFields=0,fallbackAfterDeviceCompletionFields=0;
        std::uint64_t stale=0, budgetDeclines=0, observationDrops=0;
        std::uint64_t publicationStalls=0,clockInvalidMeasurements=0;
        std::uint64_t captures=0,captureDeclines=0,probesStarted=0,probesExact=0,probesCanceled=0;
        std::uint64_t probeCallingCpuNs=0,probeUnknownCpuNs=0,probeProcessCpuNs=0,probeRetired=0;
        bool automaticTuning=false;
        CpuSavingPolicy::Metrics automaticPolicy;
        std::uint64_t batchPreparationCpuNs=0,batchSubmissionCpuNs=0,batchCompletionCpuNs=0;
        std::size_t queued=0, retainedHostBytes=0;
        bool running=false, ready=false,automaticClockUnavailable=false;
        unsigned configuredMaxBatch=8, deviceConcurrency=1;
        unsigned coordinatorThreads=0;
        std::uint64_t coordinatorThreadId=0;
        bool diagnostics=false;
        bool crossDueRequested=false,crossDueReady=false;
        unsigned batchProfiles=0;
        std::uint64_t batchManifestHash=0,batchSourceHash=0,batchProfileDeclines=0,crossDueBatches=0;
        std::uint64_t cadenceSamples=0,cadenceDrops=0,cadenceFloorNs=0,cadenceRevision=0;
        bool cpuEnvelopeRequested=false,cpuEnvelopeRegistryReady=false;
        // Sum of overlapping narrow batch windows is diagnostic only. It must
        // never be reported as total process CPU or causal driver attribution.
        std::uint64_t cpuEnvelopeWindows=0,cpuEnvelopeInvalid=0,cpuEnvelopeDeclines=0,cpuEnvelopeChurn=0;
        std::uint64_t cpuEnvelopeProcessNs=0,cpuEnvelopeKnownInnerNs=0,cpuEnvelopeUnknownUpperNs=0,cpuEnvelopeSamplerNs=0;
        std::uint64_t cpuEnvelopeOmittedThreads=0,cpuEnvelopeMaxResolutionNs=0,cpuEnvelopeRegistrySetupCpuNs=0,cpuEnvelopeRoleRegistrationCpuNs=0;
        // Lifecycle includes allocation, all start/finish advances and release;
        // sampler is a component of lifecycle, and both overlap hostCpuNs.
        std::uint64_t cpuEnvelopeLifecycleNs=0,cpuEnvelopeSetupOvershoots=0,cpuEnvelopeFinishOvershoots=0;
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
    bool crossDueRequested() const noexcept;
    // Owner sends only O(1) timing metadata. Window statistics and profile I/O
    // run on the broker; this never waits for a manifest or cadence readiness.
    void recordCadence(GradientCadenceSample sample) noexcept;
    // Required workers submit bounded metadata only; learning runs when the
    // coordinator has no required device work. Overflow drops optional learning.
    void recordAccepted(std::shared_ptr<BackendSession>,const WorkloadKey&,Plan,
                        std::uint64_t hostCpuNs,std::uint64_t tick,bool failed=false,bool publicationStall=false) noexcept;
    // These operations are called only from a CPU worker. They never dispatch,
    // compile or join optional work; successful captures queue immutable bytes.
    std::unique_ptr<GradientCpuCapture> beginCapture(std::shared_ptr<BackendSession>,
        GradientCpuCapture::Input,GradientCpuCapture::Receipt&) noexcept;
    bool submitCapture(std::shared_ptr<GradientCpuCapture::Record>,std::shared_ptr<BackendSession>) noexcept;
    std::unique_ptr<GroupCapture> beginGroupCapture(std::shared_ptr<BackendSession>,
        GradientCpuCaptureGroup::Input,GradientCpuCapture::Receipt&) noexcept;
    bool learningActive() const noexcept;
    GradientBatchChoice acceptedBatchChoice(const std::shared_ptr<BackendSession>&,
        const WorkloadKey&) const noexcept;
    void recordBatchOfferFallback(std::shared_ptr<BackendSession>,const WorkloadKey&,Plan,
        std::uint64_t version,std::uint64_t hostCpuNs,std::uint64_t tick,bool clockValid) noexcept;
};
}
