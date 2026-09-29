// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include <PerformanceTelemetry.h>
#include "BuildingGradientSearch.h"
#include "Map.h"
#include "MapInternal.h"
#include "kernel/GradientRelaxation.h"

using gradient_kernel::BUCKETS;
using gradient_kernel::COST_LIMIT;
using gradient_kernel::EntrySteps;
using gradient_kernel::LAND_STEPS;
using gradient_kernel::WATER_STEP;
using gradient_kernel::entrySteps;
using gradient_kernel::expandBucket;
using gradient_kernel::weightedClass;

void BuildingGradientSearch::begin(const Map &map, std::uint16_t *seeded, int swim)
{
	gradient = seeded;
	cells = std::size_t(map.getW()) * map.getH();
	widthMask = map.getMaskW();
	heightMask = map.getMaskH();
	widthShift = map.getShiftW();
	swimClass = swim;
	currentCost = 0;
	popped = 0;
	pending = 0;
	for (auto &bucket : buckets) bucket.clear();
	const bool weighted = weightedClass(swim);
	const bool splitWater = weighted && map.computeEnabled(Map::ComputeInitialize) && cells >= 16384;
	if (weighted) water.resize(cells);
	else water.clear();
	if (splitWater)
	{
		map.initializeGradientCells([&](size_t begin, size_t end) {
			for (size_t i = begin; i < end; ++i) water[i] = map.isWater(static_cast<unsigned>(i));
		});
	}
	// Building fields have only zero-cost seeds, so no deferred seeds are needed.
	for (std::size_t i = 0; i < cells; ++i)
	{
		assert(gradient[i] <= GRADIENT_UNREACHABLE || gradient[i] == GRADIENT_AT_GOAL);
		if (gradient[i] == GRADIENT_AT_GOAL)
		{
			buckets[0].push(static_cast<Uint32>(i));
			++pending;
		}
		if (weighted && !splitWater) water[i] = map.isWater(static_cast<unsigned>(i));
	}
}

bool BuildingGradientSearch::resolved(std::size_t target) const
{
	assert(target < cells);
	const auto value = gradient[target];
	return complete() || value == GRADIENT_FORBIDDEN || value == GRADIENT_AT_GOAL
		|| (value > GRADIENT_UNREACHABLE && GRADIENT_AT_GOAL - value < currentCost);
}

void BuildingGradientSearch::resolve(std::size_t target)
{
	assert(target <= cells);
	if (complete() || (target < cells && resolved(target))) return;
	PERF_SCOPE_TIME(BuildingGradientResume);
	auto sweep = [&](auto weighted, EntrySteps waterSteps, auto waterAt)
	{
		while (pending && (target == cells || !resolved(target)))
		{
			assert(currentCost <= COST_LIMIT);
			popped += buckets[currentCost % BUCKETS].size;
			expandBucket<decltype(weighted)::value>(gradient, buckets.data(), pending, currentCost, COST_LIMIT,
				widthMask, heightMask, widthShift, waterSteps, waterAt);
			++currentCost;
		}
	};
	if (water.empty())
		sweep(std::false_type(), LAND_STEPS, [](size_t) { return false; });
	else
	{
		const std::uint8_t *const waterCells = water.data();
		sweep(std::true_type(), entrySteps(WATER_STEP[swimClass]), [waterCells](size_t i) { return waterCells[i] != 0; });
	}
}
