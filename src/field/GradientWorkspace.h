// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GradientBucket.h"
#include "GradientBackend.h"
#include "TerrainGradientWorkspace.h"
#include <memory>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Retained queue capacity belongs to one executor slot, never to a process.
struct GradientWorkspace
{
	std::shared_ptr<gradient_kernel::BackendSession> backendSession = std::make_shared<gradient_kernel::BackendSession>();
	gradient_kernel::Family family = gradient_kernel::Family::Generic;
	std::unique_ptr<TerrainGradientWorkspace> terrain;
	std::array<GradientBucket, GradientBucket::COUNT> buckets;
	std::vector<std::pair<int, int>> deferredSeeds;
};
