// SPDX-License-Identifier: GPL-3.0-or-later
#include "ComputeThreads.h"
#include <bit>
#include <string>
#include <map>
#include <cstddef>
#include <cstring>
#include <set>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <fstream>
#include <sched.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace compute_topology {
std::optional<CpuList> parseCpuList(std::string_view text)
{
    // Bound both CPU identifiers and expansion of untrusted sysfs input.
    constexpr unsigned MaximumCpuId = 1024 * 1024 - 1;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
    if (text.empty()) return std::nullopt;
    CpuList result;
    while (!text.empty()) {
        unsigned first = 0;
        auto parsed = std::from_chars(text.data(), text.data() + text.size(), first);
        if (parsed.ec != std::errc{} || parsed.ptr == text.data() || first > MaximumCpuId)
            return std::nullopt;
        text.remove_prefix(parsed.ptr - text.data());
        unsigned last = first;
        if (!text.empty() && text.front() == '-') {
            text.remove_prefix(1);
            parsed = std::from_chars(text.data(), text.data() + text.size(), last);
            if (parsed.ec != std::errc{} || parsed.ptr == text.data() || last < first || last > MaximumCpuId)
                return std::nullopt;
            text.remove_prefix(parsed.ptr - text.data());
        }
        if (result.size() + last - first + 1 > MaximumCpuId + 1) return std::nullopt;
        for (unsigned cpu = first; cpu <= last; ++cpu) result.push_back(cpu);
        if (text.empty()) break;
        if (text.front() != ',') return std::nullopt;
        text.remove_prefix(1);
        if (text.empty()) return std::nullopt;
    }
    std::sort(result.begin(), result.end());
    if (std::adjacent_find(result.begin(), result.end()) != result.end()) return std::nullopt;
    return result;
}

std::optional<unsigned> countCoreGroups(const CpuList& eligible, const std::vector<CpuList>& siblings)
{
    if (eligible.empty() || eligible.size() != siblings.size()) return std::nullopt;
    std::set<CpuList> groups;
    std::map<unsigned, CpuList> membership;
    std::set<unsigned> seen;
    for (std::size_t i = 0; i < eligible.size(); ++i) {
        auto group = siblings[i];
        std::sort(group.begin(), group.end());
        if (!seen.insert(eligible[i]).second ||
            !std::binary_search(group.begin(), group.end(), eligible[i]) ||
            std::adjacent_find(group.begin(), group.end()) != group.end()) return std::nullopt;
        for (unsigned cpu : group) {
            const auto [entry, inserted] = membership.emplace(cpu, group);
            if (!inserted && entry->second != group) return std::nullopt;
        }
        groups.insert(std::move(group));
    }
    return static_cast<unsigned>(groups.size());
}
std::optional<unsigned> countCoreMasks(std::uint64_t eligible, const std::vector<std::uint64_t>& masks)
{
    if (!eligible) return std::nullopt;
    std::uint64_t seen = 0;
    unsigned count = 0;
    for (auto mask : masks) {
        if (!mask || (seen & mask)) return std::nullopt;
        seen |= mask;
        if (mask & eligible) ++count;
    }
    if ((seen & eligible) != eligible) return std::nullopt;
    return count;
}

}

const char* physicalCoreSourceName(const CpuAvailabilityObservation& cpu)
{
    if (!cpu.physicalCoreCount) return "unknown";
    switch (cpu.physicalCoreSource) {
        case CpuAvailabilityObservation::PhysicalSource::Sysctl: return "sysctl";
        case CpuAvailabilityObservation::PhysicalSource::LinuxTopology: return "sysfs-affinity";
        case CpuAvailabilityObservation::PhysicalSource::WindowsTopology: return "windows-topology-affinity";
        default: return "unknown";
    }
}

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
static std::optional<compute_topology::CpuList> readSiblings(unsigned cpu, const char* name)
{
    std::ifstream file("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/" + name);
    if (!file) return std::nullopt;
    // Read a bounded line; very large or malformed observations are unknown.
    std::string text;
    char ch;
    while (file.get(ch)) {
        if (text.size() >= 1024 * 1024) return std::nullopt;
        text.push_back(ch);
    }
    if (!file.eof()) return std::nullopt;
    return compute_topology::parseCpuList(text);
}
#endif

// Discovery samples the executor-creating thread. No affinity is changed, and
// none of these observations measures CPU-time quota or reserved throughput.
CpuAvailabilityObservation discoverComputeCpuAvailability(unsigned hardware) noexcept
{
    auto result = cpuAvailabilityObservation(hardware);
    try {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
        constexpr std::size_t MaximumBytes = 128 * 1024;
        for (std::size_t sets = 1; sets <= MaximumBytes / sizeof(cpu_set_t); sets *= 2) {
            std::vector<cpu_set_t> mask(sets);
            const std::size_t bytes = mask.size() * sizeof(cpu_set_t);
            if (sched_getaffinity(0, bytes, mask.data()) == 0) {
                compute_topology::CpuList eligible;
                const auto* bits = reinterpret_cast<const unsigned char*>(mask.data());
                // cpu_set_t stores native unsigned-long words; byte-order must
                // not change the logical CPU IDs on big-endian Linux targets.
                for (std::size_t i = 0; i < bytes; i += sizeof(unsigned long)) {
                    unsigned long word = 0;
                    std::memcpy(&word, bits + i, sizeof(word));
                    for (unsigned bit = 0; bit < sizeof(word) * 8; ++bit)
                        if (word & (1ul << bit)) eligible.push_back(static_cast<unsigned>(i * 8 + bit));
                }
                if (eligible.empty()) break;
                result.eligibleLogicalCpuCount = static_cast<unsigned>(eligible.size());
                std::vector<compute_topology::CpuList> siblings;
                for (unsigned cpu : eligible) {
                    auto group = readSiblings(cpu, "core_cpus_list");
                    if (!group) group = readSiblings(cpu, "thread_siblings_list");
                    if (!group) return result;
                    siblings.push_back(std::move(*group));
                }
                result.physicalCoreCount = compute_topology::countCoreGroups(eligible, siblings);
                if (result.physicalCoreCount) {
                    result.physicalCoreSource = CpuAvailabilityObservation::PhysicalSource::LinuxTopology;
                    result.physicalCoresAreEligible = true;
                }
                return result;
            }
            if (errno != EINVAL) break;
        }
#elif defined(__APPLE__)
        int cores = 0;
        std::size_t size = sizeof(cores);
        if (sysctlbyname("hw.physicalcpu", &cores, &size, nullptr, 0) == 0 && size == sizeof(cores) && cores > 0) {
            result.physicalCoreCount = static_cast<unsigned>(cores);
            result.physicalCoreSource = CpuAvailabilityObservation::PhysicalSource::Sysctl;
        }
#elif defined(_WIN32)
        // A primary-group mask is not a machine-wide eligibility observation.
        // Leave multi-group systems unknown rather than silently undercounting.
        if (GetActiveProcessorGroupCount() != 1) return result;
        // WOW64 folds masks above bit 31; do not report those as eligibility.
        if (GetActiveProcessorCount(0) > sizeof(DWORD_PTR) * 8) return result;
        DWORD_PTR processMask = 0, systemMask = 0;
        if (!GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask) || !processMask) return result;
        result.eligibleLogicalCpuCount = static_cast<unsigned>(std::popcount(processMask));
        DWORD bytes = 0;
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER || !bytes || bytes > 16 * 1024 * 1024) return result;
        // Aligned storage for variable-sized Windows records.
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX> records(
            (bytes + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) - 1) / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX));
        const DWORD capacity = static_cast<DWORD>(records.size() * sizeof(records[0]));
        if (!GetLogicalProcessorInformationEx(RelationProcessorCore, records.data(), &bytes) || bytes > capacity) return result;
        std::vector<std::uint64_t> coreMasks;
        for (std::size_t offset = 0; offset < bytes;) {
            constexpr std::size_t headerBytes = offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor);
            constexpr std::size_t coreBytes = headerBytes + offsetof(PROCESSOR_RELATIONSHIP, GroupMask) + sizeof(GROUP_AFFINITY);
            if (bytes - offset < headerBytes) return result;
            const auto* record = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
                reinterpret_cast<const unsigned char*>(records.data()) + offset);
            if (record->Size < coreBytes || record->Size > bytes - offset ||
                record->Size % alignof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) != 0 ||
                record->Relationship != RelationProcessorCore || record->Processor.GroupCount != 1 ||
                record->Processor.GroupMask[0].Group != 0) return result;
            coreMasks.push_back(record->Processor.GroupMask[0].Mask);
            offset += record->Size;
        }
        result.physicalCoreCount = compute_topology::countCoreMasks(processMask, coreMasks);
        if (result.physicalCoreCount) {
            result.physicalCoreSource = CpuAvailabilityObservation::PhysicalSource::WindowsTopology;
            result.physicalCoresAreEligible = true;
        }
#endif
    } catch (...) {
        // Discovery is advisory and must never prevent startup. Keep any complete
        // logical eligibility observation when topology/allocation fails.
    }
    return result;
}

unsigned resolveComputeThreadCount(unsigned requested)
{
    return requested ? requested : defaultComputeThreadCount(discoverComputeCpuAvailability());
}
