// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include "ApplicationResourcePlan.h"
#include <ThreadSupport.h>
#include <limits>
#include <sstream>
#include <string>

TEST_SUITE("ApplicationResourcePlan")
{
TEST_CASE("auto uses eligible logical fallback when physical topology is unknown")
{
    const auto observed = resolveApplicationResourcePlan(0, cpuAvailabilityObservation(32, 6), true, {});
    CHECK(observed.cpuAvailability.eligibleLogicalCpuCount == 6);
#ifdef __EMSCRIPTEN__
    CHECK(observed.computeParticipants == 8);
    CHECK(std::string(computeParticipantSourceName(observed)) == "hardware-hint");
    CHECK(observed.exceedsCpuAllocation);
#else
    CHECK(observed.computeParticipants == 6);
    CHECK(std::string(computeParticipantSourceName(observed)) == "eligible-logical-fallback");
    CHECK_FALSE(observed.exceedsCpuAllocation);
#endif

    const auto hinted = resolveApplicationResourcePlan(0, cpuAvailabilityObservation(32), true, {});
    CHECK(hinted.computeParticipants == defaultComputeThreadCount(32));
    CHECK_FALSE(hinted.cpuAvailability.eligibleLogicalCpuCount.has_value());
    CHECK(std::string(computeParticipantSourceName(hinted)) == "hardware-hint");
    CHECK_FALSE(hinted.exceedsCpuAllocation);

    const auto maskOnly = resolveApplicationResourcePlan(0, cpuAvailabilityObservation(0, 4), true, {});
    CHECK(maskOnly.cpuAvailability.eligibleLogicalCpuCount == 4);
#ifdef __EMSCRIPTEN__
    CHECK(maskOnly.computeParticipants == 1);
    CHECK(std::string(computeParticipantSourceName(maskOnly)) == "serial-fallback");
#else
    CHECK(maskOnly.computeParticipants == 4);
    CHECK(std::string(computeParticipantSourceName(maskOnly)) == "eligible-logical-fallback");
#endif
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
    CHECK(allocated.str().find(std::string("\"participant_source\":\"") + computeParticipantSourceName(plan) + "\"") != std::string::npos);
    CHECK(allocated.str().find("\"physical_cores\":null") != std::string::npos);
    CHECK(allocated.str().find("\"physical_core_source\":\"unknown\"") != std::string::npos);
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

TEST_CASE("physical cores size native auto and preserve explicit overrides")
{
    for (unsigned logical : {8u, 16u, 24u}) {
        auto cpu = cpuAvailabilityObservation(logical, logical);
        cpu.physicalCoreCount = 8;
        cpu.physicalCoreSource = CpuAvailabilityObservation::PhysicalSource::LinuxTopology;
        cpu.physicalCoresAreEligible = true;
        const auto plan = resolveApplicationResourcePlan(0, cpu, true, {});
#ifdef __EMSCRIPTEN__
        CHECK(plan.computeParticipants == defaultComputeThreadCount(logical));
#else
        CHECK(plan.computeParticipants == 8);
        CHECK(std::string(computeParticipantSourceName(plan)) == "physical-cores");
#endif
        CHECK(resolveApplicationResourcePlan(20, cpu, true, {}).computeParticipants == 20);
        CHECK(resolveApplicationResourcePlan(0, cpu, false, {}).computeParticipants == 1);
        std::ostringstream json;
        writeApplicationResourcePlanJson(json, plan);
        CHECK(json.str().find("\"physical_cores\":8") != std::string::npos);
        CHECK(json.str().find("\"physical_core_source\":\"sysfs-affinity\"") != std::string::npos);
    }
    auto system = cpuAvailabilityObservation(16, 3);
    system.physicalCoreCount = 8;
    system.physicalCoreSource = CpuAvailabilityObservation::PhysicalSource::Sysctl;
#ifndef __EMSCRIPTEN__
    CHECK(resolveComputeThreadCount(0, system) == 4);
    CHECK(std::string(automaticComputeThreadSourceName(system)) == "physical-core-bound");
#endif
    system.physicalCoreCount = 0;
#ifdef __EMSCRIPTEN__
    CHECK(resolveComputeThreadCount(0, system) == 8);
#else
    CHECK(resolveComputeThreadCount(0, system) == 4);
#endif
    system.physicalCoreCount = 1;
#ifndef __EMSCRIPTEN__
    const gradient_kernel::Configuration gpu{gradient_kernel::Backend::Vulkan, gradient_kernel::Algorithm::Jacobi};
    CHECK(resolveApplicationResourcePlan(0, system, true, gpu).computeGpuPermitted());
    CHECK_FALSE(resolveApplicationResourcePlan(1, system, true, gpu).computeGpuPermitted());
    system.hardwareHint = 0;
    system.eligibleLogicalCpuCount.reset();
    system.physicalCoreCount = 4;
    CHECK(resolveComputeThreadCount(0, system) == 4);
    CHECK(std::string(automaticComputeThreadSourceName(system)) == "physical-cores");
#endif
}
TEST_CASE("small CPU auto counts give every core a worker plus the owner")
{
    struct Sizing { unsigned cpus, participants; };
    for (const auto [cpus, participants] : {Sizing{1, 2}, Sizing{2, 3}, Sizing{3, 4}, Sizing{4, 4}, Sizing{8, 8}})
    {
        auto physical = cpuAvailabilityObservation(cpus * 2, cpus * 2);
        physical.physicalCoreCount = cpus;
        physical.physicalCoresAreEligible = true;
        const auto plan = resolveApplicationResourcePlan(0, physical, true, {});
#ifdef __EMSCRIPTEN__
        CHECK(plan.computeParticipants == defaultComputeThreadCount(cpus * 2));
#else
        CHECK(plan.computeParticipants == participants);
        CHECK(std::string(computeParticipantSourceName(plan)) == "physical-cores");
        CHECK_FALSE(plan.exceedsCpuAllocation);
        // Without SMT, the additional owner intentionally exceeds CPU eligibility.
        physical.eligibleLogicalCpuCount = cpus;
        const auto noSmt = resolveApplicationResourcePlan(0, physical, true, {});
        CHECK(noSmt.computeParticipants == participants);
        CHECK(noSmt.exceedsCpuAllocation == (cpus < 4));
        // Apply the policy after bounding a system-wide physical count by affinity.
        physical.physicalCoreCount = 8;
        physical.physicalCoresAreEligible = false;
        CHECK(resolveApplicationResourcePlan(0, physical, true, {}).computeParticipants == participants);
#endif
        const auto logical = cpuAvailabilityObservation(cpus, cpus);
        CHECK(resolveApplicationResourcePlan(0, logical, true, {}).computeParticipants == participants);
        CHECK(resolveApplicationResourcePlan(0, physical, false, {}).computeParticipants == 1);
        for (unsigned requested : {1u, 2u, 3u, 4u})
            CHECK(resolveApplicationResourcePlan(requested, physical, true, {}).computeParticipants == requested);
    }
}
TEST_CASE("CPU list parsing is bounded and rejects malformed topology")
{
    using namespace compute_topology;
    CHECK(parseCpuList("0-2,8,10-11\n") == CpuList{0, 1, 2, 8, 10, 11});
    CHECK(parseCpuList("12,4\n") == CpuList{4, 12});
    for (auto text : {"", "0,", "-1", "1-0", "0-", "1 2", "1,,2", "0,0", "0-2,2", "4294967296", "0-1048576"})
        CHECK_FALSE(parseCpuList(text).has_value());
}
TEST_CASE("eligible sibling groups count cores once and reject incomplete or inconsistent data")
{
    using namespace compute_topology;
    CHECK(countCoreGroups({0, 1, 8, 9}, {{0, 8}, {1, 9}, {0, 8}, {1, 9}}) == 2);
    CHECK(countCoreGroups({8, 9}, {{0, 8}, {1, 9}}) == 2);
    CHECK(countCoreGroups({0, 8}, {{0, 8}, {0, 8}}) == 1);
    CHECK(countCoreGroups({0, 64, 128}, {{0}, {64}, {128}}) == 3);
    CHECK_FALSE(countCoreGroups({}, {}).has_value());
    CHECK_FALSE(countCoreGroups({0, 8}, {{0, 8}}).has_value());
    CHECK_FALSE(countCoreGroups({0}, {{8}}).has_value());
    CHECK_FALSE(countCoreGroups({0, 8}, {{0, 8}, {8}}).has_value());
    CHECK_FALSE(countCoreGroups({0}, {{0, 0}}).has_value());
    CHECK_FALSE(countCoreGroups({0, 0}, {{0}, {0}}).has_value());
}
TEST_CASE("Windows core masks respect affinity and require complete disjoint topology")
{
    using compute_topology::countCoreMasks;
    CHECK(countCoreMasks(15, {3, 12}) == 2);
    CHECK(countCoreMasks(3, {3, 12}) == 1);
    CHECK(countCoreMasks(5, {3, 12}) == 2);
    CHECK(countCoreMasks(1ull << 63, {1ull << 63}) == 1);
    CHECK_FALSE(countCoreMasks(0, {3}).has_value());
    CHECK_FALSE(countCoreMasks(15, {3}).has_value());
    CHECK_FALSE(countCoreMasks(15, {3, 14}).has_value());
    CHECK_FALSE(countCoreMasks(1, {0, 1}).has_value());
}
}
