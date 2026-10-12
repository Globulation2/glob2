// SPDX-License-Identifier: GPL-3.0-or-later
#include "ApplicationResourcePlan.h"
#include "ComputeThreads.h"
#include <ThreadSupport.h>
#include <SDL3/SDL.h>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace resource_policy
{
std::uint64_t parsePlanningMemoryBytes(std::string_view text)
{
    if (text.empty()) throw std::invalid_argument("--memory-budget-bytes expects 1..INT64_MAX");
    std::uint64_t bytes = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), bytes);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        bytes == 0 || bytes > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
        throw std::invalid_argument("--memory-budget-bytes expects 1..INT64_MAX");
    return bytes;
}
DeviceCapabilities applicationCapabilitiesFromCpuAvailability(const CpuAvailabilityObservation& observation, bool threadsSupported)
{
    DeviceCapabilities device;
    // Only an observed eligible logical mask is a known allocation. A caller
    // may separately inject its own explicit allocation through capabilities.
    device.allocatedCpuParticipants = observation.eligibleLogicalCpuCount;
    device.threadsSupported = threadsSupported;
#if defined(__EMSCRIPTEN__) || defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    device.computeGpuSupported = false; // Current native OpenCL backend is absent.
#endif
    // Driver availability, renderer support and memory allocation remain unknown.
    return device;
}
DeviceCapabilities discoverApplicationResourceCapabilities(CpuAvailabilityObservation* observation)
{
    const auto sampled=discoverComputeCpuAvailability();
    if(observation) *observation=sampled;
    return applicationCapabilitiesFromCpuAvailability(sampled,GAGCore::ThreadSupport::available);
}
ResourcePlan resolveApplicationResourcePlan(const ApplicationResourceInputs &input)
{
    auto request = input.request;
    request.savedProfile = input.savedProfile;
    request.contextDefault = input.headless ? ExecutionProfile::FastSimulation : ExecutionProfile::Auto;
    if (input.legacyRequestedComputeParticipants)
    {
        if (request.computeParticipants && *request.computeParticipants != input.legacyRequestedComputeParticipants)
            throw std::invalid_argument("Conflicting explicit compute participant requests");
        request.computeParticipants = input.legacyRequestedComputeParticipants;
    }
    if (input.legacyGpuComputeExplicit)
    {
        const auto legacyGpu = input.legacyGpuComputeEnabled ? GpuPolicy::Enabled : GpuPolicy::Disabled;
        if (request.computeGpu && *request.computeGpu != legacyGpu)
            throw std::invalid_argument("Conflicting explicit GPU compute requests");
        request.computeGpu = legacyGpu;
    }
    WorkloadRequirements workload;
    if (input.ownerOnly) workload.maximumComputeParticipants = 1;
    workload.defaults.computeParticipants = defaultComputeThreadCount(input.capabilities.allocatedCpuParticipants.value_or(1));
    workload.defaults.computeGpu = input.legacyGpuComputeEnabled ? GpuPolicy::Enabled : GpuPolicy::Disabled;
    workload.defaults.render = input.currentPresentation;
    // No application live-memory census exists here; no retention/render action
    // consumes these planning values. Keep allocation/budget unknown rather than
    // guessing memory tiers or changing any simulation-visible cache policy.
    return resolveResourcePlan(request, input.capabilities, workload);
}
} // namespace resource_policy
