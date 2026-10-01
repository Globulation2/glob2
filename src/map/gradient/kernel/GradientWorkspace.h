// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientBucket.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Retained queue capacity belongs to one executor slot, never to a process.
struct GradientWorkspace
{
	std::array<GradientBucket, GradientBucket::COUNT> buckets;
	std::vector<std::pair<int, int>> deferredSeeds;
	// Guard-area balancing's box-sum scratch (Map::seedGuardAreaCrowding).
	struct Crowding
	{
		std::vector<std::uint16_t> warriors, paint, rows;
		std::vector<int> columnSums;
		std::vector<std::size_t> positions, seeds;
	} crowding;
};
