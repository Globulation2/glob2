// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <charconv>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

// Development-only preflight, not a measurement or a hard allocation bound.
// The evidence owner supplies an explicit memory budget and serializes writers.
// Reserve room for the original/reloaded maps, temporary gradients and host work.
namespace benchmark_fixture {
inline constexpr std::uint64_t StressMemoryMiB=8192;
inline bool memoryAdmission(std::uint64_t budgetMiB,std::uint64_t availableMiB) {
    return budgetMiB>=StressMemoryMiB && availableMiB>=budgetMiB
        && availableMiB-budgetMiB>=2048;
}
inline std::uint64_t parseMemoryBudget(const char* text) {
    if(!text || !*text) return 0;
    const std::string_view value(text);std::uint64_t result=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    return parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size() ? result : 0;
}
inline std::uint64_t availableMemoryMiB() {
#if defined(__linux__)
    std::ifstream input("/proc/meminfo");std::string line;
    while(std::getline(input,line)) {
        if(!line.starts_with("MemAvailable:")) continue;
        const auto first=line.find_first_of("0123456789");
        if(first==std::string::npos) return 0;
        std::uint64_t kib=0;const auto parsed=std::from_chars(line.data()+first,line.data()+line.size(),kib);
        return parsed.ec==std::errc{} ? kib/1024 : 0;
    }
#endif
    // No supported available-memory reading means no large allocation admission.
    return 0;
}
}
