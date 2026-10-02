// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cassert>
#include <climits>
#include <cstdint>
#include <vector>

namespace AIMaximaPlacement
{
// Obstacle-free wrapped Manhattan fields are at most 512 on supported maps.
// Expose the old INT_MAX infinity to scoring while storing it compactly.
// Obstacle-constrained routes can exceed the geometric diameter and stay wide.
class DistanceField
{
	std::vector<uint16_t> cells;
	static uint16_t encode(int value)
	{
		assert(value == INT_MAX || (value >= 0 && value < UINT16_MAX));
		return value == INT_MAX ? UINT16_MAX : static_cast<uint16_t>(value);
	}
public:
	class Reference
	{
		uint16_t &cell;
	public:
		explicit Reference(uint16_t &value) : cell(value) {}
		operator int() const { return cell == UINT16_MAX ? INT_MAX : int(cell); }
		Reference &operator=(int value) { cell = encode(value); return *this; }
		Reference &operator=(const Reference &value) { return *this = int(value); }
	};
	void clear() { cells.clear(); }
	bool empty() const { return cells.empty(); }
	size_t size() const { return cells.size(); }
	void assign(size_t count, int value) { cells.assign(count, encode(value)); }
	int operator[](size_t i) const { return cells[i] == UINT16_MAX ? INT_MAX : int(cells[i]); }
	Reference operator[](size_t i) { return Reference(cells[i]); }
	std::vector<uint16_t> &storage() { return cells; }
	const std::vector<uint16_t> &storage() const { return cells; }
};
}
