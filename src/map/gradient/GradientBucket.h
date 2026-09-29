// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

// One cost layer of the gradient bucket queue: cell indices in [0, size).
// cells.size() is capacity, so the kernel can reserve room for a run of
// expanded cells and then append through a raw cursor without branching.
struct GradientBucket
{
	// A power of two above the largest edge cost (42): each bucket then holds a
	// single cost at a time and a mask selects it.
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
