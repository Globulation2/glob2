// SPDX-License-Identifier: GPL-3.0-or-later
#include "MovementConnectionCache.h"
#include "MapStateView.h"
#include "field/Grid.h"
#include <algorithm>

namespace MapState
{
namespace
{
unsigned profileFor(int movement)
{
	return swimAccessVariant(movement);
}

ConnectionMasks compileTerrainConnections(const View &v, int movement)
{
	auto result = std::make_shared<std::vector<std::uint8_t>>(std::size_t(v.width) * v.height, 0);
	for (int y = 0; y < v.height; ++y)
		for (int x = 0; x < v.width; ++x)
			for (unsigned d = 0; d < field::Surrounding.size(); ++d)
			{
				const auto [dx, dy] = field::Surrounding[d];
				if (terrainAllowsMovement(v, v.index(x + dx, y + dy), movement) &&
					terrainConnection(v, x, y, dx, dy, movement) &&
					(movement == AIR_MOVEMENT_CLASS || !dx || !dy ||
					 (terrainAllowsMovement(v, v.index(x + dx, y), movement) &&
					  terrainAllowsMovement(v, v.index(x, y + dy), movement))))
					(*result)[v.index(x, y)] |= std::uint8_t(1u << d);
			}
	return result;
}
} // namespace

ConnectionMasks TerrainConnectionCache::outgoing(const View &v, int movement) const
{
	if (movement == AIR_MOVEMENT_CLASS)
		return compileTerrainConnections(v, movement);
	std::lock_guard lock(mutex);
	auto &result = masks[profileFor(movement)];
	if (!result)
		result = compileTerrainConnections(v, movement);
	return result;
}

void TerrainConnectionCache::clear()
{
	std::lock_guard lock(mutex);
	masks = {};
}

ConnectionMasks terrainConnectionMasks(const View &v, int movement)
{
	return v.terrainConnections ? v.terrainConnections->outgoing(v, movement)
								: compileTerrainConnections(v, movement);
}

ConnectionMasks compileMovementConnections(const View &v, int movement, Uint32 team,
										   bool interactionEndpoints)
{
	const field::Grid grid(v.width, v.height);
	if (movement == AIR_MOVEMENT_CLASS)
		return std::make_shared<const std::vector<std::uint8_t>>(grid.cells(), 255);
	constexpr std::uint8_t TerrainAccess = 1, PhysicalPassage = 2;
	std::vector<std::uint8_t> passable(grid.cells());
	for (std::size_t i = 0; i < grid.cells(); ++i)
	{
		const bool access = terrainAllowsMovement(v, i, movement);
		const auto resource = v.resources[i].resource.type;
		const bool passage =
			access && v.occupancy[i].building == NOGBID &&
			(resource == NO_RES_TYPE || !v.resourceProperties(resource).blocksGround) &&
			!(v.areas[i].forbidden & team);
		passable[i] = (access ? TerrainAccess : 0) | (passage ? PhysicalPassage : 0);
	}
	// Cached outgoing terrain masks include destination and terrain-side access.
	// Without an epoch, retain the direct query for small standalone views.
	const auto terrain =
		v.terrainConnections ? v.terrainConnections->outgoing(v, movement) : ConnectionMasks{};
	auto incoming = std::make_shared<std::vector<std::uint8_t>>(grid.cells(), 0);
	for (int y = 0; y < v.height; ++y)
		for (int x = 0; x < v.width; ++x)
		{
			const auto i = v.index(x, y);
			const bool interaction =
				interactionEndpoints && resourceInteractionEndpoint(v, i, movement);
			if (!(passable[i] & TerrainAccess) && !interaction)
				continue;
			for (unsigned d = 0; d < field::Surrounding.size(); ++d)
			{
				const auto [dx, dy] = field::Surrounding[d];
				const auto predecessor = v.index(x + dx, y + dy);
				if (!(passable[predecessor] & TerrainAccess))
					continue;
				// A gathering sink retains adjacent, nonmoving interaction range.
				if (interaction)
				{
					(*incoming)[i] |= std::uint8_t(1u << d);
					continue;
				}
				// Otherwise test the actual forward edge from the predecessor.
				const bool connected =
					terrain
						? ((*terrain)[predecessor] & (1u << field::directionIndex(-dx, -dy))) != 0
						: terrainConnection(v, x + dx, y + dy, -dx, -dy, movement);
				const bool sidesClear = !dx || !dy ||
										((passable[v.index(x + dx, y)] & PhysicalPassage) &&
										 (passable[v.index(x, y + dy)] & PhysicalPassage));
				if (connected && sidesClear)
					(*incoming)[i] |= std::uint8_t(1u << d);
			}
		}
	return incoming;
}

ConnectionMasks MovementConnectionCache::incoming(const View &v, int movement, Uint32 team,
												  bool interactionEndpoints) const
{
	if (movement == AIR_MOVEMENT_CLASS)
		return compileMovementConnections(v, movement, team, interactionEndpoints);
	std::lock_guard lock(mutex);
	const auto profile = profileFor(movement);
	for (auto &entry : entries)
		if (unsigned(entry.profile) == profile && entry.team == team &&
			entry.interactions == interactionEndpoints)
		{
			++counters.hits;
			entry.used = ++clock;
			return entry.masks;
		}
	auto result = compileMovementConnections(v, movement, team, interactionEndpoints);
	++counters.builds;
	const auto bytes = result->capacity();
	if (bytes <= budget)
	{
		while ((counters.retainedBytes > budget - bytes || entries.size() >= MaximumEntries) &&
			   !entries.empty())
		{
			const auto oldest =
				std::min_element(entries.begin(), entries.end(),
								 [](const Entry &a, const Entry &b) { return a.used < b.used; });
			counters.retainedBytes -= oldest->masks->capacity();
			entries.erase(oldest);
		}
		entries.push_back({int(profile), team, interactionEndpoints, result, ++clock});
		counters.retainedBytes += bytes;
	}
	return result;
}

MovementConnectionCache::Stats MovementConnectionCache::stats() const
{
	std::lock_guard lock(mutex);
	auto result = counters;
	result.entries = entries.size();
	return result;
}

void MovementConnectionCache::clear()
{
	std::lock_guard lock(mutex);
	entries.clear();
	counters.retainedBytes = 0;
	clock = 0;
}
} // namespace MapState
