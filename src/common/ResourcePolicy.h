// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace resource_policy
{
// Names describe requested intent. The first-stage resolver does not invent tuned
// CPU, GPU, memory or rendering tiers: callers supply validated current defaults.
enum class ExecutionProfile { Auto, Eco, Balanced, Luxurious, FastSimulation, EfficientThroughput };
enum class GpuPolicy { Automatic, Disabled, Enabled };
enum class ProfileSource { ExplicitProfile, SavedPreference, ContextDefault };
enum class ValueSource { ExplicitOverride, ValidatedDefault };
enum class Diagnostic { UnknownCpuUsesSerial, ExplicitCpuOversubscription, RenderGpuUnavailable, ComputeGpuUnavailable };
ExecutionProfile parseExecutionProfile(std::string_view);
std::string_view executionProfileName(ExecutionProfile);

struct RenderResources
{
    unsigned frameRate = 0; // Zero means uncapped, as supplied by the application.
    double resolutionScale = 1;
    unsigned detail = 0;
    unsigned preparationBudgetUs = 0; // Discretionary microseconds; zero grants none. Required work is excluded.
    bool operator==(const RenderResources &) const = default;
};
struct ResourceDefaults
{
    unsigned computeParticipants = 1; // Includes the simulation owner.
    std::optional<std::uint64_t> planningMemoryBytes;
    std::uint64_t recoverableMemoryBytes = 0;
    RenderResources render;
    GpuPolicy renderGpu = GpuPolicy::Automatic, computeGpu = GpuPolicy::Disabled;
    bool operator==(const ResourceDefaults &) const = default;
};
struct RuntimeConstraints
{
    std::optional<bool> onBattery, batterySaver, backgrounded, memoryPressure;
    std::optional<std::uint64_t> memoryLimitBytes;
    bool operator==(const RuntimeConstraints &) const = default;
};
struct ExecutionRequest
{
    std::optional<ExecutionProfile> explicitProfile, savedProfile;
    ExecutionProfile contextDefault = ExecutionProfile::Auto;
    std::optional<unsigned> computeParticipants; // Positive explicit override; null means automatic.
    std::optional<std::uint64_t> planningMemoryBytes, recoverableMemoryBytes;
    std::optional<unsigned> renderFrameRate, renderDetail, renderPreparationBudgetUs;
    std::optional<double> renderResolutionScale;
    std::optional<GpuPolicy> renderGpu, computeGpu;
    RuntimeConstraints constraints;
    bool operator==(const ExecutionRequest &) const = default;
};
struct DeviceCapabilities
{
    std::optional<unsigned> allocatedCpuParticipants, hardParticipantLimit;
    std::optional<bool> threadsSupported, renderGpuSupported, computeGpuSupported;
    std::optional<std::uint64_t> allocatedMemoryBytes;
    bool operator==(const DeviceCapabilities &) const = default;
};
struct WorkloadRequirements
{
    ResourceDefaults defaults;
    std::uint64_t requiredLiveBytes = 0;
    // Workload execution support, independent of device allocation; includes owner.
    std::optional<unsigned> maximumComputeParticipants;
    // Optional validated application/platform bounds, not profile tuning guesses.
    std::optional<unsigned> maximumFrameRate, maximumRenderDetail, maximumRenderPreparationBudgetUs;
    std::optional<double> maximumResolutionScale;
    bool operator==(const WorkloadRequirements &) const = default;
};
struct ResourcePlan
{
    static constexpr unsigned Version = 1;
    unsigned version = Version;
    ExecutionProfile requestedProfile = ExecutionProfile::Auto, selectedProfile = ExecutionProfile::Auto;
    ProfileSource profileSource = ProfileSource::ContextDefault;
    bool profileTuningPending = true; // Intent recognized; six tuned tiers are not yet implemented.
    ResourceDefaults resources;
    ValueSource participantSource = ValueSource::ValidatedDefault;
    ValueSource planningMemorySource = ValueSource::ValidatedDefault, recoverableMemorySource = ValueSource::ValidatedDefault;
    ValueSource frameRateSource = ValueSource::ValidatedDefault, resolutionSource = ValueSource::ValidatedDefault;
    ValueSource detailSource = ValueSource::ValidatedDefault, preparationSource = ValueSource::ValidatedDefault;
    ValueSource renderGpuSource = ValueSource::ValidatedDefault, computeGpuSource = ValueSource::ValidatedDefault;
    std::vector<Diagnostic> diagnostics;
    // Value snapshots preserve known/unknown capabilities, constraints and provenance.
    ExecutionRequest request;
    DeviceCapabilities capabilities;
    WorkloadRequirements workload;
    bool operator==(const ResourcePlan &) const = default;
};
// Pure: no OS/environment/global state. Planning/derived retention limits do not
// authorize changing simulation-visible cache budgets, replay history or horizons.
ResourcePlan resolveResourcePlan(const ExecutionRequest &, const DeviceCapabilities &, const WorkloadRequirements &);
} // namespace resource_policy
