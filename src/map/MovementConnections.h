// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include "field/Grid.h"
#include <limits>

namespace MapState
{
// Searches retain frozen connections; transient units are encoded only in seeds.
// Resource fields may expose adjacent gathering goals as sinks. Callers compiling
// physical reachability pass false; interaction goals never grant transit access.
inline field::Grid movementGrid(const View &v, int movement, Uint32 team,
								bool interactionEndpoints = true)
{
	field::Grid grid(v.width, v.height);
	if (movement == AIR_MOVEMENT_CLASS)
		return grid;
	constexpr std::uint8_t TerrainAccess = 1, PhysicalPassage = 2;
	std::vector<std::uint8_t> passable(grid.cells());
	for (int y = 0; y < v.height; ++y)
		for (int x = 0; x < v.width; ++x)
			passable[v.index(x, y)] =
				(terrainAllowsMovement(v, v.index(x, y), movement) ? TerrainAccess : 0) |
				(groundPassage(v, x, y, movement, team) ? PhysicalPassage : 0);
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
				if (!(passable[v.index(x + dx, y + dy)] & TerrainAccess))
					continue;
				// Reverse propagation checks the forward connection from predecessor
				// to destination. Gathering is nonmoving and retains adjacent range.
				const bool connected = terrainConnection(v, x + dx, y + dy, -dx, -dy, movement);
				const bool sidesClear = !dx || !dy ||
										((passable[v.index(x + dx, y)] & PhysicalPassage) &&
										 (passable[v.index(x, y + dy)] & PhysicalPassage));
				if (interaction || (connected && sidesClear))
					(*incoming)[i] |= 1u << d;
			}
		}
	return grid.withIncomingConnections(std::move(incoming));
}

// Ascent follows the same edges as reverse propagation. Field seeds encode
// destination occupancy, including resource/building interaction endpoints.
inline bool gradientStep(const View &view, int x, int y, int dx, int dy, int movement, Uint32 team)
{
	return movement == AIR_MOVEMENT_CLASS ||
		   (terrainAllowsMovement(view, view.index(x + dx, y + dy), movement) &&
			groundSidesClear(view, x, y, dx, dy, movement, team));
}
// A seeded resource goal is an interaction endpoint, rather than a step onto
// its terrain. Ordinary ascent must follow the same physical edge as propagation.
template <class T>
bool gradientAscentStep(const View &view, int x, int y, int dx, int dy, int movement, Uint32 team,
						T value)
{
	return (value == std::numeric_limits<T>::max() &&
			resourceInteractionEndpoint(view, view.index(x + dx, y + dy), movement)) ||
		   gradientStep(view, x, y, dx, dy, movement, team);
}
inline constexpr field::Offset AscentDirections[] = {{0, -1},  {1, 0},  {0, 1}, {-1, 0},
													 {-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
template <class T>
bool gradientDestination(const View &view, const T *values, int x, int y, Sint32 *targetX,
						 Sint32 *targetY, int movement, Uint32 team)
{
	int vx = view.normalizeX(x), vy = view.normalizeY(y);
	T strongest = values[view.index(vx, vy)];
	while (true)
	{
		int dx = 0, dy = 0;
		bool found = false;
		for (const auto step : AscentDirections)
		{
			const T candidate = values[view.index(vx + step.x, vy + step.y)];
			if (candidate > strongest &&
				gradientAscentStep(view, vx, vy, step.x, step.y, movement, team, candidate))
			{
				strongest = candidate;
				dx = step.x;
				dy = step.y;
				found = true;
			}
		}
		vx = view.normalizeX(vx + dx);
		vy = view.normalizeY(vy + dy);
		if (!found || strongest == std::numeric_limits<T>::max())
			break;
	}
	*targetX = vx;
	*targetY = vy;
	return strongest == std::numeric_limits<T>::max();
}
template <class T>
bool gradientPeak(const View &view, const T *values, int x, int y, int movement, Uint32 team)
{
	const T here = values[view.index(x, y)];
	for (const auto step : AscentDirections)
		if (values[view.index(x + step.x, y + step.y)] > here &&
			gradientAscentStep(view, x, y, step.x, step.y, movement, team,
							   values[view.index(x + step.x, y + step.y)]))
			return false;
	return true;
}

} // namespace MapState
