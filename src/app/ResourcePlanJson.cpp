// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourcePlanJson.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace resource_policy
{
namespace
{
using Json = nlohmann::json;
template<class T> Json optional(const std::optional<T> &value)
{
    return value ? Json(*value) : Json(nullptr);
}
Json profile(const std::optional<ExecutionProfile> &value)
{
    return value ? Json(executionProfileName(*value)) : Json(nullptr);
}
std::string_view name(GpuPolicy value)
{
    switch (value)
    {
    case GpuPolicy::Automatic: return "automatic";
    case GpuPolicy::Disabled: return "disabled";
    case GpuPolicy::Enabled: return "enabled";
    }
    throw std::invalid_argument("Invalid GPU resource policy for JSON observation");
}
Json gpu(const std::optional<GpuPolicy> &value)
{
    return value ? Json(name(*value)) : Json(nullptr);
}
std::string_view name(ProfileSource value)
{
    switch (value)
    {
    case ProfileSource::ExplicitProfile: return "explicit_profile";
    case ProfileSource::SavedPreference: return "saved_preference";
    case ProfileSource::ContextDefault: return "context_default";
    }
    throw std::invalid_argument("Invalid profile provenance for JSON observation");
}
std::string_view name(ValueSource value)
{
    switch (value)
    {
    case ValueSource::ExplicitOverride: return "explicit_override";
    case ValueSource::ValidatedDefault: return "validated_default";
    }
    throw std::invalid_argument("Invalid resource provenance for JSON observation");
}
std::string_view name(Diagnostic value)
{
    switch (value)
    {
    case Diagnostic::UnknownCpuUsesSerial: return "unknown_cpu_uses_serial";
    case Diagnostic::ExplicitCpuOversubscription: return "explicit_cpu_oversubscription";
    case Diagnostic::RenderGpuUnavailable: return "render_gpu_unavailable";
    case Diagnostic::ComputeGpuUnavailable: return "compute_gpu_unavailable";
    }
    throw std::invalid_argument("Invalid resource diagnostic for JSON observation");
}
Json constraints(const RuntimeConstraints &value)
{
    return {{"on_battery", optional(value.onBattery)}, {"battery_saver", optional(value.batterySaver)},
            {"backgrounded", optional(value.backgrounded)},
            {"memory_pressure", optional(value.memoryPressure)}, {"memory_limit_bytes", optional(value.memoryLimitBytes)}};
}
Json resources(const ResourceDefaults &value)
{
    return {{"compute_participants", value.computeParticipants}, {"compute_participants_include_owner", true},
            {"planning_memory_budget_bytes", optional(value.planningMemoryBytes)},
            {"recoverable_memory_budget_bytes", value.recoverableMemoryBytes},
            {"render_frame_rate_limit", value.render.frameRate}, {"render_resolution_scale", value.render.resolutionScale},
            {"render_detail", value.render.detail}, {"presentation_preparation_budget_us", value.render.preparationBudgetUs},
            {"gpu_render_policy", name(value.renderGpu)}, {"gpu_compute_policy", name(value.computeGpu)}};
}
}
nlohmann::json describeExecutionProfiles()
{
    Json profiles = Json::array();
    for (const auto value : {ExecutionProfile::Auto, ExecutionProfile::Eco, ExecutionProfile::Balanced,
                            ExecutionProfile::Luxurious, ExecutionProfile::FastSimulation, ExecutionProfile::EfficientThroughput})
        profiles.push_back(executionProfileName(value));
    return {{"policy_version", ResourcePlan::Version}, {"resource_plan_schema_version", 1}, {"profiles", profiles}};
}
nlohmann::json describeResourcePlan(const ResourcePlan &plan)
{
    if (plan.version != ResourcePlan::Version) throw std::invalid_argument("Unsupported resource policy version for JSON observation");
    const auto &request = plan.request;
    const auto &device = plan.capabilities;
    const auto &workload = plan.workload;
    Json diagnostics = Json::array();
    for (const auto value : plan.diagnostics) diagnostics.push_back(name(value));
    return {
        {"schema_version", 1}, {"policy_version", plan.version},
        {"requested_execution_profile", executionProfileName(plan.requestedProfile)},
        {"selected_execution_profile", executionProfileName(plan.selectedProfile)},
        {"profile_source", name(plan.profileSource)}, {"profile_tuning_pending", plan.profileTuningPending},
        {"resolved_resources", resources(plan.resources)},
        {"resource_sources", {
            {"compute_participants", name(plan.participantSource)}, {"planning_memory_budget_bytes", name(plan.planningMemorySource)},
            {"recoverable_memory_budget_bytes", name(plan.recoverableMemorySource)}, {"render_frame_rate_limit", name(plan.frameRateSource)},
            {"render_resolution_scale", name(plan.resolutionSource)}, {"render_detail", name(plan.detailSource)},
            {"presentation_preparation_budget_us", name(plan.preparationSource)}, {"gpu_render_policy", name(plan.renderGpuSource)},
            {"gpu_compute_policy", name(plan.computeGpuSource)}}},
        {"device_capabilities", {
            {"allocated_cpu_participants", optional(device.allocatedCpuParticipants)}, {"hard_participant_limit", optional(device.hardParticipantLimit)},
            {"threads_supported", optional(device.threadsSupported)}, {"gpu_render_supported", optional(device.renderGpuSupported)},
            {"gpu_compute_supported", optional(device.computeGpuSupported)}, {"allocated_memory_bytes", optional(device.allocatedMemoryBytes)}}},
        {"runtime_constraints", constraints(request.constraints)},
        {"original_request", {
            {"explicit_execution_profile", profile(request.explicitProfile)}, {"saved_execution_profile", profile(request.savedProfile)},
            {"context_default_execution_profile", executionProfileName(request.contextDefault)},
            {"resource_overrides", {
                {"compute_participants", optional(request.computeParticipants)}, {"planning_memory_budget_bytes", optional(request.planningMemoryBytes)},
                {"recoverable_memory_budget_bytes", optional(request.recoverableMemoryBytes)}, {"render_frame_rate_limit", optional(request.renderFrameRate)},
                {"render_resolution_scale", optional(request.renderResolutionScale)}, {"render_detail", optional(request.renderDetail)},
                {"presentation_preparation_budget_us", optional(request.renderPreparationBudgetUs)}, {"gpu_render_policy", gpu(request.renderGpu)},
                {"gpu_compute_policy", gpu(request.computeGpu)}}}, {"runtime_constraints", constraints(request.constraints)}}},
        {"workload", {
            {"required_live_bytes", workload.requiredLiveBytes}, {"validated_defaults", resources(workload.defaults)},
            {"maximum_render_frame_rate", optional(workload.maximumFrameRate)}, {"maximum_render_resolution_scale", optional(workload.maximumResolutionScale)},
            {"maximum_compute_participants", optional(workload.maximumComputeParticipants)},
            {"maximum_render_detail", optional(workload.maximumRenderDetail)}, {"maximum_presentation_preparation_budget_us", optional(workload.maximumRenderPreparationBudgetUs)}}},
        {"diagnostics", diagnostics},
        {"semantics", {
            {"unknown_capabilities", "null; false is a known unsupported state"},
            {"resource_sources", "winning input before capability bounds or fallback; inspect resolved_resources for effective values"},
            {"memory_budget_scope", "planning estimates, not enforced process RSS limits"}, {"process_rss_limit_enforced", false},
            {"recoverable_scope", "derived read-only or idle unleased storage; excludes simulation-visible cache state, replay history and consumer horizons"},
            {"render_frame_rate_zero", "uncapped"}, {"presentation_preparation_unit", "microseconds"},
            {"presentation_preparation_zero", "no discretionary preparation"}, {"presentation_preparation_scope", "required work excluded"},
            {"gpu_enabled", "offload permitted with existing fallback; not proof of execution or a named renderer backend promise"},
            {"profile_tuning", "pending; current caller-validated defaults, no six optimized tier claim"}}}
    };
}
} // namespace resource_policy
