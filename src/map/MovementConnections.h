// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MapStateView.h"
#include "MovementConnectionCache.h"
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
	return grid.withIncomingConnections(
		v.movementConnections
			? v.movementConnections->incoming(v, movement, team, interactionEndpoints)
			: compileMovementConnections(v, movement, team, interactionEndpoints));
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
