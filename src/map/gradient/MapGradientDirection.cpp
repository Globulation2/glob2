// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "MapInternal.h"
#include "Utilities.h"
#include "kernel/GradientCosts.h"

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

int Map::minStepCost(int swimClass)
{
	if (swimClass > 0 && WATER_STEP[swimClass] < GRADIENT_STEP)
		return WATER_STEP[swimClass];
	return GRADIENT_STEP;
}

int Map::stepCost(int dx, int dy, size_t targetIndex, int swimClass) const
{
	int step = GRADIENT_STEP;
	if (swimClass > 0 && isWater((unsigned)targetIndex))
		step = WATER_STEP[swimClass];
	if (dx != 0 && dy != 0)
		step = step * GRADIENT_DIAGONAL_STEP / GRADIENT_STEP;
	return step;
}

bool Map::directionByGradient(Uint32 teamMask, int swimClass, int x, int y, const Uint16 *gradient, int *dx, int *dy, bool strict) const
{
	PERF_SCOPE_TIME(PathDirection);
	const bool canSwim = swimClass > 0;
	Uint16 here = gradient[coordToIndex(x, y)];
	// Settling the whole cost layer of `here` also finalizes every better or
	// equal-valued neighbor inspected below; worse neighbors cannot be selected.
	if (here <= GRADIENT_UNREACHABLE)
		return false;
	if (here == GRADIENT_AT_GOAL)
	{
		*dx = 0;
		*dy = 0;
		return true;
	}
	int best = -1;
	int bestD = -1;
	int sidesteps[8];
	int sidestepCount = 0;
	for (int d = 0; d < 8; d++)
	{
		int ddx = tabClose[d][0];
		int ddy = tabClose[d][1];
		size_t n = coordToIndex(x + ddx, y + ddy);
		Uint16 g = gradient[n];
		if (g <= GRADIENT_UNREACHABLE || !isFreeForGroundUnit(x + ddx, y + ddy, canSwim, teamMask))
			continue;
		if (g > here)
		{
			// Worth of going through n: its value less the step to get there.
			int score = g - stepCost(ddx, ddy, n, swimClass);
			if (score > best)
			{
				best = score;
				bestD = d;
			}
		}
		else if (g == here)
			sidesteps[sidestepCount++] = d;
	}
	if (bestD >= 0)
	{
		*dx = tabClose[bestD][0];
		*dy = tabClose[bestD][1];
		return true;
	}
	if (strict || sidestepCount == 0)
		return false;
	// Blocked: sidestep to a random neighbour no farther from the goal, so two
	// units blocking each other do not mirror each other forever. syncRand
	// keeps the choice deterministic.
	int pick = sidesteps[syncRand() % sidestepCount];
	*dx = tabClose[pick][0];
	*dy = tabClose[pick][1];
	return true;
}
