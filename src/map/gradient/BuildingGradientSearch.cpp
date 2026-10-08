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

BuildingGradientSearch::Inputs BuildingGradientSearch::Inputs::of(const Map &map, int swim)
{
	Inputs inputs;
	inputs.modifiedCosts = map.hasTerrainMovementModifiers();
	inputs.registry = map.frozenTerrainRegistry();
	inputs.buckets = map.terrainQueueBuckets();
	// Modified costs follow the map's compact cell profiles; otherwise only
	// weighted swimmers need to know which cells are water.
	if (inputs.modifiedCosts)
		inputs.profiles = map.frozenTerrainMovementSnapshot(swim);
	else if (weightedClass(swim))
		inputs.water = map.frozenWaterSnapshot();
	return inputs;
}

void BuildingGradientSearch::begin(const Map &map, std::uint16_t *seeded, int swim)
{
	begin(Inputs::of(map, swim), seeded, swim, map.getW(), map.getH());
}

void BuildingGradientSearch::begin(const Inputs &inputs, std::uint16_t *seeded, int swim,
								   int width, int height)
{
	assert(width > 0 && height > 0 && !(width & (width - 1)) && !(height & (height - 1)));
	gradient = seeded;
	cells = std::size_t(width) * height;
	widthMask = width - 1;
	heightMask = height - 1;
	swimClass = swim;
	currentCost = readerCost = 0;
	popped = 0;
	pending = 0;
	queries = extensions = 0;
	poppedAtDepth.fill(0);
	for (auto &bucket : buckets)
		bucket.clear();
	modifiedCosts = inputs.modifiedCosts;
	registry = inputs.registry;
	terrainBuckets = inputs.buckets;
	const bool dynamic = modifiedCosts;
	profiles = dynamic ? inputs.profiles : nullptr;
	if (dynamic)
	{
		gradient_kernel::runtime_terrain::validateQueue(profiles->movement, terrainBuckets);
		if (!custom)
			custom = std::make_unique<TerrainGradientWorkspace>();
		custom->prepare(terrainBuckets);
		for (auto &b : custom->buckets)
			b.clear();
	}
	auto *queues = dynamic ? custom->buckets.data() : buckets.data();
	water = !modifiedCosts && weightedClass(swim) ? inputs.water : nullptr;
	assert(!dynamic || profiles);
	assert(dynamic || !weightedClass(swim) || water);
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
	++queries;
	if (!complete() && !(target < cells && resolved(target)))
	{
		++extensions;
		PERF_SCOPE_TIME(BuildingGradientResume);
		advance([&] { return target != cells && resolved(target); });
	}
	if (target < cells)
	{
		const auto value = gradient[target];
		// A reachable query requires its whole cost layer, even if preparation
		// already settled it. An unreachable query needs search exhaustion.
		// Goals and blocked cells are known from seeds without propagation.
		const int needed = value == GRADIENT_UNREACHABLE ? currentCost
			: (value > GRADIENT_UNREACHABLE && value != GRADIENT_AT_GOAL
				? GRADIENT_AT_GOAL - value + 1 : 0);
		readerCost = std::max(readerCost, needed);
	}
}

void BuildingGradientSearch::resolveToCost(int cost)
{
	// Owner-free: workers call this, so it records no telemetry scope.
	if (complete() || currentCost > cost)
		return;
	advance([&] { return currentCost > cost; });
}

// Expands one whole cost layer per iteration until done() or the queues drain.
template <class Done> void BuildingGradientSearch::advance(Done done)
{
	auto sweep = [&](auto weighted, EntrySteps waterSteps, auto waterAt)
	{
		while (pending && !done())
		{
			assert(currentCost <= COST_LIMIT);
			popped += buckets[currentCost % BUCKETS].size;
			poppedAtDepth[depthBin(currentCost)] += buckets[currentCost % BUCKETS].size;
			expandBucket<decltype(weighted)::value>(gradient, buckets.data(), pending, currentCost,
													COST_LIMIT, {widthMask + 1, heightMask + 1},
													waterSteps, waterAt);
			++currentCost;
		}
	};
	if (modifiedCosts)
	{
		auto run = [&]<unsigned N>()
		{
			while (pending && !done())
			{
				popped += custom->buckets[unsigned(currentCost) % N].size;
				poppedAtDepth[depthBin(currentCost)] += custom->buckets[unsigned(currentCost) % N].size;
				gradient_kernel::runtime_terrain::expandProfileBucket<N>(
					gradient, custom->buckets.data(), pending, currentCost, COST_LIMIT,
					{widthMask + 1, heightMask + 1}, profiles->movement,
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
	else if (water)
		sweep(std::true_type(), entrySteps(WATER_STEP[swimClass]),
			  [water = water->data()](size_t i) { return water[i] != 0; });
	else
		sweep(std::false_type(), LAND_STEPS, [](size_t) { return false; });
	if (complete())
	{
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
	currentCost = readerCost = 0;
	popped = queries = extensions = 0;
	poppedAtDepth.fill(0);
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
