// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ThreadCpuClock.h"
namespace glob2 {
// All process threads, including presentation, coordinator and driver work.
// Zero means unavailable; deliberately no coarse std::clock fallback.
inline std::uint64_t processCpuNs() noexcept {
#if defined(_WIN32)
    FILETIME creation{},exit{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user))return 0;
    const auto value=[](FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    return (value(kernel)+value(user))*100;
#elif defined(CLOCK_PROCESS_CPUTIME_ID)
    timespec t{};
    if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t))return 0;
    return std::uint64_t(t.tv_sec)*1000000000ull+std::uint64_t(t.tv_nsec);
#else
    return 0;
#endif
}
}
