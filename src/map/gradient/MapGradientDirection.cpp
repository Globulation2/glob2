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
		return WATER_ONLY_CLASS;
	// A water cell should cost walk/swim land cells: pick the closest class.
	int wanted = (GRADIENT_STEP * walkSpeed + swimSpeed / 2) / swimSpeed;
	int best = 1;
	for (int c = 2; c < LEGACY_SWIM_CLASS_COUNT; c++)
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
	const auto cost = cellRule(targetIndex).ground[swimClass];
	return dx != 0 && dy != 0 ? cost.diagonal : cost.cardinal;
}

template<bool Track>
bool Map::directionByGradientImpl(EntityRandom& random, Uint32 teamMask, int swimClass, int x, int y, const Uint16 *gradient, int *dx, int *dy, bool strict, Uint32 guardAreaMask, GradientMovementEvidence* evidence) const
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
        if (g <= GRADIENT_UNREACHABLE) continue;
        if constexpr (Track) {
            bool free;
            MovementRejection rejected=MovementRejection::None;
            // Reuse exactly the normal selector's short-circuited predicates.
            // Classification exists only in this collection specialization.
            if (swimClass==AIR_MOVEMENT_CLASS) free=isFreeForMovement(x+ddx,y+ddy,swimClass,teamMask);
            else if (swimClass==WATER_ONLY_CLASS && !terrainPropertiesAt(x+ddx,y+ddy).swimmable) {
                free=false;rejected=MovementRejection::Persistent;
            } else free=checkTileClassified(x+ddx,y+ddy,{true,true,true,false,true},canSwim,teamMask,NOGBID,rejected);
            if (!free) {
                if (g>here) {
                    evidence->persistentProgressBlocked|=rejected==MovementRejection::Persistent;
                    evidence->unitProgressBlocked|=rejected==MovementRejection::Unit;
                }
                continue;
            }
        } else if (!isFreeForMovement(x+ddx,y+ddy,swimClass,teamMask)) continue;
		if (guardAreaMask && !(areaCells[n].guard & guardAreaMask))
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
	// units blocking each other do not mirror each other forever. The caller's private stream
	// keeps the choice deterministic.
    if constexpr (Track) evidence->sidestepUsed=true;
	int pick = sidesteps[random.nextU32() % sidestepCount];
	*dx = tabClose[pick][0];
	*dy = tabClose[pick][1];
	return true;
}

// The off specialization retains the original public ABI. In an optimized build
// its wrapper inlines the original selector with no evidence branches/arguments
// passed to its occupancy predicate. Diagnostic work has a separate entry point.
bool Map::directionByGradient(EntityRandom& random,Uint32 teamMask,int swimClass,int x,int y,
    const Uint16* gradient,int* dx,int* dy,bool strict,Uint32 guardAreaMask) const
{
    return directionByGradientImpl<false>(random,teamMask,swimClass,x,y,gradient,dx,dy,strict,guardAreaMask,nullptr);
}
bool Map::directionByGradientObserved(EntityRandom& random,Uint32 teamMask,int swimClass,int x,int y,
    const Uint16* gradient,int* dx,int* dy,bool strict,GradientMovementEvidence& evidence,Uint32 guardAreaMask) const
{
    return directionByGradientImpl<true>(random,teamMask,swimClass,x,y,gradient,dx,dy,strict,guardAreaMask,&evidence);
}
