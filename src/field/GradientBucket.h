// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

// One cost layer of the gradient bucket queue. Only cells[0..size) are live;
// cells.size() is allocated storage, not the number of queued entries. The
// kernel reserves enough storage for a chunk, writes through raw end cursors,
// then publishes their final offsets back to size. Rejected writes beyond size
// may remain in storage and must never be read as queue entries.
struct GradientBucket
{
	// Power of two above the largest edge cost (42). The ring cannot reuse a
	// bucket for a future cost while that bucket's current layer is expanding.
	static constexpr unsigned COUNT = 64;

	std::vector<std::uint32_t> cells;
	std::size_t size = 0;

	void reserveExtra(std::size_t extra)
	{
		if (size + extra > cells.size())
			grow(size + extra);
	}
	void push(std::uint32_t cell)
	{
		reserveExtra(1);
		cells[size++] = cell;
	}
	void clear() { size = 0; }

private:
	[[gnu::noinline, gnu::cold]] void grow(std::size_t needed)
	{
		cells.resize(std::max(needed, std::max<std::size_t>(256, cells.size() * 2)));
	}
};
