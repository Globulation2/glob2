// SPDX-License-Identifier: GPL-3.0-or-later
#include <SteadyClock.h>
#include <chrono>

namespace GAGCore
{
std::int64_t steadyClockNanoseconds() noexcept
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
