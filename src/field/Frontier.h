// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cassert>
#include <cstddef>
#include <vector>

namespace field
{
// Reusable FIFO for distance fields. Retain the largest pending frontier, rather
// than every cell ever visited. Capacity belongs to the caller's execution owner.
class Frontier
{
	std::vector<int> cells;
	std::size_t head = 0, count = 0;

  public:
	bool empty() const { return count == 0; }
	std::size_t capacity() const { return cells.size(); }
	std::size_t retainedBytes() const noexcept { return cells.capacity() * sizeof(int); }
	void clear() { head = count = 0; }
	int front() const
	{
		assert(count);
		return cells[head];
	}
	void pop_front()
	{
		assert(count);
		head = (head + 1) & (cells.size() - 1);
		--count;
	}
	void push_back(int value)
	{
		if (count == cells.size())
		{
			std::vector<int> grown(cells.empty() ? 64 : cells.size() * 2);
			for (std::size_t i = 0; i < count; ++i)
				grown[i] = cells[(head + i) & (cells.size() - 1)];
			cells.swap(grown);
			head = 0;
		}
		cells[(head + count) & (cells.size() - 1)] = value;
		++count;
	}
};
} // namespace field
