// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace GAGCore
{
// std::chrono::steady_clock::now(), in nanoseconds since that clock's epoch.
// Defined out of line so that widely included headers need not include
// <chrono>: with C++20 it brings formatting and time-zone support that costs
// about 0.6 s of compile time in every translation unit that reaches it.
std::int64_t steadyClockNanoseconds() noexcept;
}
