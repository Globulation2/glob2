// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <charconv>
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <optional>
#include <vector>
#include <cstdint>

// Total executor participants, including the simulation owner. Hardware discovery
// may return zero; one participant is the portable serial fallback. Below four
// available cores, give every core a background worker in addition to the owner.
// At four or more, leave one CPU for the owner. Work placement
// and match deadlines are independent of this local execution setting.
// This overload applies the count policy; native auto selects a core count below.
inline unsigned defaultComputeThreadCount(unsigned hardware)
{
    if (!hardware) return 1;
    if (hardware < 4) return hardware + 1;
#ifdef __EMSCRIPTEN__
    // Browser workers share a bounded Wasm heap with artwork and encoders;
    // logical CPU counts on large hosts are not a useful allocation budget.
    return std::min(hardware, 8u);
#else
    return hardware;
#endif
}

// Zero represents automatic sizing in application configuration. The explicit
// override accepts the executor's positive unsigned count range.
inline unsigned parseComputeThreadCount(std::string_view value)
{
    if (value == "auto") return 0;
    if (value.empty())
        throw std::invalid_argument("--compute-threads expects auto or a positive unsigned integer");
    unsigned count = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), count);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || count < 1)
        throw std::invalid_argument("--compute-threads expects auto or a positive unsigned integer");
    return count;
}

struct CpuAvailabilityObservation {
    std::optional<unsigned> eligibleLogicalCpuCount;
    unsigned hardwareHint = 0;
    // System-wide enabled cores unless filtered by observed affinity below.
    std::optional<unsigned> physicalCoreCount;
    enum class PhysicalSource { Unknown, Sysctl, LinuxTopology, WindowsTopology };
    PhysicalSource physicalCoreSource = PhysicalSource::Unknown;
    // True only for an exact core/affinity intersection, not a throughput estimate.
    bool physicalCoresAreEligible = false;
};
// Pure status mapping. A hardware hint is never promoted to observed allocation.
inline CpuAvailabilityObservation cpuAvailabilityObservation(unsigned hardware, unsigned eligible = 0) {
    CpuAvailabilityObservation result;
    result.hardwareHint = hardware;
    if (eligible) result.eligibleLogicalCpuCount = eligible;
    return result;
}
inline const char* cpuAvailabilitySourceName(const CpuAvailabilityObservation& observation) {
    if (observation.eligibleLogicalCpuCount) return "affinity-mask";
    return observation.hardwareHint ? "hardware-hint-only" : "unknown";
}
// Observe topology and eligibility once at setup. Linux uses creating-thread
// affinity; Windows uses process affinity. Counts do not measure CPU-time quota
// or reserved throughput.
CpuAvailabilityObservation discoverComputeCpuAvailability(unsigned hardware = std::thread::hardware_concurrency()) noexcept;

// Topology parsing/counting is separate from OS access for synthetic tests.
namespace compute_topology {
using CpuList = std::vector<unsigned>;
std::optional<CpuList> parseCpuList(std::string_view text);
// One complete sibling list per eligible CPU, in the same order.
std::optional<unsigned> countCoreGroups(const CpuList& eligible, const std::vector<CpuList>& siblings);
// A single Windows processor group's core masks, with complete eligible coverage.
std::optional<unsigned> countCoreMasks(std::uint64_t eligible, const std::vector<std::uint64_t>& masks);
}

// Physical cores first, then eligible logical CPUs, hardware hint, serial.
// A system-wide physical count can only be bounded by logical eligibility;
// it must never be described as an observed eligible physical count.
inline unsigned defaultComputeThreadCount(const CpuAvailabilityObservation& cpu)
{
#ifdef __EMSCRIPTEN__
    return defaultComputeThreadCount(cpu.hardwareHint);
#else
    if (cpu.physicalCoreCount && *cpu.physicalCoreCount) {
        const unsigned physical = *cpu.physicalCoreCount;
        const unsigned available = cpu.eligibleLogicalCpuCount ? std::min(physical, *cpu.eligibleLogicalCpuCount) : physical;
        return defaultComputeThreadCount(available);
    }
    return defaultComputeThreadCount(cpu.eligibleLogicalCpuCount.value_or(cpu.hardwareHint));
#endif
}
inline const char* automaticComputeThreadSourceName(const CpuAvailabilityObservation& cpu)
{
#ifndef __EMSCRIPTEN__
    if (cpu.physicalCoreCount && *cpu.physicalCoreCount) {
        if (!cpu.physicalCoresAreEligible && cpu.eligibleLogicalCpuCount)
            return "physical-core-bound";
        return "physical-cores";
    }
    if (cpu.eligibleLogicalCpuCount) return "eligible-logical-fallback";
#endif
    return cpu.hardwareHint ? "hardware-hint" : "serial-fallback";
}
const char* physicalCoreSourceName(const CpuAvailabilityObservation& cpu);
inline unsigned resolveComputeThreadCount(unsigned requested, const CpuAvailabilityObservation& cpu)
{
    return requested ? requested : defaultComputeThreadCount(cpu);
}
// Explicit hardware injection remains available to tests; normal callers discover
// topology through the one-argument overload.
inline unsigned resolveComputeThreadCount(unsigned requested, unsigned hardware)
{
    return resolveComputeThreadCount(requested, cpuAvailabilityObservation(hardware));
}
unsigned resolveComputeThreadCount(unsigned requested);

inline bool isRemovedComputeOption(std::string_view option)
{
    return option == "--ai-threads" || option == "--gradient-workers" || option == "--compute-experiments";
}
