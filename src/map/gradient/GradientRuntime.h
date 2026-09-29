// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private Map-owned execution state. Keeping this behind a pointer in Map.h
// prevents queue, thread and scratch-storage details from entering Map's API.
#include "GradientPipeline.h"
#include "kernel/GradientWorkspace.h"

#include <vector>

struct GradientRuntime
{
	std::vector<GradientWorkspace> workspaces{1};
	GradientPipeline pipeline;
};
