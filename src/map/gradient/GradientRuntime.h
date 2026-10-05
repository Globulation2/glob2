// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private Map-owned execution state. Keeping this behind a pointer in Map.h
// prevents queue, thread and scratch-storage details from entering Map's API.
#include "GradientPipeline.h"
#include "BuildingGradientScheduler.h"
#include "BuildingGradientImpact.h"
#include "BuildingGradientDiagnostics.h"
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
	std::shared_ptr<AsyncGradientExecutor> async = std::make_shared<AsyncGradientExecutor>();
	GradientPipeline pipeline{async};
	BuildingGradientScheduler buildings{async};
	bool buildingAccessByClass = false;
	std::unique_ptr<BuildingGradientDiagnostics> buildingDiagnostics;
	std::unique_ptr<BuildingGradientImpact> impact;
	struct TickTiming
	{
		std::uint32_t tick;
		std::uint64_t elapsedNs, waitNs, bytes, pending;
	};
	std::string timingPath;
	std::uint64_t timingStart = 0, timingWaitStart = 0;
	std::vector<TickTiming> timings;
};
