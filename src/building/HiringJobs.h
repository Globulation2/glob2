// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/EntityRef.h"
#include <array>
namespace Hiring
{
inline constexpr unsigned PublicationDelay = 1, RefreshInterval = 4, CandidateExtras = 2;
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
