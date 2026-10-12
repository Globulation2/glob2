// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ResourcePolicy.h"
#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>

using namespace resource_policy;
namespace
{
DeviceCapabilities supported()
{
    DeviceCapabilities device;
    device.allocatedCpuParticipants = 6; device.threadsSupported = true;
    device.renderGpuSupported = true; device.computeGpuSupported = true;
    return device;
}
WorkloadRequirements workload()
{
    WorkloadRequirements value;
    value.defaults.computeParticipants = 6;
    value.defaults.planningMemoryBytes = 1024;
    value.defaults.recoverableMemoryBytes = 256;
    value.defaults.render = {60, 1, 2, 3};
    value.defaults.computeGpu = GpuPolicy::Automatic;
    value.requiredLiveBytes = 512;
    return value;
}
bool diagnostic(const ResourcePlan &plan, Diagnostic value)
{
    return std::find(plan.diagnostics.begin(), plan.diagnostics.end(), value) != plan.diagnostics.end();
}
}
TEST_SUITE("ResourcePolicy")
{
TEST_CASE("six profile names preserve intent without claiming tier tuning")
{
    const std::array<std::string_view, 6> names{"auto", "eco", "balanced", "luxurious", "fast-simulation", "efficient-throughput"};
    for (const auto name : names)
    {
        const auto profile = parseExecutionProfile(name);
        CHECK(executionProfileName(profile) == name);
        ExecutionRequest request; request.explicitProfile = profile;
        const auto plan = resolveResourcePlan(request, supported(), workload());
        CHECK(plan.version == ResourcePlan::Version);
        CHECK(plan.requestedProfile == profile); CHECK(plan.selectedProfile == profile);
        CHECK(plan.profileTuningPending);
        CHECK(plan.resources == workload().defaults);
    }
    for (const auto name : {"", "AUTO", "fast", "eco ", "balanced\n"})
        CHECK_THROWS_AS(parseExecutionProfile(name), std::invalid_argument);
    CHECK_THROWS_AS(executionProfileName(static_cast<ExecutionProfile>(99)), std::invalid_argument);
}
TEST_CASE("profile precedence includes an explicit auto and records each source")
{
    ExecutionRequest request; request.contextDefault = ExecutionProfile::Balanced;
    auto plan = resolveResourcePlan(request, supported(), workload());
    CHECK(plan.selectedProfile == ExecutionProfile::Balanced); CHECK(plan.profileSource == ProfileSource::ContextDefault);
    request.savedProfile = ExecutionProfile::Eco;
    plan = resolveResourcePlan(request, supported(), workload());
    CHECK(plan.selectedProfile == ExecutionProfile::Eco); CHECK(plan.profileSource == ProfileSource::SavedPreference);
    request.explicitProfile = ExecutionProfile::Auto;
    plan = resolveResourcePlan(request, supported(), workload());
    CHECK(plan.selectedProfile == ExecutionProfile::Auto); CHECK(plan.profileSource == ProfileSource::ExplicitProfile);
}
TEST_CASE("explicit resource overrides win while all original inputs remain frozen")
{
    ExecutionRequest request; request.explicitProfile = ExecutionProfile::Eco;
    request.computeParticipants = 12; request.renderFrameRate = 30; request.renderResolutionScale = .75;
    request.renderDetail = 1; request.renderPreparationBudgetUs = 2;
    request.renderGpu = GpuPolicy::Disabled; request.computeGpu = GpuPolicy::Enabled;
    request.planningMemoryBytes = 900; request.recoverableMemoryBytes = 200;
    request.constraints.onBattery = true; request.constraints.backgrounded = false;
    const auto original = request;
    const auto plan = resolveResourcePlan(request, supported(), workload());
    CHECK(request == original); CHECK(plan.request == original);
    CHECK(plan.resources.computeParticipants == 12); CHECK(diagnostic(plan, Diagnostic::ExplicitCpuOversubscription));
    CHECK((plan.resources.render == RenderResources{30, .75, 1, 2}));
    CHECK(plan.resources.computeGpu == GpuPolicy::Enabled); CHECK(plan.resources.renderGpu == GpuPolicy::Disabled);
    CHECK(plan.resources.planningMemoryBytes == 900); CHECK(plan.resources.recoverableMemoryBytes == 200);
    CHECK(plan.participantSource == ValueSource::ExplicitOverride); CHECK(plan.resolutionSource == ValueSource::ExplicitOverride);
    CHECK(plan.computeGpuSource == ValueSource::ExplicitOverride); CHECK(plan.recoverableMemorySource == ValueSource::ExplicitOverride);
    request.computeParticipants = 1; request.constraints.onBattery = false;
    CHECK(plan.request == original);
}
TEST_CASE("known allocation bounds automatic participants but is not an explicit hard cap")
{
    auto device = supported(); auto requirements = workload(); requirements.defaults.computeParticipants = 24;
    CHECK(resolveResourcePlan({}, device, requirements).resources.computeParticipants == 6);
    ExecutionRequest request; request.computeParticipants = 12;
    CHECK(resolveResourcePlan(request, device, requirements).resources.computeParticipants == 12);
    device.hardParticipantLimit = 8;
    CHECK_THROWS_AS(resolveResourcePlan(request, device, requirements), std::invalid_argument);
    CHECK(resolveResourcePlan({}, device, requirements).resources.computeParticipants == 6);
    device.threadsSupported = false;
    CHECK(resolveResourcePlan({}, device, requirements).resources.computeParticipants == 1);
    request.computeParticipants = 2;
    CHECK_THROWS_AS(resolveResourcePlan(request, device, requirements), std::invalid_argument);
}
TEST_CASE("unknown CPU and threading use serial without guessing an allocation")
{
    DeviceCapabilities device;
    auto plan = resolveResourcePlan({}, device, workload());
    CHECK(plan.resources.computeParticipants == 1); CHECK(diagnostic(plan, Diagnostic::UnknownCpuUsesSerial));
    CHECK_FALSE(plan.capabilities.allocatedCpuParticipants.has_value());
    device.threadsSupported = true;
    CHECK(resolveResourcePlan({}, device, workload()).resources.computeParticipants == 1);
    device.allocatedCpuParticipants = 6; device.threadsSupported.reset();
    CHECK(resolveResourcePlan({}, device, workload()).resources.computeParticipants == 1);
}
TEST_CASE("GPU request and capability are separate from guaranteed device execution")
{
    ExecutionRequest request; request.computeGpu = GpuPolicy::Enabled; request.renderGpu = GpuPolicy::Enabled;
    auto device = supported(); device.computeGpuSupported = false;
    auto plan = resolveResourcePlan(request, device, workload());
    CHECK(plan.resources.computeGpu == GpuPolicy::Disabled); CHECK(plan.resources.renderGpu == GpuPolicy::Enabled);
    CHECK(plan.request.computeGpu == GpuPolicy::Enabled); CHECK(plan.computeGpuSource == ValueSource::ExplicitOverride);
    CHECK(diagnostic(plan, Diagnostic::ComputeGpuUnavailable)); CHECK_FALSE(diagnostic(plan, Diagnostic::RenderGpuUnavailable));
    device.renderGpuSupported = false;
    CHECK(diagnostic(resolveResourcePlan(request, device, workload()), Diagnostic::RenderGpuUnavailable));
    device.computeGpuSupported.reset();
    CHECK(resolveResourcePlan(request, device, workload()).resources.computeGpu == GpuPolicy::Enabled);
}
TEST_CASE("memory constraints reserve live state before recoverable retention")
{
    auto device = supported(); device.allocatedMemoryBytes = 700;
    auto plan = resolveResourcePlan({}, device, workload());
    CHECK(plan.resources.planningMemoryBytes == 700); CHECK(plan.resources.recoverableMemoryBytes == 188);
    CHECK(plan.workload.requiredLiveBytes == 512); // Never trim live simulation state to fit a profile.
    ExecutionRequest request; request.constraints.memoryLimitBytes = 511;
    CHECK_THROWS_AS(resolveResourcePlan(request, device, workload()), std::invalid_argument);
    request.constraints.memoryLimitBytes = 600;
    CHECK(resolveResourcePlan(request, device, workload()).resources.recoverableMemoryBytes == 88);
    request.recoverableMemoryBytes = 89;
    CHECK_THROWS_AS(resolveResourcePlan(request, device, workload()), std::invalid_argument);
    request.recoverableMemoryBytes = 0;
    CHECK(resolveResourcePlan(request, device, workload()).resources.recoverableMemoryBytes == 0);
    request.planningMemoryBytes = 701;
    CHECK_THROWS_AS(resolveResourcePlan(request, device, workload()), std::invalid_argument);
}
TEST_CASE("invalid requests and validated rendering bounds reject clearly")
{
    ExecutionRequest request; request.computeParticipants = 0;
    CHECK_THROWS_AS(resolveResourcePlan(request, supported(), workload()), std::invalid_argument);
    request.computeParticipants.reset();
    for (const auto scale : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        request.renderResolutionScale = scale;
        CHECK_THROWS_AS(resolveResourcePlan(request, supported(), workload()), std::invalid_argument);
    }
    request.renderResolutionScale = 2;
    auto requirements = workload(); requirements.maximumResolutionScale = 1;
    CHECK_THROWS_AS(resolveResourcePlan(request, supported(), requirements), std::invalid_argument);
    request.renderResolutionScale.reset(); request.renderFrameRate = 0; requirements.maximumFrameRate = 120;
    CHECK_THROWS_AS(resolveResourcePlan(request, supported(), requirements), std::invalid_argument);
    request.renderFrameRate = 121;
    CHECK_THROWS_AS(resolveResourcePlan(request, supported(), requirements), std::invalid_argument);
    request.renderFrameRate = 120;
    CHECK(resolveResourcePlan(request, supported(), requirements).resources.render.frameRate == 120);
    request.computeGpu = static_cast<GpuPolicy>(99);
    CHECK_THROWS_AS(resolveResourcePlan(request, supported(), requirements), std::invalid_argument);
}
TEST_CASE("repeated resolutions preserve capability and constraint provenance deterministically")
{
    ExecutionRequest request; request.savedProfile = ExecutionProfile::EfficientThroughput;
    request.constraints.backgrounded = true; request.constraints.memoryPressure = true; request.constraints.batterySaver = false;
    auto device = supported(); auto requirements = workload();
    const auto expected = resolveResourcePlan(request, device, requirements);
    for (unsigned i = 0; i < 32; ++i)
        CHECK(resolveResourcePlan(request, device, requirements) == expected);
    CHECK(expected.capabilities == device); CHECK(expected.workload == requirements);
    CHECK(expected.request.constraints.backgrounded == true); CHECK(expected.request.constraints.memoryPressure == true);
    CHECK_FALSE(expected.request.constraints.onBattery.has_value());
    CHECK(expected.request.constraints.batterySaver == false);
}
TEST_CASE("memory arithmetic preserves zero headroom and Uint64 live requirements")
{
    auto requirements = workload(); auto device = supported();
    requirements.defaults.planningMemoryBytes = requirements.requiredLiveBytes;
    CHECK(resolveResourcePlan({}, device, requirements).resources.recoverableMemoryBytes == 0);
    requirements.requiredLiveBytes = std::numeric_limits<std::uint64_t>::max();
    requirements.defaults.planningMemoryBytes = requirements.requiredLiveBytes;
    device.allocatedMemoryBytes = requirements.requiredLiveBytes;
    const auto plan = resolveResourcePlan({}, device, requirements);
    CHECK(plan.resources.planningMemoryBytes == requirements.requiredLiveBytes);
    CHECK(plan.resources.recoverableMemoryBytes == 0);
    ExecutionRequest request; request.recoverableMemoryBytes = 1;
    CHECK_THROWS_AS(resolveResourcePlan(request, device, requirements), std::invalid_argument);
}
TEST_CASE("a caller supplied browser automatic default is not an explicit hard limit")
{
    auto requirements = workload(); auto device = supported();
    device.allocatedCpuParticipants = 32; requirements.defaults.computeParticipants = 8;
    CHECK(resolveResourcePlan({}, device, requirements).resources.computeParticipants == 8);
    ExecutionRequest request; request.computeParticipants = 12;
    CHECK(resolveResourcePlan(request, device, requirements).resources.computeParticipants == 12);
    device.threadsSupported = false;
    CHECK(resolveResourcePlan({}, device, requirements).resources.computeParticipants == 1);
    CHECK_THROWS_AS(resolveResourcePlan(request, device, requirements), std::invalid_argument);
}

TEST_CASE("battery use and battery saver are independent observed constraints without tuning")
{
    ExecutionRequest request;
    request.constraints.onBattery = true;
    const auto ordinaryBattery = resolveResourcePlan(request, supported(), workload());
    CHECK(ordinaryBattery.request.constraints.onBattery == true);
    CHECK_FALSE(ordinaryBattery.request.constraints.batterySaver.has_value());
    request.constraints.batterySaver = true;
    const auto saver = resolveResourcePlan(request, supported(), workload());
    CHECK(saver.request.constraints.batterySaver == true);
    CHECK(saver.resources == ordinaryBattery.resources); // Constraint observed, tuning remains pending.
    request.constraints.onBattery = false;
    const auto pluggedInSaver = resolveResourcePlan(request, supported(), workload());
    CHECK(pluggedInSaver.request.constraints.onBattery == false);
    CHECK(pluggedInSaver.request.constraints.batterySaver == true);
    CHECK(pluggedInSaver.resources == ordinaryBattery.resources);
}

TEST_CASE("workload participant bounds preserve device capabilities and reject unsupported overrides")
{
    auto requirements = workload(); requirements.maximumComputeParticipants = 1;
    const auto device = supported(); ExecutionRequest request;
    const auto plan = resolveResourcePlan(request, device, requirements);
    CHECK(plan.resources.computeParticipants == 1);
    CHECK(plan.capabilities == device); CHECK(plan.workload.maximumComputeParticipants == 1);
    request.computeParticipants = 2;
    CHECK_THROWS_AS(resolveResourcePlan(request, device, requirements), std::invalid_argument);
    request.computeParticipants = 1;
    CHECK(resolveResourcePlan(request, device, requirements).resources.computeParticipants == 1);
    requirements.maximumComputeParticipants = 0;
    CHECK_THROWS_AS(resolveResourcePlan(request, device, requirements), std::invalid_argument);
    requirements.maximumComputeParticipants = 3; request.computeParticipants.reset();
    CHECK(resolveResourcePlan(request, device, requirements).resources.computeParticipants == 3);
    requirements.maximumComputeParticipants.reset();
    CHECK(resolveResourcePlan(request, device, requirements).resources.computeParticipants == 6);
}

TEST_CASE("explicit resources and constraints survive every profile precedence stratum")
{
    const std::array<ExecutionProfile,6> profiles{ExecutionProfile::Auto,ExecutionProfile::Eco,
        ExecutionProfile::Balanced,ExecutionProfile::Luxurious,ExecutionProfile::FastSimulation,ExecutionProfile::EfficientThroughput};
    for(const auto profile:profiles) {
        ExecutionRequest request; request.contextDefault=ExecutionProfile::FastSimulation;
        request.savedProfile=ExecutionProfile::Luxurious; request.explicitProfile=profile;
        request.computeParticipants=3; request.planningMemoryBytes=700; request.recoverableMemoryBytes=100;
        request.renderFrameRate=0; request.renderResolutionScale=.75; request.renderDetail=1; request.renderPreparationBudgetUs=0;
        request.renderGpu=GpuPolicy::Disabled; request.computeGpu=GpuPolicy::Enabled;
        request.constraints.onBattery=false; request.constraints.batterySaver=true;
        request.constraints.backgrounded=true; request.constraints.memoryPressure=true; request.constraints.memoryLimitBytes=768;
        const auto plan=resolveResourcePlan(request,supported(),workload());
        CHECK(plan.selectedProfile==profile); CHECK(plan.profileSource==ProfileSource::ExplicitProfile); CHECK(plan.profileTuningPending);
        CHECK(plan.resources.computeParticipants==3); CHECK(plan.resources.planningMemoryBytes==700); CHECK(plan.resources.recoverableMemoryBytes==100);
        CHECK((plan.resources.render==RenderResources{0,.75,1,0}));
        CHECK(plan.resources.renderGpu==GpuPolicy::Disabled); CHECK(plan.resources.computeGpu==GpuPolicy::Enabled);
        CHECK(plan.request.constraints==request.constraints);
        CHECK(plan.participantSource==ValueSource::ExplicitOverride); CHECK(plan.frameRateSource==ValueSource::ExplicitOverride);
        request.explicitProfile.reset(); const auto saved=resolveResourcePlan(request,supported(),workload());
        CHECK(saved.selectedProfile==ExecutionProfile::Luxurious); CHECK(saved.resources==plan.resources);
        request.savedProfile.reset(); const auto context=resolveResourcePlan(request,supported(),workload());
        CHECK(context.selectedProfile==ExecutionProfile::FastSimulation); CHECK(context.resources==plan.resources);
    }
}
TEST_CASE("invalid capability defaults and enum overrides fail while unknown support stays distinct")
{
    auto request=ExecutionRequest{}; auto device=supported(); auto requirements=workload();
    device.allocatedCpuParticipants=0; CHECK_THROWS_AS(resolveResourcePlan(request,device,requirements),std::invalid_argument);
    device=supported(); device.hardParticipantLimit=0; CHECK_THROWS_AS(resolveResourcePlan(request,device,requirements),std::invalid_argument);
    device=supported(); requirements.defaults.computeParticipants=0; CHECK_THROWS_AS(resolveResourcePlan(request,device,requirements),std::invalid_argument);
    requirements=workload(); request.explicitProfile=static_cast<ExecutionProfile>(99);
    CHECK_THROWS_AS(resolveResourcePlan(request,device,requirements),std::invalid_argument);
    request.explicitProfile=ExecutionProfile::Auto; request.savedProfile=static_cast<ExecutionProfile>(99);
    CHECK_THROWS_AS(resolveResourcePlan(request,device,requirements),std::invalid_argument);
    request.explicitProfile.reset(); request.savedProfile.reset();
    requirements.maximumRenderDetail=0; request.renderDetail=1;
    CHECK_THROWS_AS(resolveResourcePlan(request,device,requirements),std::invalid_argument);
    request.renderDetail=0; requirements.maximumRenderPreparationBudgetUs=0; request.renderPreparationBudgetUs=1;
    CHECK_THROWS_AS(resolveResourcePlan(request,device,requirements),std::invalid_argument);
    request.renderPreparationBudgetUs=0;
    CHECK(resolveResourcePlan(request,device,requirements).resources.render.preparationBudgetUs==0);
    request.computeParticipants=2; device.threadsSupported.reset();
    CHECK(resolveResourcePlan(request,device,requirements).resources.computeParticipants==2);
    device.threadsSupported=false;
    CHECK_THROWS_AS(resolveResourcePlan(request,device,requirements),std::invalid_argument);
}

}
