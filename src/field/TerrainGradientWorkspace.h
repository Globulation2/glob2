// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientBucket.h"
#include <array>
#include <vector>

// Retained by one executor/search, allocated only for custom weighted terrain.
struct TerrainGradientWorkspace
{
	std::vector<GradientBucket> buckets;
	void prepare(unsigned count)
	{
		if (buckets.size() != count)
			buckets.resize(count);
	}
};
