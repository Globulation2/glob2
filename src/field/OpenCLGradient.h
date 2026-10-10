// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <span>
#include <cstddef>
#include <memory>
namespace gradient_kernel
{
struct BackendRequest;
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
    // promotion: the optional yielding probe intentionally declines this mode.
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
};
// Status only: never initializes or compiles. Worker-only maintenance publishes
// readiness; required callers keep using CPU until a selected plan is ready.
OpenCLStatus openCLStatus();
// These operations are also valid on an owned device-service thread without an
// executor worker slot. Execution never selects a plan or performs CPU recovery.
bool initializeOpenCL();
bool executeOpenCLDevice(std::span<const BackendRequest> requests, Plan plan);
// Process-owned coordinators retain this lease until after their thread joins.
// It pins the backend/API library, without initializing or compiling a driver.
std::shared_ptr<const void> retainOpenCLLifetime();
enum class OpenCLProbeProgress { Pending, Complete, Declined };
// Development-only optional execution. Required requests keep the synchronous
// API. Each normal advance enqueues at most one kernel and never waits for the
// driver. A driver failure before a trailing event exists requires an emergency
// drain to protect borrowed transfer storage; such failures cannot be promoted.
// keepAlive must retain original gradient, cost context and session lifetimes;
// source/cost storage must remain immutable and be charged to the shared probe
// budget by its owner. In particular, keepAlive must retain that owner's probe
// budget lease for original seed capacity and DTO/captured payload until probe
// destruction. Generic keepAlive allocation sizes are unknowable here, so the
// backend accounts only its staging/state/lane and retained cost plane.
// Backend staging is charged
// separately. Unknown/unready cost planes are declined without preparation.
// Required-only active-epoch/parity-binding/direct-seed ablations are also declined; their
// results cannot supply automatic-promotion evidence until yielding counterparts
// have been independently implemented and qualified.
class OpenCLProbe
{
public:
    struct Metrics {std::uint64_t threadCpuNs=0,maxAdvanceCpuNs=0,overshoots=0,dispatches=0,setupCpuNs=0;};
    ~OpenCLProbe();
    OpenCLProbe(const OpenCLProbe&)=delete;
    OpenCLProbe& operator=(const OpenCLProbe&)=delete;
    OpenCLProbeProgress advance(std::size_t copyCells=4096,std::uint64_t cpuBudgetNs=500000) noexcept;
    void cancel() noexcept;
    // Copy phases check CPU time at most every64 entries. Probe results never
    // overwrite the original seeds. Valid only at Complete.
    std::span<const std::uint16_t> result() const noexcept;
    Metrics metrics() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> state;
    explicit OpenCLProbe(std::unique_ptr<Impl>);
    friend std::unique_ptr<OpenCLProbe> beginOpenCLProbe(const BackendRequest&,Plan,std::shared_ptr<const void>);
};
// Cancellation parks pending events: call advance until terminal before normal
// destruction. A premature destructor defensively drains on its calling thread.
std::unique_ptr<OpenCLProbe> beginOpenCLProbe(const BackendRequest&,Plan,std::shared_ptr<const void> keepAlive);
} // namespace gradient_kernel
