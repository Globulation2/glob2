// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <span>
#include <cstddef>
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
};
// Status only: never initializes or compiles. Worker-only maintenance publishes
// readiness; required callers keep using CPU until a selected plan is ready.
OpenCLStatus openCLStatus();
// These operations are also valid on an owned device-service thread without an
// executor worker slot. Execution never selects a plan or performs CPU recovery.
bool initializeOpenCL();
bool executeOpenCLDevice(std::span<const BackendRequest> requests, Plan plan);
} // namespace gradient_kernel
