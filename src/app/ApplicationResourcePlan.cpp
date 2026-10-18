// SPDX-License-Identifier: GPL-3.0-or-later
#include "ApplicationResourcePlan.h"
#include <ThreadSupport.h>
#include <ostream>

ApplicationResourcePlan resolveApplicationResourcePlan(unsigned requested,
    const CpuAvailabilityObservation& cpu, bool threadsSupported, gradient_kernel::Configuration gradient)
{
    ApplicationResourcePlan plan;
    plan.cpuAvailability = cpu;
    plan.requestedComputeThreads = requested;
    plan.threadsSupported = threadsSupported;
    plan.gradient = gradient;
    plan.computeParticipants = threadsSupported ? resolveComputeThreadCount(requested, cpu) : 1;
    plan.exceedsCpuAllocation = cpu.eligibleLogicalCpuCount &&
        plan.computeParticipants > *cpu.eligibleLogicalCpuCount;
    return plan;
}

ApplicationResourcePlan discoverApplicationResourcePlan(unsigned requested, gradient_kernel::Configuration gradient)
{
    return resolveApplicationResourcePlan(requested, discoverComputeCpuAvailability(),
        GAGCore::ThreadSupport::available, gradient);
}

const char* computeParticipantSourceName(const ApplicationResourcePlan& plan)
{
    if (!plan.threadsSupported) return "threads-unavailable";
    if (plan.requestedComputeThreads) return "explicit-override";
    return automaticComputeThreadSourceName(plan.cpuAvailability);
}

void writeApplicationResourcePlanJson(std::ostream& out, const ApplicationResourcePlan& plan)
{
    out << "{\"requested_threads\":" << plan.requestedComputeThreads
        << ",\"compute_participants\":" << plan.computeParticipants
        << ",\"participant_source\":\"" << computeParticipantSourceName(plan)
        << "\",\"cpu_availability_source\":\"" << cpuAvailabilitySourceName(plan.cpuAvailability)
        << "\",\"hardware_threads\":" << plan.cpuAvailability.hardwareHint
        << ",\"eligible_threads\":";
    if (plan.cpuAvailability.eligibleLogicalCpuCount) out << *plan.cpuAvailability.eligibleLogicalCpuCount;
    else out << "null";
    out << ",\"physical_cores\":";
    if (plan.cpuAvailability.physicalCoreCount) out << *plan.cpuAvailability.physicalCoreCount;
    else out << "null";
    out << ",\"physical_core_source\":\"" << physicalCoreSourceName(plan.cpuAvailability)
        << "\",\"physical_cores_are_eligible\":" << (plan.cpuAvailability.physicalCoresAreEligible ? "true" : "false");
    out << ",\"threads_supported\":" << (plan.threadsSupported ? "true" : "false")
        << ",\"exceeds_cpu_allocation\":" << (plan.exceedsCpuAllocation ? "true" : "false")
        << ",\"compute_gpu_permitted\":" << (plan.computeGpuPermitted() ? "true" : "false")
        << ",\"gradient_backend\":\"" << gradient_kernel::backendName(plan.gradient.backend) << "\"}";
}
