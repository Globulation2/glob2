// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "MapInternal.h"
#include "Utilities.h"
#include "field/TerrainMovementCosts.h"

#include <cstdlib>

using gradient_kernel::WATER_STEP;
static_assert(WATER_STEP[Map::SWIM_CLASS_EVEN] == GRADIENT_STEP);


int Map::swimClass(int walkSpeed, int swimSpeed)
{
	if (swimSpeed <= 0)
		return 0;
	if (walkSpeed <= 0)
		return SWIM_CLASS_COUNT - 1;
	// A water cell should cost walk/swim land cells: pick the closest class.
	int wanted = (GRADIENT_STEP * walkSpeed + swimSpeed / 2) / swimSpeed;
	int best = 1;
	for (int c = 2; c < SWIM_CLASS_COUNT; c++)
		if (std::abs(WATER_STEP[c] - wanted) < std::abs(WATER_STEP[best] - wanted))
			best = c;
	return best;
}

int Map::minStepCost(int swimClass) const
{
	return terrainMinimumGround[swimClass];
}

int Map::stepCost(int dx, int dy, size_t targetIndex, int swimClass) const
{
	const auto cost = terrainRegistry().movement(swimClass).entries[terrainTypeAt(targetIndex)];
	return dx != 0 && dy != 0 ? cost.diagonal : cost.cardinal;
}

Map::GradientDirectionDecision Map::evaluateGradientDirection(Uint32 teamMask, int swimClass, int x,
															  int y, const Uint16 *gradient,
															  bool strict,
															  Uint32 guardAreaMask) const
{
	GradientDirectionDecision decision;
	const auto here = gradient[coordToIndex(x, y)];
	if (here <= GRADIENT_UNREACHABLE)
		return decision;
	if (here == GRADIENT_AT_GOAL)
	{
		decision.atGoal = true;
		return decision;
	}
	int best = -1;
	for (int d = 0; d < 8; ++d)
	{
		const auto nx = x + tabClose[d][0], ny = y + tabClose[d][1];
		const auto n = coordToIndex(nx, ny);
		const auto g = gradient[n];
		if (g <= GRADIENT_UNREACHABLE || !isFreeForGroundUnit(nx, ny, swimClass > 0, teamMask))
			continue;
		if (guardAreaMask && !(tiles[n].guardArea & guardAreaMask))
			continue;
		if (g > here)
		{
			const int score = g - stepCost(tabClose[d][0], tabClose[d][1], n, swimClass);
			if (score > best)
			{
				best = score;
				decision.best = d;
			}
		}
		else if (!strict && g == here)
			decision.sidesteps[decision.count++] = d;
	}
	return decision;
}

bool Map::directionByGradient(Uint32 teamMask, int swimClass, int x, int y, const Uint16 *gradient,
							  int *dx, int *dy, bool strict, Uint32 guardAreaMask) const
{
	PERF_SCOPE_TIME(PathDirection);
	const auto decision =
		evaluateGradientDirection(teamMask, swimClass, x, y, gradient, strict, guardAreaMask);
	if (decision.atGoal)
	{
		*dx = 0;
		*dy = 0;
		return true;
	}
	int d = decision.best;
	if (d < 0)
	{
		if (!decision.count)
			return false;
		d = decision.sidesteps[syncRand() % decision.count];
	}
	*dx = tabClose[d][0];
	*dy = tabClose[d][1];
	return true;
}
