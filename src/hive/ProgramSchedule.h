// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace Hive
{
// Serve the oldest due deadline, breaking ties by the map's stable name order.
// A long-running first program must not starve the rest. Move its deadline from
// now, never from the old due tick, so catching up cannot create a burst.
template <class Programs> std::string scheduleDue(Programs &programs, unsigned tick)
{
	auto selected = programs.end();
	for (auto it = programs.begin(); it != programs.end(); ++it)
		if (!it->second.paused && tick >= it->second.nextTick &&
			(selected == programs.end() || it->second.nextTick < selected->second.nextTick))
			selected = it;
	if (selected == programs.end())
		return {};
	selected->second.nextTick =
		tick + selected->second.definition.at("intervalTicks").template get<unsigned>();
	return selected->first;
}
} // namespace Hive
