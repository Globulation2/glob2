// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "gradient/MapRouting.h"
#include "gradient/GradientRuntime.h"
#include "Game.h"
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
		return WATER_ONLY_CLASS;
	// A water cell should cost walk/swim land cells: pick the closest class.
	int wanted = (GRADIENT_STEP * walkSpeed + swimSpeed / 2) / swimSpeed;
	int best = 1;
	for (int c = 2; c < LEGACY_SWIM_CLASS_COUNT; c++)
		if (std::abs(WATER_STEP[c] - wanted) < std::abs(WATER_STEP[best] - wanted))
			best = c;
	return best;
}

int MapRouting::stepCost(int dx, int dy, size_t targetIndex, int swimClass) const
{
	const auto cost = map.cellRule(targetIndex).ground[swimClass];
	return dx != 0 && dy != 0 ? cost.diagonal : cost.cardinal;
}

bool MapRouting::directionByGradient(EntityRandom& random, Uint32 teamMask, MovementAccess movementAccessValue, int x, int y, const Uint16 *gradient, int *dx, int *dy, bool strict, Uint32 guardAreaMask) const
{
    int swimClass=accessClass(movementAccessValue);
	PERF_SCOPE_TIME(PathDirection);
	Uint16 here = gradient[map.coordToIndex(x, y)];
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
    const int costClass=publishedCostClass(gradient,swimClass);
	int best = -1;
	int bestD = -1;
	int sidesteps[8];
	int sidestepCount = 0;
	for (int d = 0; d < 8; d++)
	{
		int ddx = tabClose[d][0];
		int ddy = tabClose[d][1];
		size_t n = map.coordToIndex(x + ddx, y + ddy);
		Uint16 g = gradient[n];
        if (g <= GRADIENT_UNREACHABLE) continue;
		if (!map.isFreeForMovement(x+ddx,y+ddy,swimClass,teamMask)) continue;
		if (guardAreaMask && !(map.areaCells[n].guard & guardAreaMask))
			continue;
		if (g > here)
		{
			// Worth of going through n: its value less the step to get there.
			int score = g - stepCost(ddx, ddy, n, costClass);
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
	// units blocking each other do not mirror each other forever. The caller's private stream
	// keeps the choice deterministic.
	int pick = sidesteps[random.nextU32() % sidestepCount];
	*dx = tabClose[pick][0];
	*dy = tabClose[pick][1];
	return true;
}

int MapRouting::preparationCostClass(int team,int access) const
{
    if(access!=int(MovementAccess::Amphibious) || !map.game || team<0 || team>=map.game->teamsCount())return access;
    const auto& state=map.game->teams[team]->routingRatio;
    return state.known ? int(state.selected) : Map::SWIM_CLASS_EVEN;
}

int MapRouting::publishedCostClass(const Uint16* field,int access) const
{
    const auto it=gradientRuntime->fieldCosts.find(field);
    return it!=gradientRuntime->fieldCosts.end()?it->second:access;
}
void MapRouting::recordFieldCost(const Uint16* field,int cost)
{ if(field) {if(cost<0)gradientRuntime->fieldCosts.erase(field);else gradientRuntime->fieldCosts[field]=cost;} }
