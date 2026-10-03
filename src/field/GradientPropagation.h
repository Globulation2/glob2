// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Eager field construction from caller-owned seeds, terrain and scratch. No Map
// state is read here; immediate and pipelined callers supply their own terrain
// lookup, and both use the same relaxation kernel as resumed building searches.
// The field and workspace must belong to this call alone. The terrain lookup
// must remain stable until the call finishes; pipelined jobs use a water snapshot.
#include "GradientRelaxation.h"
#include "GradientWorkspace.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

namespace gradient_kernel
{
// Seeds are already encoded as GRADIENT_AT_GOAL - starting cost. Values 0 and 1
// are forbidden and unreachable cells, respectively. The caller reseeds before
// every run; a completed field cannot be reused as its own seed buffer.
// The cap limits propagation, not the supplied seed values. Preserve those
// values even when a deferred seed lies beyond the last expandable cost layer.
template<class IsWater>
void propagateField(std::uint16_t *gradient, int swimClass, int maxCost,
	field::Grid geometry, GradientWorkspace &workspace, IsWater isWater)
{
	auto *buckets = workspace.buckets.data();
	auto &deferredSeeds = workspace.deferredSeeds;
	static_assert(std::tuple_size<decltype(workspace.buckets)>::value == BUCKETS);
	const int limit = std::min(maxCost, COST_LIMIT);
	for (auto &bucket : workspace.buckets)
		bucket.clear();
	deferredSeeds.clear();
	std::size_t pending = 0;
	// Seeds beyond the largest edge cost cannot enter the initial ring without
	// aliasing a cheaper layer. Hold them sorted until the sweep reaches them.
	for (std::size_t i = 0; i < geometry.cells(); i++)
		if (gradient[i] > GRADIENT_UNREACHABLE)
		{
			int cost = GRADIENT_AT_GOAL - gradient[i];
			if (cost <= MAX_STEP)
			{
				buckets[unsigned(cost) % BUCKETS].push(std::uint32_t(i));
				pending++;
			}
			else
				deferredSeeds.push_back({cost, (int)i});
		}
	std::sort(deferredSeeds.begin(), deferredSeeds.end());
	// Class zero never enters water; class EVEN pays the land rate there.
	// Specializing this common case skips the per-cell terrain lookup.
	auto sweep = [&](auto weighted, EntrySteps waterSteps, auto waterAt)
	{
		size_t nextSeed = 0;
		for (int cur = 0; (pending > 0 || nextSeed < deferredSeeds.size()) && cur <= limit; cur++)
		{
			if (pending == 0)
				cur = deferredSeeds[nextSeed].first; // the queue is empty: jump to the next seed
			for (; nextSeed < deferredSeeds.size() && deferredSeeds[nextSeed].first == cur; nextSeed++)
			{
				buckets[unsigned(cur) % BUCKETS].push(std::uint32_t(deferredSeeds[nextSeed].second));
				pending++;
			}
			expandBucket<decltype(weighted)::value>(gradient, buckets, pending, cur, limit,
				geometry, waterSteps, waterAt);
		}
	};
	if (!weightedClass(swimClass))
		sweep(std::false_type(), LAND_STEPS, [](std::size_t) { return false; });
	else
	{
		const EntrySteps waterSteps = entrySteps(WATER_STEP[swimClass]);
		sweep(std::true_type(), waterSteps, isWater);
	}
}
}
