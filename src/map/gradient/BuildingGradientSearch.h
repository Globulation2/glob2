// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class Map;

// Resumable version of the building field's zero-cost, multi-source Dijkstra.
// Obstacles/goals are already frozen in the initialized gradient. Weighted swim
// classes additionally retain water costs, so a pause never mixes map snapshots.
// Callers must resolve a cell before reading it; completion preserves the old
// full-field API and save representation without refreshing the field's age.
class BuildingGradientSearch
{
	// One more than the largest edge cost; checked against the shared kernel.
	static constexpr int BucketCount = 43;
	std::array<std::vector<int>, BucketCount> buckets;
	std::vector<std::uint8_t> water;
	std::uint16_t *gradient = nullptr;
	std::size_t cells = 0, pending = 0;
	int currentCost = 0, swimClass = 0;
	int widthMask = 0, heightMask = 0, widthShift = 0;

public:
	static bool enabled();
	void begin(const Map &map, std::uint16_t *seeded, int swim);
	// target == cells finishes the field. A whole cost layer is completed to
	// preserve equal-distance sidesteps as well as the requested scalar value.
	void resolve(std::size_t target);
	void finish() { resolve(cells); }
	bool complete() const { return pending == 0; }
	bool resolved(std::size_t target) const;
};
