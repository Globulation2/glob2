// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Eager field construction from caller-owned seeds, terrain and scratch. No Map
// state is read here; immediate and pipelined callers supply their own terrain
// lookup, and both use the same relaxation kernel as resumed building searches.
// The field and workspace must belong to this call alone. The terrain lookup
// must remain stable until the call finishes; pipelined jobs use a water snapshot.
#include "GradientRelaxation.h"
#include "GradientWorkspace.h"
#include "GradientBackend.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace gradient_kernel
{
// Development ablation, fixed for the process. Selection is once per eager
// field; the default specialization retains the original cost-layer loop.
inline bool parseClassicEmptyLayerSkip(const char* value)
{
    if(!value || !*value)return false;
    if((value[0]=='0'||value[0]=='1') && value[1]=='\0')return value[0]=='1';
    throw std::invalid_argument("GLOB2_GRADIENT_SKIP_EMPTY_CLASSIC must be 0 or 1");
}
inline bool requestedClassicEmptyLayerSkip()
{
    static const bool enabled=parseClassicEmptyLayerSkip(std::getenv("GLOB2_GRADIENT_SKIP_EMPTY_CLASSIC"));
    return enabled;
}
// Seeds are already encoded as GRADIENT_AT_GOAL - starting cost. Values 0 and 1
// are forbidden and unreachable cells, respectively. The caller reseeds before
// every run; a completed field cannot be reused as its own seed buffer.
// The cap limits propagation, not the supplied seed values. Preserve those
// values even when a deferred seed lies beyond the last expandable cost layer.
template<bool SkipEmptyClassic, class IsWater>
void propagateFieldCPUVariant(std::uint16_t *gradient, int swimClass, int maxCost,
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
			// Skip only setup/expansion. Keep cur increments and seed insertion
			// identical, including originals outside the propagation cap.
			if constexpr (SkipEmptyClassic)
				if (buckets[unsigned(cur) % BUCKETS].size == 0) continue;
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
template<class IsWater>
void propagateFieldCPU(std::uint16_t *gradient, int swimClass, int maxCost,
    field::Grid geometry, GradientWorkspace &workspace, IsWater isWater)
{
    if(requestedClassicEmptyLayerSkip())
        propagateFieldCPUVariant<true>(gradient,swimClass,maxCost,geometry,workspace,isWater);
    else
        propagateFieldCPUVariant<false>(gradient,swimClass,maxCost,geometry,workspace,isWater);
}
// Same entry point for CPU and optional exact full-field accelerators. Resumed
// searches still use expandBucket, since their completed-layer state is observable.
template<class IsWater>
void propagateField(std::uint16_t* gradient, int swimClass, int maxCost,
                    field::Grid grid, GradientWorkspace& workspace, IsWater isWater, CostIdentity identity = {})
{
    const auto costs = [&](std::size_t i) {
        return weightedClass(swimClass) && isWater(i)
            ? entrySteps(WATER_STEP[swimClass]) : LAND_STEPS;
    };
    const auto cpu = [&](std::uint16_t* out) {
        propagateFieldCPU(out, swimClass, maxCost, grid, workspace, isWater);
    };
    if (!tryAcceleratedGradient(gradient, maxCost, grid, *workspace.backendSession, costs, cpu, std::move(identity), workspace.family)) cpu(gradient);
}

}
