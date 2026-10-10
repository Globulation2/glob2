// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif

namespace glob2
{
// Wall time includes waiting for the device; policy training must use actual
// host CPU instead. Zero means unavailable and cannot qualify a learned plan.
inline std::uint64_t threadCpuNs() noexcept
{
#if defined(_WIN32)
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)) return 0;
    const auto value = [](FILETIME time) {
        return (std::uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return (value(kernel) + value(user)) * 100;
#elif defined(CLOCK_THREAD_CPUTIME_ID)
    timespec time{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time)) return 0;
    return std::uint64_t(time.tv_sec) * 1000000000ull + std::uint64_t(time.tv_nsec);
#else
    return 0;
#endif
}
}
