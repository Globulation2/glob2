// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <span>

namespace SimulationSnapshot
{
// Entity capture groups teams and preserves their population order. Select a team once, then scan its
// contiguous records without a team branch per entity or another index table.
template<class Record>
std::span<const Record> teamRecords(std::span<const Record> records, int team)
{
	if (records.empty()) return {};
	const auto first=std::lower_bound(records.begin(),records.end(),team,
		[](const Record& record,int value) { return record.team<value; });
	const auto last=std::upper_bound(first,records.end(),team,
		[](int value,const Record& record) { return value<record.team; });
	return {first,last};
}
}
