// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include <PerformanceTelemetry.h>
#include "BuildingGradientSearch.h"
#include <mutex>
#include "Map.h"
#include "MapInternal.h"
#include "field/RuntimeTerrainGradient.h"

using gradient_kernel::BUCKETS;
using gradient_kernel::COST_LIMIT;
using gradient_kernel::EntrySteps;
using gradient_kernel::entrySteps;
using gradient_kernel::expandBucket;
using gradient_kernel::LAND_STEPS;
using gradient_kernel::WATER_STEP;
using gradient_kernel::weightedClass;

void BuildingGradientSearch::begin(const Map &map, std::uint16_t *seeded, int swim)
{
	gradient = seeded;
	cells = std::size_t(map.getW()) * map.getH();
	widthMask = map.getMaskW();
	heightMask = map.getMaskH();
	swimClass = swim;
	currentCost = 0;
	popped = 0;
	pending = 0;
	for (auto &bucket : buckets)
		bucket.clear();
	modifiedCosts = map.hasTerrainMovementModifiers();
	registry = map.frozenTerrainRegistry();
	terrainBuckets = map.terrainQueueBuckets();
	const bool dynamic = modifiedCosts && registry->size() > TERRAIN_COUNT;
	profiles = dynamic ? map.frozenTerrainMovementSnapshot(swim) : nullptr;
	if (dynamic)
	{
		if (!custom)
			custom = std::make_unique<TerrainGradientWorkspace>();
		custom->prepare(terrainBuckets);
		for (auto &b : custom->buckets)
			b.clear();
	}
	auto *queues = dynamic ? custom->buckets.data() : buckets.data();
	const bool weighted = weightedClass(swim) || modifiedCosts;
	water = !modifiedCosts && weighted && registry->size() > TERRAIN_COUNT
				? map.frozenWaterSnapshot()
				: nullptr;
	terrain = weighted && !dynamic && !water ? map.frozenTerrainSnapshot() : nullptr;
	// Building fields have only zero-cost seeds, so no deferred seeds are needed.
	for (std::size_t i = 0; i < cells; ++i)
	{
		const auto value = seeded[i];
		// Ordinary cells need one comparison; validate only potential seeds.
		if (value > GRADIENT_UNREACHABLE)
		{
			assert(value == GRADIENT_AT_GOAL);
			// Preserve release-build handling of an invalid non-goal seed too.
			if (value == GRADIENT_AT_GOAL)
			{
				queues[0].push(static_cast<Uint32>(i));
				++pending;
			}
		}
	}
}

bool BuildingGradientSearch::resolved(std::size_t target) const
{
	assert(target < cells);
	const auto value = gradient[target];
	return complete() || value == GRADIENT_FORBIDDEN || value == GRADIENT_AT_GOAL ||
		   (value > GRADIENT_UNREACHABLE && GRADIENT_AT_GOAL - value < currentCost);
}

void BuildingGradientSearch::resolve(std::size_t target)
{
	assert(target <= cells);
	if (complete() || (target < cells && resolved(target)))
		return;
	PERF_SCOPE_TIME(BuildingGradientResume);
	auto sweep = [&](auto weighted, EntrySteps waterSteps, auto waterAt)
	{
		while (pending && (target == cells || !resolved(target)))
		{
			assert(currentCost <= COST_LIMIT);
			popped += buckets[currentCost % BUCKETS].size;
			expandBucket<decltype(weighted)::value>(gradient, buckets.data(), pending, currentCost,
													COST_LIMIT, {widthMask + 1, heightMask + 1},
													waterSteps, waterAt);
			++currentCost;
		}
	};
	if (modifiedCosts && registry->size() > TERRAIN_COUNT)
	{
		auto run = [&]<unsigned N>()
		{
			while (pending && (target == cells || !resolved(target)))
			{
				popped += custom->buckets[unsigned(currentCost) % N].size;
				gradient_kernel::runtime_terrain::expandTerrainBucket<N, true>(
					gradient, custom->buckets.data(), pending, currentCost, COST_LIMIT,
					{widthMask + 1, heightMask + 1}, profiles->movement, *custom,
					[&](size_t i) { return profiles->cells[i]; });
				++currentCost;
			}
		};
		if (terrainBuckets == 64)
			run.template operator()<64>();
		else if (terrainBuckets == 128)
			run.template operator()<128>();
		else
			run.template operator()<256>();
	}
	else if (modifiedCosts)
	{
		const auto *types = terrain->data();
		while (pending && (target == cells || !resolved(target)))
		{
			popped += buckets[currentCost % BUCKETS].size;
			gradient_kernel::expandTerrainBucket(gradient, buckets.data(), pending, currentCost,
												 COST_LIMIT, {widthMask + 1, heightMask + 1},
												 gradient_kernel::PREPARED_TERRAIN_COSTS[swimClass],
												 [types](size_t i) { return types[i]; });
			++currentCost;
		}
	}
	else if (water)
		sweep(std::true_type(), entrySteps(WATER_STEP[swimClass]),
			  [water = water->data()](size_t i) { return water[i] != 0; });
	else if (!terrain)
		sweep(std::false_type(), LAND_STEPS, [](size_t) { return false; });
	else
	{
		const auto *const terrainCells = terrain->data();
		if (registry->size() == TERRAIN_COUNT)
			sweep(std::true_type(), entrySteps(WATER_STEP[swimClass]), [terrainCells](size_t i)
				  { return gradient_kernel::terrainUsesSwimming(terrainCells[i]); });
		else
		{
			const int sole = registry->soleSwimmingType();
			if (sole >= 0)
				sweep(std::true_type(), entrySteps(WATER_STEP[swimClass]),
					  [&](size_t i) { return terrainCells[i] == sole; });
			else
				sweep(std::true_type(), entrySteps(WATER_STEP[swimClass]),
					  [&](size_t i) { return registry->swimming(terrainCells[i]); });
		}
	}
	if (complete())
	{
		terrain.reset();
		profiles.reset();
		water.reset();
	}
}

std::size_t BuildingGradientSearch::retainedBytes() const
{
	std::size_t bytes = sizeof(*this);
	for (const auto &bucket : buckets)
		bytes += bucket.cells.capacity() * sizeof(bucket.cells[0]);
	if (custom)
	{
		bytes += sizeof(*custom) + custom->buckets.capacity() * sizeof(GradientBucket);
		for (const auto &b : custom->buckets)
			bytes += b.cells.capacity() * sizeof(std::uint32_t);
	}
	return bytes;
}

void BuildingGradientSearch::clearForReuse()
{
	gradient = nullptr;
	cells = pending = 0;
	terrain.reset();
	registry.reset();
	profiles.reset();
	water.reset();
	if (custom)
		for (auto &b : custom->buckets)
			b.clear();
	for (auto &bucket : buckets)
		bucket.clear();
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
} // namespace

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
	if (!search)
		return;
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
	while (pool.count)
		pool.searches[--pool.count].reset();
	pool.bytes = 0;
}
