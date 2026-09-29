// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "kernel/GradientBucket.h"
#include <array>
#include <utility>
#include <vector>

// Retained queue capacity belongs to one executor slot, never to a process.
struct GradientWorkspace
{
	std::array<GradientBucket, GradientBucket::COUNT> buckets;
	std::vector<std::pair<int, int>> deferredSeeds;
};
