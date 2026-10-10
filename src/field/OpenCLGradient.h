// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <span>
#include <cstddef>
#include <memory>
#include <array>
namespace gradient_kernel
{
struct BackendRequest;
struct OpenCLProbeInput;
enum class Plan : unsigned;
inline constexpr std::size_t OpenCLHostBudget = 64 * 1024 * 1024;
inline constexpr std::size_t OpenCLDeviceBudget = 128 * 1024 * 1024;
inline constexpr std::size_t OpenCLProbeBudget = 16 * 1024 * 1024;
// Shared payload budget: includes service-owned requests, staging and cost planes.
// A successful reservation must be released exactly once by its owner.
bool reserveOpenCLHostBytes(std::size_t bytes) noexcept;
void releaseOpenCLHostBytes(std::size_t bytes) noexcept;
// Optional CPU/GPU reference storage shares one process-wide subset of the
// total host budget. These reservations also charge the total host budget.
bool reserveOpenCLProbeBytes(std::size_t bytes) noexcept;
void releaseOpenCLProbeBytes(std::size_t bytes) noexcept;
std::size_t openCLProbeBytes() noexcept;
// Synthetic tracked-budget pressure only; no GPU allocation or driver query.
// Explicit development tests retain this owned lease until after probe cleanup.
std::shared_ptr<const void> reserveOpenCLDeviceBytesForDiagnostics(std::size_t bytes);
enum class OpenCLCpuCategory : unsigned {Preparation,Upload,Arguments,Fill,KernelEnqueue,CheckRead,OutputRead,OutputCopy,Other,Count};
struct OpenCLApiCpuCounters {
    std::array<std::uint64_t,unsigned(OpenCLCpuCategory::Count)> ns{},calls{};
    std::uint64_t invalidScopes=0,reconciliationErrors=0,controlBracketNs=0,clockReads=0,coveredNs=0;
};
struct OpenCLStatus
{
    bool available = false;
    std::string device, error;
    std::uint64_t fields = 0, calibrations = 0, cpuSelections = 0;
    std::uint64_t batches = 0, maxBatchFields = 0, costUploads = 0, costCacheHits = 0;
    std::uint64_t dispatches = 0, hostChecks = 0;
    std::uint64_t costIdentityHits = 0, retiredFields = 0, schedulerBatches = 0, tunings = 0;
    std::uint64_t executionLanes = 0, maxConcurrentBatches = 0;
    bool colored = false;
    unsigned tileWidth = 16, tileHeight = 16, localSteps = 4;
    std::uint64_t hostBytes = 0, peakHostBytes = 0, deviceBytes = 0, peakDeviceBytes = 0;
    std::uint64_t budgetDeclines = 0, threadCPUNs = 0, initializationThreadCPUNs = 0;
    std::uint64_t preparationNs = 0, uploadNs = 0, dispatchWaitNs = 0, readbackNs = 0;
    unsigned checkInterval = 8;
    bool threadCPUAvailable = false;
    unsigned pollMicros = 0;
    bool deviceProfiling = false;
    std::uint64_t deviceUploadNs = 0, deviceKernelNs = 0, deviceReadbackNs = 0;
    std::uint64_t deviceCheckReadNs = 0, profilingErrors = 0;
    std::uint64_t noopFields = 0;
    bool uniformMetadata = false;
    std::uint64_t uniformMetadataHits = 0;
    std::uint64_t probeBytes = 0, peakProbeBytes = 0;
    // Experimental command-reduction ablation. Not qualified for automatic
    // promotion: its matching isolated yielding probe still needs qualification.
    bool activeEpoch = false;
    std::uint64_t tileMaskInitializations = 0, tileMaskClears = 0;
    // Independent required-only ablation: two lane-private kernel instances
    // retain fixed pingpong bindings, avoiding four argument setters/dispatch.
    bool parityBound = false;
    std::uint64_t kernelArgumentUpdates = 0;
    // Appended to retain positional unavailable-platform initializers.
    // Immutable initialization metadata. Empty values decline exact offline
    // configuration matching; status queries never call into the driver.
    std::string platform, platformVendor, platformVersion;
    std::string deviceVendor, driverVersion, deviceVersion, openCLCVersion;
    // Confirmed kernel completion versus successful transactional output commit.
    std::uint64_t deviceObservedFields = 0, committedFields = 0;
    // Independent required-only memcpy removal. Mixed batches retain staging;
    // yielding probes decline this configuration until separately qualified.
    bool directSeedUploadRequested = false, directSeedUpload = false;
    std::uint64_t directSeedUploads = 0, seedCopiedBytes = 0, seedUploadedBytes = 0;
    std::uint64_t directSeedUploadedBytes = 0, outputCopiedBytes = 0;
    // Physical identity is optional for execution, required by offline batching
    // evidence. Names alone cannot distinguish two identical adapters.
    std::string deviceUUIDHex;
    unsigned deviceOrdinal = 0;
    bool deviceOrdinalKnown = false;
    std::uint64_t threadCPUInvalidMeasurements = 0;
    // Background required execution diagnostic: 0 off, 1 exclusive actual
    // scopes, 2 same scopes/read count with empty brackets before actual work.
    // Includes calling-thread CPU only; never qualifies automatic promotion.
    unsigned apiCpuMode = 0;
    bool apiCpuConfigured = false;
    OpenCLApiCpuCounters apiCpu;
    std::uint64_t optionalQuarantines=0;
    bool optionalQuarantinePending=false;
    std::uint64_t requiredQuarantines=0,requiredProofFailures=0;
    bool requiredQuarantinePending=false;
    std::uint64_t directSeedUploadSafetyCopies=0,requiredCancellations=0;
    // Isolated optional preparation only; never publishes pending/cache planes.
    bool probeColdCosts=false;
};
// Status only: never initializes or compiles. Worker-only maintenance publishes
// readiness; required callers keep using CPU until a selected plan is ready.
OpenCLStatus openCLStatus();
// These operations are also valid on an owned device-service thread without an
// executor worker slot. Execution never selects a plan or performs CPU recovery.
// Background single coordinator only. No waits, initialization or GPU restart.
bool reapOpenCLRequiredQuarantine() noexcept;
bool initializeOpenCL();
bool executeOpenCLDevice(std::span<const BackendRequest> requests, Plan plan);
// Process-owned coordinators retain this lease until after their thread joins.
// It pins the backend/API library, without initializing or compiling a driver.
std::shared_ptr<const void> retainOpenCLLifetime();
enum class OpenCLProbeProgress { Pending, Complete, Declined, Quarantined };
// The same stable background coordinator owns begin, advance, result/metrics,
// destruction and quarantine reaping. They must never run concurrently for a
// probe: positive reaping mutates/releases its trailing event without acquiring
// the lane mutex. Only atomic cancel may be requested from another thread.
// Background/coordinator only: poll an existing trailing event without waiting.
// False leaves ambiguous state, all payload leases and admission retained.
bool reapOpenCLProbeQuarantine() noexcept;
// Development-only optional execution. Required requests keep the synchronous
// API. Each normal advance enqueues at most one kernel and never waits for the
// driver. Every accepted command retains a trailing event. Unknown completion
// quarantines borrowed storage and disables GPU admissions; no blocking Finish
// or completion inference from a negative event status is permitted here.
// keepAlive must retain original gradient, cost context and session lifetimes;
// source/cost storage must remain immutable and be charged to the shared probe
// budget by its owner. In particular, keepAlive must retain that owner's probe
// budget lease for original seed capacity and DTO/captured payload until probe
// destruction. Generic keepAlive allocation sizes are unknowable here, so the
// backend accounts only its staging/state/lane and retained cost plane.
// Backend staging is charged
// separately. Unknown/unready planes decline by default; the isolated cold
// preparation opt-in retains its completed plane privately through destruction.
// Epoch probes use the same initialized program and required check window,
// private kernel/buffers and one-dispatch advances. Their yielded/cold lifecycle
// CPU is not a faithful required-batch timing reference or full process cost;
// capture and automatic promotion remain disabled pending independent gates.
// Required-only parity-binding/direct-seed/API-CPU ablations are also declined; their
// results cannot supply automatic-promotion evidence until yielding counterparts
// have been independently implemented and qualified.
// Optional owned clock injection is for correctness diagnostics. Its owner
// must be retained and charged by the caller like other captured probe payload.
struct OpenCLProbeClock {
    std::shared_ptr<const void> owner;
    std::uint64_t (*read)(const void*) noexcept = nullptr;
};
class OpenCLProbe
{
public:
    enum class Stage {Costs,Reserve,Copy,Queue,Buffers,Upload,InitialWait,Dispatch,KernelWait,CheckWait,OutputWait,CostsReserve,CostsPack,CostsUpload,CostsWait};
    struct Metrics {
        std::uint64_t threadCpuNs=0,maxAdvanceCpuNs=0,overshoots=0,dispatches=0,setupCpuNs=0;
        std::uint64_t hostChecks=0,tileMaskInitializations=0,tileMaskClears=0,kernelArgumentUpdates=0;
        std::uint64_t invalidCpuClocks=0,costCellsPrepared=0,costPreparedBytes=0,costUploads=0;
        bool coldCostPlane=false;
        bool quarantined=false;
        unsigned checkInterval=1;
        bool activeEpoch=false;
        Stage stage=Stage::Costs;
        // Driver/protocol failure, distinct from cancellation or admission decline.
        bool executionFailed=false;
        // Successful active-epoch dispatches replacing an older in-order event
        // without polling it. At most checkInterval kernels remain queued.
        std::uint64_t queuedWindowDispatches=0;
    };
    ~OpenCLProbe();
    OpenCLProbe(const OpenCLProbe&)=delete;
    OpenCLProbe& operator=(const OpenCLProbe&)=delete;
    OpenCLProbeProgress advance(std::size_t copyCells=4096,std::uint64_t cpuBudgetNs=500000) noexcept;
    void cancel() noexcept;
    // Copy phases check CPU time at most every64 entries. Probe results never
    // overwrite the original seeds. Valid only at Complete.
    std::span<const std::uint16_t> result() const noexcept;
    // Invalid clock measurements decline the optional result; a future learning
    // caller must conservatively charge its full reservation or use a known
    // external measurement, never settle missing CPU expense as zero.
    // Read from the same coordinator that advances the probe. Cancellation is
    // independently atomic; metrics and result do not supply concurrent reads.
    Metrics metrics() const noexcept;
    // Same coordinator only; no driver call. A retained trailing event has no
    // terminal proof yet at a transfer/check boundary. An active epoch dispatch
    // within its bounded window can replace that event without polling it.
    // CPU setup/comparison may instead resume immediately
    // after the broker checks required work again.
    bool waitingForEvent() const noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> state;
    explicit OpenCLProbe(std::shared_ptr<Impl>);
    friend std::unique_ptr<OpenCLProbe> beginOpenCLProbe(const BackendRequest&,Plan,std::shared_ptr<const void>,OpenCLProbeClock);
    friend std::unique_ptr<OpenCLProbe> beginOpenCLProbe(const OpenCLProbeInput&,Plan,std::shared_ptr<const void>,OpenCLProbeClock);
    friend bool reapOpenCLProbeQuarantine() noexcept;
};
// Cancellation parks pending events: call advance until terminal before normal
// destruction. Premature destruction or an ambiguous completion quarantines
// the heap-owned graph and disables GPU admissions; it never blocks on Finish.
std::unique_ptr<OpenCLProbe> beginOpenCLProbe(const BackendRequest&,Plan,std::shared_ptr<const void> keepAlive,OpenCLProbeClock clock={});
std::unique_ptr<OpenCLProbe> beginOpenCLProbe(const OpenCLProbeInput&,Plan,std::shared_ptr<const void> keepAlive,OpenCLProbeClock clock={});
} // namespace gradient_kernel
