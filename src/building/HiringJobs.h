// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/EntityRef.h"
#include <array>
namespace Hiring
{
// New games rank each team every RefreshInterval ticks and publish the
// ranking PublicationDelay ticks later. Saves keep the rules they were taken
// with, so games saved under older rules continue unchanged.
inline constexpr unsigned PublicationDelay = 4, RefreshInterval = 8, CandidateExtras = 2;
inline constexpr unsigned MaxPublicationDelay = 4, MaxRefreshInterval = 16;
struct Rules
{
	unsigned delay = PublicationDelay, refresh = RefreshInterval;
	bool valid() const
	{
		return delay >= 1 && delay <= MaxPublicationDelay && refresh >= 1 &&
			   refresh <= MaxRefreshInterval;
	}
	// Ticks from a ranking's snapshot until the next ranking is published.
	unsigned lookahead() const { return delay + refresh - 1; }
	bool operator==(const Rules &) const = default;
};
// Keep the tested eight-tick attraction retry window independent of scheduling.
inline constexpr unsigned RecruitmentRetryTicks = 8;
// Slot zero is delivery; the remaining slots are clearing, exploration and defense.
inline std::array<unsigned, 4> roleTargets(unsigned seats, bool delivery, std::array<bool, 3> roles)
{
	std::array<unsigned, 4> result{};
	const unsigned count =
		unsigned(delivery) + unsigned(roles[0]) + unsigned(roles[1]) + unsigned(roles[2]);
	if (!count)
		return result;
	unsigned rank = 0;
	for (unsigned slot = 0; slot < result.size(); ++slot)
		if (slot ? roles[slot - 1] : delivery)
			result[slot] = seats / count + (rank++ < seats % count);
	return result;
}
enum class Job : unsigned
{
	Delivery,
	Fetch,
	Clear,
	Explore,
	Defend
};
struct Key
{
	Job job;
	int material = -1;
	bool operator==(const Key &) const = default;
};
} // namespace Hiring
