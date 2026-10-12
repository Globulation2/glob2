// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourcePolicy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace resource_policy
{
namespace
{
constexpr std::array<std::string_view, 6> Names{"auto", "eco", "balanced", "luxurious", "fast-simulation", "efficient-throughput"};
void valid(GpuPolicy p)
{
    if (p != GpuPolicy::Automatic && p != GpuPolicy::Disabled && p != GpuPolicy::Enabled)
        throw std::invalid_argument("Invalid GPU resource policy");
}
void positive(std::optional<unsigned> value, const char *message)
{
    if (value && !*value) throw std::invalid_argument(message);
}
void scale(double value)
{
    if (!std::isfinite(value) || value <= 0) throw std::invalid_argument("Resolution scale must be finite and positive");
}
template<class T> T choose(const std::optional<T> &override, T fallback, ValueSource &source)
{
    source = override ? ValueSource::ExplicitOverride : ValueSource::ValidatedDefault;
    return override.value_or(fallback);
}
std::optional<std::uint64_t> lower(std::optional<std::uint64_t> a, std::optional<std::uint64_t> b)
{
    return a && b ? std::optional<std::uint64_t>(std::min(*a, *b)) : a ? a : b;
}
GpuPolicy gpu(GpuPolicy requested, std::optional<bool> supported, Diagnostic reason, ResourcePlan &plan)
{
    if (supported && !*supported && requested != GpuPolicy::Disabled)
    {
        plan.diagnostics.push_back(reason);
        return GpuPolicy::Disabled; // A request permits offload; exact CPU/software fallback remains valid.
    }
    return requested;
}
}
ExecutionProfile parseExecutionProfile(std::string_view name)
{
    const auto it = std::find(Names.begin(), Names.end(), name);
    if (it == Names.end()) throw std::invalid_argument("Unknown execution resource profile");
    return static_cast<ExecutionProfile>(it - Names.begin());
}
std::string_view executionProfileName(ExecutionProfile profile)
{
    const auto index = static_cast<unsigned>(profile);
    if (index >= Names.size()) throw std::invalid_argument("Invalid execution resource profile");
    return Names[index];
}
ResourcePlan resolveResourcePlan(const ExecutionRequest &request, const DeviceCapabilities &device, const WorkloadRequirements &workload)
{
    executionProfileName(request.contextDefault);
    if (request.explicitProfile) executionProfileName(*request.explicitProfile);
    if (request.savedProfile) executionProfileName(*request.savedProfile);
    positive(request.computeParticipants, "Explicit compute participants must be positive");
    positive(device.allocatedCpuParticipants, "Known CPU allocation must be positive");
    positive(device.hardParticipantLimit, "Hard participant limit must be positive");
    positive(workload.maximumComputeParticipants, "Workload participant limit must be positive");
    if (!workload.defaults.computeParticipants) throw std::invalid_argument("Validated default participants must be positive");
    scale(workload.defaults.render.resolutionScale);
    if (request.renderResolutionScale) scale(*request.renderResolutionScale);
    if (workload.maximumResolutionScale) scale(*workload.maximumResolutionScale);
    valid(workload.defaults.renderGpu); valid(workload.defaults.computeGpu);
    if (request.renderGpu) valid(*request.renderGpu);
    if (request.computeGpu) valid(*request.computeGpu);
    ResourcePlan plan;
    plan.request = request; plan.capabilities = device; plan.workload = workload;
    plan.requestedProfile = request.explicitProfile.value_or(request.savedProfile.value_or(request.contextDefault));
    plan.selectedProfile = plan.requestedProfile;
    plan.profileSource = request.explicitProfile ? ProfileSource::ExplicitProfile : request.savedProfile ? ProfileSource::SavedPreference : ProfileSource::ContextDefault;
    auto &out = plan.resources;
    if (request.computeParticipants)
    {
        out.computeParticipants = *request.computeParticipants;
        plan.participantSource = ValueSource::ExplicitOverride;
        if (device.allocatedCpuParticipants && out.computeParticipants > *device.allocatedCpuParticipants)
            plan.diagnostics.push_back(Diagnostic::ExplicitCpuOversubscription);
    }
    else if (device.allocatedCpuParticipants && device.threadsSupported.value_or(false))
        out.computeParticipants = std::min(workload.defaults.computeParticipants, *device.allocatedCpuParticipants);
    else
    {
        out.computeParticipants = 1;
        if (!device.allocatedCpuParticipants || !device.threadsSupported.has_value())
            plan.diagnostics.push_back(Diagnostic::UnknownCpuUsesSerial);
    }
    const unsigned hardLimit = device.threadsSupported == false ? 1 : device.hardParticipantLimit.value_or(out.computeParticipants);
    if (request.computeParticipants && out.computeParticipants > hardLimit)
        throw std::invalid_argument("Explicit compute participants exceed supported platform limit");
    out.computeParticipants = std::min(out.computeParticipants, hardLimit);
    if (workload.maximumComputeParticipants)
    {
        if (request.computeParticipants && out.computeParticipants > *workload.maximumComputeParticipants)
            throw std::invalid_argument("Explicit compute participants exceed workload limit");
        out.computeParticipants = std::min(out.computeParticipants, *workload.maximumComputeParticipants);
    }
    plan.planningMemorySource = request.planningMemoryBytes ? ValueSource::ExplicitOverride : ValueSource::ValidatedDefault;
    const auto cap = lower(device.allocatedMemoryBytes, request.constraints.memoryLimitBytes);
    out.planningMemoryBytes = request.planningMemoryBytes ? request.planningMemoryBytes : workload.defaults.planningMemoryBytes;
    if (request.planningMemoryBytes && cap && *request.planningMemoryBytes > *cap)
        throw std::invalid_argument("Explicit planning memory exceeds known allocation or runtime constraint");
    out.planningMemoryBytes = lower(out.planningMemoryBytes, cap);
    if (out.planningMemoryBytes && *out.planningMemoryBytes < workload.requiredLiveBytes)
        throw std::invalid_argument("Planning memory cannot fit required live resources");
    out.recoverableMemoryBytes = choose(request.recoverableMemoryBytes, workload.defaults.recoverableMemoryBytes, plan.recoverableMemorySource);
    if (out.planningMemoryBytes)
    {
        const auto remaining = *out.planningMemoryBytes - workload.requiredLiveBytes;
        if (request.recoverableMemoryBytes && out.recoverableMemoryBytes > remaining)
            throw std::invalid_argument("Explicit recoverable memory exceeds live-resource headroom");
        out.recoverableMemoryBytes = std::min(out.recoverableMemoryBytes, remaining);
    }
    out.render.frameRate = choose(request.renderFrameRate, workload.defaults.render.frameRate, plan.frameRateSource);
    out.render.resolutionScale = choose(request.renderResolutionScale, workload.defaults.render.resolutionScale, plan.resolutionSource);
    out.render.detail = choose(request.renderDetail, workload.defaults.render.detail, plan.detailSource);
    out.render.preparationBudgetUs = choose(request.renderPreparationBudgetUs, workload.defaults.render.preparationBudgetUs, plan.preparationSource);
    if (workload.maximumFrameRate && (!out.render.frameRate || out.render.frameRate > *workload.maximumFrameRate))
        throw std::invalid_argument("Render frame rate exceeds validated bound");
    if (workload.maximumResolutionScale && out.render.resolutionScale > *workload.maximumResolutionScale)
        throw std::invalid_argument("Resolution scale exceeds validated bound");
    if (workload.maximumRenderDetail && out.render.detail > *workload.maximumRenderDetail)
        throw std::invalid_argument("Render detail exceeds validated bound");
    if (workload.maximumRenderPreparationBudgetUs && out.render.preparationBudgetUs > *workload.maximumRenderPreparationBudgetUs)
        throw std::invalid_argument("Render preparation exceeds validated bound");
    out.renderGpu = gpu(choose(request.renderGpu, workload.defaults.renderGpu, plan.renderGpuSource), device.renderGpuSupported, Diagnostic::RenderGpuUnavailable, plan);
    out.computeGpu = gpu(choose(request.computeGpu, workload.defaults.computeGpu, plan.computeGpuSource), device.computeGpuSupported, Diagnostic::ComputeGpuUnavailable, plan);
    return plan;
}
} // namespace resource_policy
