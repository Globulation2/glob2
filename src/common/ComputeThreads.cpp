// SPDX-License-Identifier: GPL-3.0-or-later
#include "ComputeThreads.h"

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <bit>
#include <cerrno>
#include <cstddef>
#include <new>
#include <sched.h>
#include <vector>
#endif

// Linux affinity is per thread: query the executor-creating thread, whose
// workers inherit this mask. Affinity includes cpuset restrictions, but not
// CPU-time quotas or an estimate of physical-core/SMT throughput.
CpuAvailabilityObservation discoverComputeCpuAvailability(unsigned hardware) noexcept
{
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    try
    {
        // cpu_set_t storage retains the alignment required by sched_getaffinity.
        // EINVAL can mean that the kernel's CPU-ID mask exceeds CPU_SETSIZE.
        constexpr std::size_t MaximumBytes = 128 * 1024;
        for (std::size_t sets = 1; sets <= MaximumBytes / sizeof(cpu_set_t); sets *= 2)
        {
            std::vector<cpu_set_t> mask(sets);
            const std::size_t bytes = mask.size() * sizeof(cpu_set_t);
            if (sched_getaffinity(0, bytes, mask.data()) == 0)
            {
                unsigned eligible = 0;
                const auto* bits = reinterpret_cast<const unsigned char*>(mask.data());
                for (std::size_t i = 0; i < bytes; ++i)
                    eligible += std::popcount(bits[i]);
                return cpuAvailabilityObservation(hardware, eligible);
            }
            if (errno != EINVAL) break;
        }
    }
    catch (const std::bad_alloc&) {} // Discovery must not prevent the portable fallback.
#endif
    return cpuAvailabilityObservation(hardware);
}
// Callers that only need a count retain the same portable fallback policy.
unsigned availableComputeCpuCount(unsigned hardware) noexcept {
    const auto observation=discoverComputeCpuAvailability(hardware);
    return availableComputeCpuCount(hardware,observation.eligibleLogicalCpuCount.value_or(0));
}

