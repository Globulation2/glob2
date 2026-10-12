// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ApplicationResourcePlan.h"
#include <stdexcept>

using namespace resource_policy;
namespace
{
ApplicationResourceInputs application()
{
    ApplicationResourceInputs input;
    input.capabilities.allocatedCpuParticipants = 6;
    input.capabilities.threadsSupported = true;
    input.currentPresentation = {60, 1, 0, 0};
    return input;
}
}
TEST_SUITE("ApplicationResourcePlan")
{
TEST_CASE("planning byte parser rejects partial signed and unsupported requests")
{
    CHECK(parsePlanningMemoryBytes("1") == 1);
    CHECK(parsePlanningMemoryBytes("9223372036854775807") == 9223372036854775807ULL);
    for (const auto text : {"", "0", "-1", "+1", " 1", "1 ", "1MiB", "9223372036854775808", "18446744073709551616"})
        CHECK_THROWS_AS(parsePlanningMemoryBytes(text), std::invalid_argument);
}
TEST_CASE("application context and saved intent do not tune current defaults")
{
    auto input = application();
    auto plan = resolveApplicationResourcePlan(input);
    CHECK(plan.selectedProfile == ExecutionProfile::Auto);
    CHECK(plan.profileSource == ProfileSource::ContextDefault);
    CHECK(plan.resources.computeParticipants == 6);
    CHECK(plan.resources.render == input.currentPresentation);
    CHECK(plan.resources.computeGpu == GpuPolicy::Disabled);
    CHECK_FALSE(plan.resources.planningMemoryBytes.has_value());
    input.headless = true; input.legacyGpuComputeEnabled = true;
    plan = resolveApplicationResourcePlan(input);
    CHECK(plan.selectedProfile == ExecutionProfile::FastSimulation);
    CHECK(plan.resources.computeGpu == GpuPolicy::Enabled);
    input.savedProfile = ExecutionProfile::Eco;
    plan = resolveApplicationResourcePlan(input);
    CHECK(plan.selectedProfile == ExecutionProfile::Eco); CHECK(plan.profileSource == ProfileSource::SavedPreference);
    CHECK(plan.resources.computeParticipants == 6); CHECK(plan.profileTuningPending);
    input.request.explicitProfile = ExecutionProfile::Auto;
    plan = resolveApplicationResourcePlan(input);
    CHECK(plan.selectedProfile == ExecutionProfile::Auto); CHECK(plan.profileSource == ProfileSource::ExplicitProfile);
}
TEST_CASE("legacy positive CPU override and GPU override retain explicit provenance")
{
    auto input = application(); input.legacyRequestedComputeParticipants = 12;
    input.legacyGpuComputeEnabled = true; input.legacyGpuComputeExplicit = true;
    const auto original = input.request;
    const auto plan = resolveApplicationResourcePlan(input);
    CHECK(plan.resources.computeParticipants == 12); CHECK(plan.request.computeParticipants == 12);
    CHECK(plan.participantSource == ValueSource::ExplicitOverride);
    CHECK(plan.resources.computeGpu == GpuPolicy::Enabled); CHECK(plan.request.computeGpu == GpuPolicy::Enabled);
    CHECK(plan.computeGpuSource == ValueSource::ExplicitOverride);
    CHECK(input.request == original);
}
TEST_CASE("serial platform enforces support while a threaded allocation remains a soft override bound")
{
    auto input = application(); input.legacyRequestedComputeParticipants = 12;
    CHECK(resolveApplicationResourcePlan(input).resources.computeParticipants == 12);
    input.capabilities.threadsSupported = false; input.legacyRequestedComputeParticipants = 0;
    CHECK(resolveApplicationResourcePlan(input).resources.computeParticipants == 1);
    input.legacyRequestedComputeParticipants = 2;
    CHECK_THROWS_AS(resolveApplicationResourcePlan(input), std::invalid_argument);
}
TEST_CASE("unknown allocation stays conservative without using the test host")
{
    auto input = application(); input.capabilities.allocatedCpuParticipants.reset();
    const auto plan = resolveApplicationResourcePlan(input);
    CHECK(plan.resources.computeParticipants == 1);
    CHECK_FALSE(plan.capabilities.allocatedCpuParticipants.has_value());
    CHECK_FALSE(plan.capabilities.allocatedMemoryBytes.has_value());
    CHECK_FALSE(plan.capabilities.computeGpuSupported.has_value());
}
TEST_CASE("GPU offload permission preserves exact fallback on an unsupported platform")
{
    auto input = application(); input.legacyGpuComputeEnabled = true; input.legacyGpuComputeExplicit = true;
    input.capabilities.computeGpuSupported = false;
    const auto plan = resolveApplicationResourcePlan(input);
    CHECK(plan.request.computeGpu == GpuPolicy::Enabled);
    CHECK(plan.resources.computeGpu == GpuPolicy::Disabled);
    CHECK(plan.resources.renderGpu == GpuPolicy::Automatic);
}
TEST_CASE("planning requests and runtime constraints are snapshots with no guessed live census")
{
    auto input = application(); input.request.planningMemoryBytes = 4096;
    input.request.constraints.onBattery = true; input.request.constraints.batterySaver = false;
    input.request.constraints.backgrounded = true; input.request.constraints.memoryPressure = true;
    const auto plan = resolveApplicationResourcePlan(input);
    CHECK(plan.resources.planningMemoryBytes == 4096); CHECK(plan.resources.recoverableMemoryBytes == 0);
    CHECK(plan.workload.requiredLiveBytes == 0); CHECK_FALSE(plan.capabilities.allocatedMemoryBytes.has_value());
    CHECK(plan.request.constraints == input.request.constraints);
    input.request.constraints.backgrounded = false;
    CHECK(plan.request.constraints.backgrounded == true);
    CHECK(plan.resources.render == input.currentPresentation);
}
TEST_CASE("explicit refresh inputs resolve repeatably without implicit updates")
{
    auto input = application(); input.savedProfile = ExecutionProfile::Balanced;
    const auto frozen = resolveApplicationResourcePlan(input);
    CHECK(resolveApplicationResourcePlan(input) == frozen);
    input.capabilities.allocatedCpuParticipants = 3;
    CHECK(frozen.resources.computeParticipants == 6);
    CHECK(resolveApplicationResourcePlan(input).resources.computeParticipants == 3);
}
TEST_CASE("owner only workloads retain actual allocation and threading support")
{
    auto input = application(); input.ownerOnly = true;
    const auto plan = resolveApplicationResourcePlan(input);
    CHECK(plan.resources.computeParticipants == 1);
    CHECK(plan.capabilities.allocatedCpuParticipants == 6);
    CHECK(plan.capabilities.threadsSupported == true);
    CHECK_FALSE(plan.capabilities.hardParticipantLimit.has_value());
    CHECK(plan.workload.maximumComputeParticipants == 1);
    input.legacyRequestedComputeParticipants = 2;
    CHECK_THROWS_AS(resolveApplicationResourcePlan(input), std::invalid_argument);
    input.legacyRequestedComputeParticipants = 1;
    CHECK(resolveApplicationResourcePlan(input).resources.computeParticipants == 1);
    input.ownerOnly = false; input.legacyRequestedComputeParticipants = 0;
    CHECK(resolveApplicationResourcePlan(input).resources.computeParticipants == 6);
}

}
