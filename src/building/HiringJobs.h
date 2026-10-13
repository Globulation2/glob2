// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sim/EntityRef.h"
namespace Hiring
{
inline constexpr unsigned PublicationDelay = 4, RefreshInterval = 4, CandidateExtras = 2;
// A vacancy just missed by an observation needs at most one refresh plus publication.
inline constexpr unsigned RecruitmentRetryTicks = PublicationDelay + RefreshInterval;
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
