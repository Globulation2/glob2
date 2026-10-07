// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The occupied slots of a fixed entity slot array, kept sorted by slot id so a
// walk over live entities visits them in the same order as the array would,
// without touching the empty slots. The slot array stays authoritative; every
// site that assigns a slot attaches or detaches here.
template<class T> class LiveSlotList
{
	std::vector<std::uint16_t> ids;
	std::vector<T*> items;
public:
	std::size_t size() const { return ids.size(); }
	bool empty() const { return ids.empty(); }
	std::span<const std::uint16_t> slots() const { return ids; }
	std::span<T* const> entries() const { return items; }
	void attach(std::uint16_t id, T* item)
	{
		const auto pos = std::lower_bound(ids.begin(), ids.end(), id);
		const auto at = std::size_t(pos - ids.begin());
		if (pos != ids.end() && *pos == id) { items[at] = item; return; }
		ids.insert(pos, id);
		items.insert(items.begin() + at, item);
	}
	void detach(std::uint16_t id)
	{
		const auto pos = std::lower_bound(ids.begin(), ids.end(), id);
		if (pos == ids.end() || *pos != id) return;
		items.erase(items.begin() + (pos - ids.begin()));
		ids.erase(pos);
	}
	void clear() { ids.clear(); items.clear(); }
	void rebuild(T* const* array, std::size_t count)
	{
		clear();
		for (std::size_t id = 0; id < count; ++id)
			if (array[id]) { ids.push_back(std::uint16_t(id)); items.push_back(array[id]); }
	}
	bool matches(T* const* array, std::size_t count) const
	{
		std::size_t n = 0;
		for (std::size_t id = 0; id < count; ++id)
		{
			if (!array[id]) continue;
			if (n >= ids.size() || ids[n] != id || items[n] != array[id]) return false;
			++n;
		}
		return n == ids.size();
	}
};
