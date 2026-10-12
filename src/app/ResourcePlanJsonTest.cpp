// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResourcePlanJson.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace resource_policy;
namespace
{
WorkloadRequirements jsonWorkload()
{
    WorkloadRequirements workload;
    workload.defaults.computeParticipants = 6;
    workload.defaults.planningMemoryBytes = 1024;
    workload.defaults.recoverableMemoryBytes = 256;
    workload.defaults.render = {0, 1, 2, 0};
    workload.requiredLiveBytes = 512;
    return workload;
}
DeviceCapabilities jsonDevice()
{
    DeviceCapabilities device; device.threadsSupported = true; device.allocatedCpuParticipants = 6;
    return device;
}
}
TEST_SUITE("ResourcePlanJson")
{
TEST_CASE("capability payload lists exactly six intents and does not imply current CLI support")
{
    const auto marker = describeExecutionProfiles();
    CHECK(marker.size() == 3);
    CHECK(marker.at("policy_version") == 1); CHECK(marker.at("resource_plan_schema_version") == 1);
    CHECK((marker.at("profiles").get<std::vector<std::string>>() == std::vector<std::string>{
        "auto", "eco", "balanced", "luxurious", "fast-simulation", "efficient-throughput"}));
    CHECK_FALSE(marker.contains("cli_supported"));
}
TEST_CASE("unknown null and known false true capability and constraints remain distinct")
{
    auto device = jsonDevice(); device.renderGpuSupported = false; device.computeGpuSupported = true;
    ExecutionRequest request; request.constraints.onBattery = false; request.constraints.memoryPressure = true;
    const auto json = describeResourcePlan(resolveResourcePlan(request, device, jsonWorkload()));
    CHECK(json.at("device_capabilities").at("gpu_render_supported").is_boolean());
    CHECK(json.at("device_capabilities").at("gpu_render_supported") == false);
    CHECK(json.at("device_capabilities").at("gpu_compute_supported") == true);
    CHECK(json.at("device_capabilities").at("allocated_memory_bytes").is_null());
    CHECK(json.at("runtime_constraints").at("on_battery") == false);
    CHECK(json.at("runtime_constraints").at("memory_pressure") == true);
    CHECK(json.at("runtime_constraints").at("backgrounded").is_null());
    CHECK(json.at("runtime_constraints") == json.at("original_request").at("runtime_constraints"));
}
TEST_CASE("explicit resource provenance describes winning input before effective GPU fallback")
{
    ExecutionRequest request; request.explicitProfile = ExecutionProfile::Eco; request.savedProfile = ExecutionProfile::Luxurious;
    request.computeParticipants = 12; request.computeGpu = GpuPolicy::Enabled;
    request.renderGpu = GpuPolicy::Disabled; request.renderResolutionScale = .75;
    auto device = jsonDevice(); device.computeGpuSupported = false;
    const auto json = describeResourcePlan(resolveResourcePlan(request, device, jsonWorkload()));
    CHECK(json.at("requested_execution_profile") == "eco"); CHECK(json.at("selected_execution_profile") == "eco");
    CHECK(json.at("profile_source") == "explicit_profile"); CHECK(json.at("profile_tuning_pending") == true);
    const auto &resolved = json.at("resolved_resources");
    CHECK(resolved.at("compute_participants") == 12); CHECK(resolved.at("compute_participants_include_owner") == true);
    CHECK(resolved.at("gpu_compute_policy") == "disabled"); CHECK(resolved.at("gpu_render_policy") == "disabled");
    CHECK(resolved.at("render_resolution_scale").is_number_float()); CHECK(resolved.at("render_resolution_scale") == .75);
    CHECK(json.at("resource_sources").at("gpu_compute_policy") == "explicit_override");
    CHECK(json.at("original_request").at("resource_overrides").at("gpu_compute_policy") == "enabled");
    CHECK(json.at("original_request").at("saved_execution_profile") == "luxurious");
    CHECK((json.at("diagnostics").get<std::vector<std::string>>() == std::vector<std::string>{
        "explicit_cpu_oversubscription", "compute_gpu_unavailable"}));
}
TEST_CASE("planning memory and discretionary preparation are observable not RSS promises")
{
    auto workload = jsonWorkload(); workload.defaults.planningMemoryBytes.reset(); workload.requiredLiveBytes = 0;
    ExecutionRequest request; request.recoverableMemoryBytes = 0; request.renderPreparationBudgetUs = 0;
    const auto json = describeResourcePlan(resolveResourcePlan(request, jsonDevice(), workload));
    const auto &resources = json.at("resolved_resources");
    CHECK(resources.at("planning_memory_budget_bytes").is_null());
    CHECK(resources.at("recoverable_memory_budget_bytes").is_number_unsigned()); CHECK(resources.at("recoverable_memory_budget_bytes") == 0);
    CHECK(resources.at("presentation_preparation_budget_us") == 0); CHECK(resources.at("render_frame_rate_limit") == 0);
    CHECK(json.at("semantics").at("process_rss_limit_enforced") == false);
    CHECK(json.at("semantics").at("render_frame_rate_zero") == "uncapped");
    CHECK(json.at("semantics").at("presentation_preparation_zero") == "no discretionary preparation");
    CHECK(json.at("semantics").at("presentation_preparation_scope") == "required work excluded");
    CHECK(json.at("resource_sources").at("presentation_preparation_budget_us") == "explicit_override");
}
TEST_CASE("Uint64 byte budgets survive JSON text roundtrip without signed or float conversion")
{
    ExecutionRequest request; request.planningMemoryBytes = std::numeric_limits<std::uint64_t>::max();
    const auto json = describeResourcePlan(resolveResourcePlan(request, jsonDevice(), jsonWorkload()));
    const auto reparsed = nlohmann::json::parse(json.dump());
    CHECK(reparsed.at("resolved_resources").at("planning_memory_budget_bytes").is_number_unsigned());
    CHECK(reparsed.at("resolved_resources").at("planning_memory_budget_bytes").get<std::uint64_t>() == *request.planningMemoryBytes);
    CHECK(reparsed.at("original_request").at("resource_overrides").at("planning_memory_budget_bytes").get<std::uint64_t>() == *request.planningMemoryBytes);
}
TEST_CASE("serializer reads the frozen plan rather than changed callers and is deterministic")
{
    ExecutionRequest request; request.savedProfile = ExecutionProfile::Balanced;
    auto device = jsonDevice(); auto workload = jsonWorkload();
    const auto plan = resolveResourcePlan(request, device, workload); const auto originalPlan = plan;
    const auto expected = describeResourcePlan(plan);
    request.savedProfile = ExecutionProfile::FastSimulation; device.allocatedCpuParticipants = 1; workload.requiredLiveBytes = 0;
    CHECK(describeResourcePlan(plan) == expected); CHECK(plan == originalPlan);
    CHECK(expected.at("profile_source") == "saved_preference");
    CHECK(expected.at("workload").at("required_live_bytes") == 512);
    CHECK(expected.at("workload").at("validated_defaults").at("compute_participants") == 6);
    CHECK(expected.at("original_request").at("explicit_execution_profile").is_null());
}
TEST_CASE("unsupported policy version cannot be disguised as schema version one")
{
    auto plan = resolveResourcePlan({}, jsonDevice(), jsonWorkload()); ++plan.version;
    CHECK_THROWS_AS(describeResourcePlan(plan), std::invalid_argument);
}
TEST_CASE("battery saver null false true is independent of battery power in both snapshots")
{
    ExecutionRequest request; request.constraints.onBattery = true;
    auto json = describeResourcePlan(resolveResourcePlan(request, jsonDevice(), jsonWorkload()));
    CHECK(json.at("runtime_constraints").at("battery_saver").is_null());
    CHECK(json.at("original_request").at("runtime_constraints").at("battery_saver").is_null());
    for (bool saver : {false, true})
    {
        request.constraints.batterySaver = saver;
        json = describeResourcePlan(resolveResourcePlan(request, jsonDevice(), jsonWorkload()));
        CHECK(json.at("runtime_constraints").at("on_battery") == true);
        CHECK(json.at("runtime_constraints").at("battery_saver").is_boolean());
        CHECK(json.at("runtime_constraints").at("battery_saver") == saver);
        CHECK(json.at("original_request").at("runtime_constraints").at("battery_saver") == saver);
    }
}

TEST_CASE("workload support is distinct from device allocation in serialized plans")
{
    auto requirements = jsonWorkload(); requirements.maximumComputeParticipants = 1;
    const auto value = describeResourcePlan(resolveResourcePlan({}, jsonDevice(), requirements));
    CHECK(value["workload"]["maximum_compute_participants"] == 1);
    CHECK(value["device_capabilities"]["allocated_cpu_participants"] == 6);
    CHECK(value["device_capabilities"]["threads_supported"] == true);
    CHECK(value["resolved_resources"]["compute_participants"] == 1);
    requirements.maximumComputeParticipants.reset();
    CHECK(describeResourcePlan(resolveResourcePlan({}, jsonDevice(), requirements))["workload"]["maximum_compute_participants"].is_null());
}

}
