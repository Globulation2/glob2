// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TerrainGradient.h"
#include "TerrainGradientWorkspace.h"
#include "map/TerrainRegistry.h"

namespace gradient_kernel::runtime_terrain
{
template <unsigned Buckets, bool DirectProfiles = false, class TerrainAt>
void expandTerrainBucket(std::uint16_t *gradient, GradientBucket *queue, std::size_t &pending,
						 int cur, int limit, const field::Grid &grid,
						 const TerrainRegistry::Movement &movement, TerrainGradientWorkspace &,
						 TerrainAt terrainAt)
{
	const auto classAt = [&](std::size_t i)
	{ return DirectProfiles ? unsigned(terrainAt(i)) : movement.profileIds[terrainAt(i)]; };
	std::visit(
		[&](const auto &profile)
		{
			expandPreparedTerrainBucket<Buckets>(gradient, queue, pending, cur, limit, grid,
												 profile, classAt);
		},
		*movement.prepared);
}

template <unsigned Buckets, bool DirectProfiles = false, class TerrainAt>
void propagate(std::uint16_t *gradient, int swim, int maxCost, field::Grid grid,
			   GradientWorkspace &workspace, TerrainAt terrainAt,
			   const TerrainRegistry::Movement &movement)
{
	if (!workspace.terrain)
		workspace.terrain = std::make_unique<TerrainGradientWorkspace>();
	auto &scratch = *workspace.terrain;
	scratch.prepare(Buckets);
	for (auto &bucket : scratch.buckets)
		bucket.clear();
	auto *buckets = scratch.buckets.data();
	auto &deferred = workspace.deferredSeeds;
	deferred.clear();
	std::size_t pending = 0;
	const auto classAt = [&](std::size_t i)
	{ return DirectProfiles ? unsigned(terrainAt(i)) : movement.profileIds[terrainAt(i)]; };
	auto enqueue = [&](std::size_t i)
	{
		if (gradient[i] <= GRADIENT_UNREACHABLE)
			return;
		const int cost = GRADIENT_AT_GOAL - gradient[i];
		if (cost < int(Buckets))
		{
			buckets[cost].push(i);
			++pending;
		}
		else
			deferred.push_back({cost, int(i)});
	};
	// Prove uniformity across all passable cells, not just seeds or popped cells.
	unsigned uniformClass = 256;
	std::size_t i = 0;
	for (; i < grid.cells(); ++i)
	{
		if (gradient[i] == GRADIENT_FORBIDDEN)
			continue;
		const auto c = classAt(i);
		if (uniformClass == 256)
			uniformClass = c;
		else if (uniformClass != c)
			break;
		enqueue(i);
	}
	const bool uniform = i == grid.cells();
	for (; i < grid.cells(); ++i)
		enqueue(i);
	std::sort(deferred.begin(), deferred.end());
	const int limit = std::min(maxCost, COST_LIMIT);
	auto sweep = [&](const auto &prepared, auto selectedClassAt)
	{
		std::size_t next = 0;
		for (int cur = 0; (pending || next < deferred.size()) && cur <= limit; ++cur)
		{
			if (!pending)
				cur = deferred[next].first;
			for (; next < deferred.size() && deferred[next].first == cur; ++next)
			{
				buckets[unsigned(cur) % Buckets].push(deferred[next].second);
				++pending;
			}
			expandPreparedTerrainBucket<Buckets>(gradient, buckets, pending, cur, limit, grid,
												 prepared, selectedClassAt);
		}
	};
	if (uniform && uniformClass < movement.profiles.size())
	{
		const PreparedTerrainCosts<1> single(
			std::array<EntrySteps, 1>{movement.profiles[uniformClass]});
		sweep(single, [](std::size_t) { return 0; });
	}
	else
		std::visit([&](const auto &prepared) { sweep(prepared, classAt); }, *movement.prepared);
}
} // namespace gradient_kernel::runtime_terrain
namespace gradient_kernel
{
inline void propagateTerrainProfiles(std::uint16_t *gradient, int swim, int maxCost,
									 field::Grid grid, GradientWorkspace &workspace,
									 const std::uint8_t *profiles,
									 const TerrainRegistry::Movement &movement, unsigned buckets)
{
	const auto at = [profiles](std::size_t i) { return profiles[i]; };
	if (buckets == 64)
		runtime_terrain::propagate<64, true>(gradient, swim, maxCost, grid, workspace, at,
											 movement);
	else if (buckets == 128)
		runtime_terrain::propagate<128, true>(gradient, swim, maxCost, grid, workspace, at,
											  movement);
	else
		runtime_terrain::propagate<256, true>(gradient, swim, maxCost, grid, workspace, at,
											  movement);
}
inline void propagateTerrainProfiles(std::uint16_t *gradient, int swim, int maxCost,
									 field::Grid grid, GradientWorkspace &workspace,
									 const std::uint8_t *profiles, const TerrainRegistry &registry,
									 unsigned buckets)
{
	propagateTerrainProfiles(gradient, swim, maxCost, grid, workspace, profiles,
							 registry.movement(swim), buckets);
}
template <class TerrainAt>
void propagateTerrainField(std::uint16_t *gradient, int swim, int maxCost, field::Grid grid,
						   GradientWorkspace &workspace, TerrainAt terrainAt, bool modifiedCosts,
						   const TerrainRegistry &registry, unsigned buckets)
{
	if (registry.size() == TERRAIN_COUNT)
	{
		propagateTerrainField(gradient, swim, maxCost, grid, workspace, terrainAt, modifiedCosts);
		return;
	}
	if (!modifiedCosts)
	{
		propagateField(gradient, swim, maxCost, grid, workspace,
					   [&](std::size_t i) { return registry.swimming(terrainAt(i)); });
		return;
	}
	if (buckets == 64)
		runtime_terrain::propagate<64>(gradient, swim, maxCost, grid, workspace, terrainAt,
									   registry.movement(swim));
	else if (buckets == 128)
		runtime_terrain::propagate<128>(gradient, swim, maxCost, grid, workspace, terrainAt,
										registry.movement(swim));
	else
		runtime_terrain::propagate<256>(gradient, swim, maxCost, grid, workspace, terrainAt,
										registry.movement(swim));
}
} // namespace gradient_kernel
