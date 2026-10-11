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
// Shared payload budget: includes service-owned requests, staging and cost planes.
// A successful reservation must be released exactly once by its owner.
bool reserveOpenCLHostBytes(std::size_t bytes) noexcept;
void releaseOpenCLHostBytes(std::size_t bytes) noexcept;
std::size_t openCLHostBytes() noexcept;
struct OpenCLStatus
{
    bool available=false;
    std::string device,error;
    std::uint64_t fields=0,cpuSelections=0,batches=0,maxBatchFields=0;
    std::uint64_t costUploads=0,costCacheHits=0,costIdentityHits=0;
    std::uint64_t dispatches=0,hostChecks=0,retiredFields=0,schedulerBatches=0;
    std::uint64_t executionLanes=0,maxConcurrentBatches=0;
    bool colored=true;
    unsigned tileWidth=16,tileHeight=16,localSteps=8,checkInterval=8,pollMicros=100;
    std::uint64_t hostBytes=0,peakHostBytes=0,deviceBytes=0,peakDeviceBytes=0;
    std::uint64_t budgetDeclines=0,threadCPUNs=0,initializationThreadCPUNs=0;
    bool threadCPUAvailable=false;
    std::uint64_t threadCPUInvalidMeasurements=0,noopFields=0;
    std::uint64_t tileMaskInitializations=0,tileMaskClears=0,kernelArgumentUpdates=0;
    std::uint64_t deviceObservedFields=0,committedFields=0;
    std::uint64_t seedCopiedBytes=0,seedUploadedBytes=0,outputCopiedBytes=0;
    std::string platform,platformVendor,platformVersion;
    std::string deviceVendor,driverVersion,deviceVersion,openCLCVersion,deviceUUIDHex;
    unsigned deviceOrdinal=0;
    bool deviceOrdinalKnown=false;
    std::uint64_t requiredQuarantines=0,requiredProofFailures=0,requiredCancellations=0;
    bool requiredQuarantinePending=false;
};
// Status only: never initializes or compiles. Worker-only maintenance publishes
// readiness; required callers keep using CPU until a selected plan is ready.
OpenCLStatus openCLStatus();
// These operations are also valid on an owned device-service thread without an
// executor worker slot. Execution never selects a plan or performs CPU recovery.
// Background single coordinator only. No waits, initialization or GPU restart.
bool reapOpenCLRequiredQuarantine() noexcept;
// Bounded metadata only: no driver query, allocation or initialization.
bool requiredOpenCLQuarantinePending() noexcept;
bool initializeOpenCL();
bool executeOpenCLDevice(std::span<const BackendRequest> requests, Plan plan);
// Process-owned coordinators retain this lease until after their thread joins.
// It pins the backend/API library, without initializing or compiling a driver.
std::shared_ptr<const void> retainOpenCLLifetime();
} // namespace gradient_kernel
