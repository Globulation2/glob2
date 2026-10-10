// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
namespace gradient_kernel
{
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
};
// Status only: never initializes or compiles. Worker-only maintenance publishes
// readiness; required callers keep using CPU until a selected plan is ready.
OpenCLStatus openCLStatus();
} // namespace gradient_kernel
