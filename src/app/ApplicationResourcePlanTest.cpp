// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ApplicationResourcePlan.h"
#include <ThreadSupport.h>
#include <limits>
#include <sstream>
#include <string>

TEST_SUITE("ApplicationResourcePlan")
{
TEST_CASE("auto preserves hardware sizing while reporting CPU eligibility")
{
    const auto observed = resolveApplicationResourcePlan(0, cpuAvailabilityObservation(32, 6), true, {});
    CHECK(observed.computeParticipants == defaultComputeThreadCount(32));
    CHECK(observed.cpuAvailability.eligibleLogicalCpuCount == 6);
    CHECK(std::string(computeParticipantSourceName(observed)) == "hardware-hint");
    CHECK(observed.exceedsCpuAllocation);

    const auto hinted = resolveApplicationResourcePlan(0, cpuAvailabilityObservation(32), true, {});
    CHECK(hinted.computeParticipants == defaultComputeThreadCount(32));
    CHECK_FALSE(hinted.cpuAvailability.eligibleLogicalCpuCount.has_value());
    CHECK(std::string(computeParticipantSourceName(hinted)) == "hardware-hint");
    CHECK_FALSE(hinted.exceedsCpuAllocation);

    const auto maskOnly = resolveApplicationResourcePlan(0, cpuAvailabilityObservation(0, 4), true, {});
    CHECK(maskOnly.computeParticipants == 1);
    CHECK(maskOnly.cpuAvailability.eligibleLogicalCpuCount == 4);
    CHECK(std::string(computeParticipantSourceName(maskOnly)) == "serial-fallback");
    const auto unknown = resolveApplicationResourcePlan(0, cpuAvailabilityObservation(0), true, {});
    CHECK(unknown.computeParticipants == 1);
    CHECK(std::string(computeParticipantSourceName(unknown)) == "serial-fallback");
}
TEST_CASE("explicit counts preserve owner-inclusive overrides and report known oversubscription")
{
    for (const unsigned requested : {1u, 6u, 12u, std::numeric_limits<unsigned>::max()})
    {
        const auto plan = resolveApplicationResourcePlan(requested, cpuAvailabilityObservation(32, 6), true, {});
        CHECK(plan.requestedComputeThreads == requested);
        CHECK(plan.computeParticipants == requested);
        CHECK(plan.exceedsCpuAllocation == (requested > 6));
        CHECK(std::string(computeParticipantSourceName(plan)) == "explicit-override");
    }
    const auto unknown = resolveApplicationResourcePlan(12, cpuAvailabilityObservation(4), true, {});
    CHECK(unknown.computeParticipants == 12);
    CHECK_FALSE(unknown.exceedsCpuAllocation);
}
TEST_CASE("threading unavailable forces serial while preserving the requested count")
{
    for (const unsigned requested : {0u, 12u})
    {
        const auto plan = resolveApplicationResourcePlan(requested, cpuAvailabilityObservation(32, 6), false, {});
        CHECK(plan.requestedComputeThreads == requested);
        CHECK(plan.computeParticipants == 1);
        CHECK_FALSE(plan.exceedsCpuAllocation);
        CHECK(std::string(computeParticipantSourceName(plan)) == "threads-unavailable");
    }
}
TEST_CASE("resource setup preserves every resolved gradient configuration without backend admission")
{
    using namespace gradient_kernel;
    for (const auto backend : {Backend::CPU, Backend::Scalar, Backend::SSE2, Backend::NEON,
                               Backend::OpenCL, Backend::Vulkan, Backend::WebGPU})
    {
        const Configuration config{backend, isGPU(backend) ? Algorithm::Frozen8 : Algorithm::Buckets};
        const auto plan = resolveApplicationResourcePlan(2, cpuAvailabilityObservation(8), true, config);
        CHECK(plan.gradient == config);
        CHECK(plan.computeGpuPermitted() == isGPU(backend));
        CHECK(plan.computeParticipants == 2);
    }
    const Configuration jacobi{Backend::Vulkan, Algorithm::Jacobi};
    const auto serial = resolveApplicationResourcePlan(1, {}, true, jacobi);
    CHECK(serial.gradient == jacobi);
    CHECK_FALSE(serial.computeGpuPermitted());
    const auto unthreaded = resolveApplicationResourcePlan(8, {}, false, jacobi);
    CHECK(unthreaded.gradient == jacobi);
    CHECK_FALSE(unthreaded.computeGpuPermitted());
}
TEST_CASE("setup provenance records automatic sizing separately from CPU eligibility")
{
    auto plan = resolveApplicationResourcePlan(0, cpuAvailabilityObservation(32, 6), true, {});
    std::ostringstream allocated;
    writeApplicationResourcePlanJson(allocated, plan);
    CHECK(allocated.str().find("\"eligible_threads\":6") != std::string::npos);
    CHECK(allocated.str().find("\"exceeds_cpu_allocation\":true") != std::string::npos);
    CHECK(allocated.str().find("\"participant_source\":\"hardware-hint\"") != std::string::npos);
    CHECK(allocated.str().find("\"cpu_availability_source\":\"affinity-mask\"") != std::string::npos);
    plan = resolveApplicationResourcePlan(0, cpuAvailabilityObservation(32), true, {});
    std::ostringstream unknown;
    writeApplicationResourcePlanJson(unknown, plan);
    CHECK(unknown.str().find("\"eligible_threads\":null") != std::string::npos);
    CHECK(unknown.str().find("\"cpu_availability_source\":\"hardware-hint-only\"") != std::string::npos);
}
TEST_CASE("platform discovery produces a usable setup with the platform threading capability")
{
    const auto plan = discoverApplicationResourcePlan(2, {});
    CHECK(plan.threadsSupported == GAGCore::ThreadSupport::available);
    CHECK(plan.computeParticipants == (GAGCore::ThreadSupport::available ? 2u : 1u));
    CHECK(plan.requestedComputeThreads == 2);
    if (plan.cpuAvailability.eligibleLogicalCpuCount)
    {
        CHECK(*plan.cpuAvailability.eligibleLogicalCpuCount > 0);
        CHECK(std::string(cpuAvailabilitySourceName(plan.cpuAvailability)) == "affinity-mask");
    }
}
}
