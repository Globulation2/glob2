// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ComputeThreads.h"
#include "field/GradientSelection.h"
#include <iosfwd>

// A session setup decision, not a runtime governor. GPU selection continues to
// use the existing backend lifecycle, including explicit live menu changes.
struct ApplicationResourcePlan
{
    CpuAvailabilityObservation cpuAvailability;
    unsigned requestedComputeThreads = 0; // Zero means auto; counts include owner.
    unsigned computeParticipants = 1;
    bool threadsSupported = true;
    bool exceedsCpuAllocation = false; // Unknown eligibility is never an allocation.
    gradient_kernel::Configuration gradient;

    // Permission for this setup, not proof of an available device or executed work.
    bool computeGpuPermitted() const {
        return computeParticipants > 1 && gradient_kernel::isGPU(gradient.backend);
    }
};

// Pure injected policy: auto prefers physical cores, then eligible logical CPUs.
// Explicit counts remain unchanged unless the platform cannot run threads.
ApplicationResourcePlan resolveApplicationResourcePlan(unsigned requested,
    const CpuAvailabilityObservation&, bool threadsSupported, gradient_kernel::Configuration);
// Sample eligibility once on the executor-creating thread; no GPU probing.
ApplicationResourcePlan discoverApplicationResourcePlan(unsigned requested, gradient_kernel::Configuration);
const char* computeParticipantSourceName(const ApplicationResourcePlan&);
void writeApplicationResourcePlanJson(std::ostream&, const ApplicationResourcePlan&);
