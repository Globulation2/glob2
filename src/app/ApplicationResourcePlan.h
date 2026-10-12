// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourcePolicy.h"

namespace resource_policy
{
// Current application values, not profile tiers or inferred device classes.
// Presentation values describe caller defaults; this adapter does not apply them.
struct ApplicationResourceInputs
{
    ExecutionRequest request;
    std::optional<ExecutionProfile> savedProfile;
    bool headless = false;
    bool ownerOnly = false; // This workload has no parallel compute executor.
    unsigned legacyRequestedComputeParticipants = 0; // Existing auto=0/N parser representation.
    bool legacyGpuComputeEnabled = false;
    bool legacyGpuComputeExplicit = false; // CLI or legacy environment override.
    RenderResources currentPresentation;
    DeviceCapabilities capabilities;
};
// One platform hint discovery per explicit refresh. No GPU initialization/probe.
DeviceCapabilities discoverApplicationResourceCapabilities();
std::uint64_t parsePlanningMemoryBytes(std::string_view);
// Pure injected bridge, suitable for tests without querying this machine.
ResourcePlan resolveApplicationResourcePlan(const ApplicationResourceInputs &);
} // namespace resource_policy
