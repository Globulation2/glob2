// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <charconv>
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <thread>

// Total executor participants, including the simulation owner. Hardware discovery
// may return zero; one participant is the portable serial fallback. Work placement
// and match deadlines are independent of this local execution setting.
inline unsigned defaultComputeThreadCount(unsigned hardware)
{
#ifdef __EMSCRIPTEN__
    // Browser workers share a bounded Wasm heap with artwork and encoders;
    // logical CPU counts on large hosts are not a useful allocation budget.
    return std::min(hardware ? hardware : 1, 8u);
#else
    return hardware ? hardware : 1;
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

// A nonzero eligible count bounds a known hardware hint. Zero means that
// discovery failed, so retain the portable hint and its one-participant fallback.
inline unsigned availableComputeCpuCount(unsigned hardware, unsigned eligible)
{
    return eligible ? (hardware ? std::min(hardware, eligible) : eligible) : (hardware ? hardware : 1);
}

// Platform discovery is isolated from the pure policy above. Unknown eligibility
// retains the hardware hint; this does not infer CPU-time quotas or SMT capacity.
unsigned availableComputeCpuCount(unsigned hardware = std::thread::hardware_concurrency()) noexcept;

inline unsigned defaultComputeThreadCount()
{
    return defaultComputeThreadCount(availableComputeCpuCount());
}

// Explicit hardware injection stays pure; normal application calls discover the
// current CPU eligibility. Explicit counts are not clamped by either path.
inline unsigned resolveComputeThreadCount(unsigned requested, unsigned hardware)
{
    return requested ? requested : defaultComputeThreadCount(hardware);
}

inline unsigned resolveComputeThreadCount(unsigned requested)
{
    return requested ? requested : defaultComputeThreadCount();
}

inline bool isRemovedComputeOption(std::string_view option)
{
    return option == "--ai-threads" || option == "--gradient-workers" || option == "--compute-experiments";
}
