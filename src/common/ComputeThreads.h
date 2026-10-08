// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <charconv>
#include <stdexcept>
#include <string_view>
#include <thread>

// Total executor participants, including the simulation owner. Hardware discovery
// may return zero; one participant is the portable serial fallback. Work placement
// and match deadlines are independent of this local execution setting.
inline unsigned defaultComputeThreadCount(unsigned hardware = std::thread::hardware_concurrency())
{
    return hardware ? hardware : 1;
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

inline unsigned resolveComputeThreadCount(unsigned requested, unsigned hardware = std::thread::hardware_concurrency())
{
    return requested ? requested : defaultComputeThreadCount(hardware);
}

inline bool isRemovedComputeOption(std::string_view option)
{
    return option == "--ai-threads" || option == "--gradient-workers" || option == "--compute-experiments";
}
