// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private Map-owned execution state. Keeping this behind a pointer in Map.h
// prevents queue, thread and scratch-storage details from entering Map's API.
#include "GradientPipeline.h"
#include "ResourceSeedCache.h"
#include "field/GradientWorkspace.h"

#include <vector>

struct GradientRuntime
{
	struct Workspace
	{
		GradientWorkspace propagation;
		struct Crowding
		{
			std::vector<std::uint16_t> warriors, paint, rows;
			std::vector<int> columnSums;
			std::vector<std::size_t> positions, seeds;
		} crowding;
	};
	std::vector<Workspace> workspaces{1};
	GradientPipeline pipeline;
	ResourceSeedCache resourceSeeds;
};
