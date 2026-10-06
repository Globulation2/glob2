// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include <PerformanceTelemetry.h>
#include "BuildingGradientSearch.h"
#include "BuildingGradientDiagnostics.h"
#include <mutex>
#include "Map.h"
#include "MapInternal.h"
#include "field/TerrainGradient.h"

using gradient_kernel::BUCKETS;
using gradient_kernel::COST_LIMIT;
using gradient_kernel::EntrySteps;
using gradient_kernel::LAND_STEPS;
using gradient_kernel::WATER_STEP;
using gradient_kernel::entrySteps;
using gradient_kernel::expandBucket;
using gradient_kernel::weightedClass;

void BuildingGradientSearch::begin(const Map &map, std::uint16_t *seeded, int swim, int gid)
{
	const bool modified = map.hasTerrainMovementModifiers();
	beginFrozen(map.getW(), map.getH(), seeded, swim,
		weightedClass(swim) || modified ? map.frozenTerrainSnapshot() : nullptr,
		modified, COST_LIMIT);
	attach(map, seeded, gid, map.topologyGeneration);
}

void BuildingGradientSearch::attach(const Map &map, std::uint16_t *field, int gid, std::uint32_t generation)
{
	sourceMap = &map;
	buildingId = gid;
	snapshotGeneration = generation;
	gradient = field;
}

void BuildingGradientSearch::beginFrozen(int width, int height, std::uint16_t *seeded, int swim,
	std::shared_ptr<const std::vector<TerrainType>> costs, bool modified, int limit, int settled)
{
	assert(width > 0 && height > 0 && !(width & (width - 1)) && !(height & (height - 1)));
	assert(swim >= 0 && swim < SWIM_CLASS_COUNT && limit >= 0 && settled >= 0);
	sourceMap = nullptr;
	buildingId = -1;
	gradient = seeded;
	cells = std::size_t(width) * height;
	widthMask = width - 1;
	heightMask = height - 1;
	swimClass = swim;
	currentCost = settled;
	popped = 0;
	pending = nextSeed = 0;
	costLimit = std::min(limit, COST_LIMIT);
	for (auto &bucket : buckets) bucket.clear();
	deferredSeeds.clear();
	modifiedCosts = modified;
	terrain = std::move(costs);
	assert(!(modifiedCosts || weightedClass(swim)) || (terrain && terrain->size() == cells));
	for (std::size_t i = 0; i < cells; ++i)
		if (gradient[i] > GRADIENT_UNREACHABLE)
		{
			const int cost = GRADIENT_AT_GOAL - gradient[i];
			if (cost < settled) continue;
			if (cost - settled <= gradient_kernel::MAX_STEP)
			{
				buckets[cost % BUCKETS].push(static_cast<Uint32>(i));
				++pending;
			}
			else deferredSeeds.emplace_back(cost, static_cast<Uint32>(i));
		}
	std::sort(deferredSeeds.begin(), deferredSeeds.end());
}

bool BuildingGradientSearch::resolved(std::size_t target) const
{
	assert(target < cells);
	const auto value = gradient[target];
	return complete() || value == GRADIENT_FORBIDDEN || value == GRADIENT_AT_GOAL
		|| (value > GRADIENT_UNREACHABLE && GRADIENT_AT_GOAL - value < currentCost);
}

void BuildingGradientSearch::resolve(std::size_t target, const char *caller)
{
	assert(target <= cells);
	if (complete() || (target < cells && resolved(target))) return;
	if (!sourceMap) { advance(target); return; } // Pure private worker execution.
	PERF_SCOPE_TIME(BuildingGradientResume);
	BuildingGradientDiagnostics::Scope evidence(sourceMap ? sourceMap->buildingGradientDiagnostics() : nullptr,
												buildingId, swimClass,
												target == cells ? "finish" : "resume", caller,
												sourceMap ? sourceMap->topologyGeneration : snapshotGeneration, snapshotGeneration);
	const auto before = popped;
	advance(target);
	evidence.result(popped - before, complete());
}

void BuildingGradientSearch::advance(std::size_t target)
{
	const field::Grid geometry{widthMask + 1, heightMask + 1};
	while (!complete() && (target == cells || !resolved(target)))
	{
		if (!pending && nextSeed < deferredSeeds.size()) currentCost = deferredSeeds[nextSeed].first;
		if (currentCost > costLimit)
		{
			// Seeds above the propagation cap are preserved but cannot expand.
			pending = 0;
			nextSeed = deferredSeeds.size();
			for (auto &bucket : buckets) bucket.clear();
			break;
		}
		while (nextSeed < deferredSeeds.size() && deferredSeeds[nextSeed].first == currentCost)
		{
			buckets[currentCost % BUCKETS].push(deferredSeeds[nextSeed++].second);
			++pending;
		}
		popped += buckets[currentCost % BUCKETS].size;
		if (modifiedCosts)
			gradient_kernel::expandTerrainBucket(gradient, buckets.data(), pending,
				currentCost, costLimit, geometry, gradient_kernel::TERRAIN_ENTRY_COSTS[swimClass],
				[this](std::size_t i) { return (*terrain)[i]; });
		else if (!weightedClass(swimClass))
			expandBucket<false>(gradient, buckets.data(), pending, currentCost, costLimit,
				geometry, LAND_STEPS, [](std::size_t) { return false; });
		else
			expandBucket<true>(gradient, buckets.data(), pending, currentCost, costLimit,
				geometry, entrySteps(WATER_STEP[swimClass]),
				[this](std::size_t i) { return gradient_kernel::terrainUsesSwimming((*terrain)[i]); });
		++currentCost;
	}
	if (complete()) terrain.reset();
}

std::vector<std::uint16_t> BuildingGradientSearch::completePrivateSnapshot() const
{
	if (!cells)
		return {};
	std::vector<std::uint16_t> result(gradient, gradient + cells);
	auto privateSearch = *this;
	privateSearch.gradient = result.data();
	privateSearch.advance(cells);
	return result;
}

std::size_t BuildingGradientSearch::retainedBytes() const
{
	std::size_t bytes = sizeof(*this) + deferredSeeds.capacity() * sizeof(deferredSeeds[0]);
	for (const auto &bucket : buckets)
		bytes += bucket.cells.capacity() * sizeof(bucket.cells[0]);
	return bytes;
}

void BuildingGradientSearch::clearForReuse()
{
	gradient = nullptr;
	cells = pending = nextSeed = 0;
	deferredSeeds.clear();
	sourceMap = nullptr;
	terrain.reset();
	for (auto &bucket : buckets) bucket.clear();
}

namespace
{
struct SearchPool
{
	// Covers the observed burst of detached searches while bounding retained
	// bucket storage across games and worker threads.
	static constexpr std::size_t SLOTS = 64;
	static constexpr std::size_t BYTES = 8 * 1024 * 1024;
	std::mutex mutex;
	std::array<std::unique_ptr<BuildingGradientSearch>, SLOTS> searches;
	std::size_t count = 0, bytes = 0;
};

SearchPool &searchPool()
{
	static SearchPool pool;
	return pool;
}
}

std::unique_ptr<BuildingGradientSearch> acquireBuildingGradientSearch()
{
	auto &pool = searchPool();
	{
		std::lock_guard<std::mutex> lock(pool.mutex);
		if (pool.count)
		{
			auto search = std::move(pool.searches[--pool.count]);
			pool.bytes -= search->retainedBytes();
			return search;
		}
	}
	return std::make_unique<BuildingGradientSearch>();
}

void recycleBuildingGradientSearch(std::unique_ptr<BuildingGradientSearch> search)
{
	if (!search) return;
	search->clearForReuse();
	const std::size_t bytes = search->retainedBytes();
	auto &pool = searchPool();
	std::lock_guard<std::mutex> lock(pool.mutex);
	if (pool.count < SearchPool::SLOTS && bytes <= SearchPool::BYTES - pool.bytes)
	{
		pool.bytes += bytes;
		pool.searches[pool.count++] = std::move(search);
	}
}

void clearBuildingGradientSearchPool()
{
	auto &pool = searchPool();
	std::lock_guard<std::mutex> lock(pool.mutex);
	while (pool.count) pool.searches[--pool.count].reset();
	pool.bytes = 0;
}
