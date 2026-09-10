// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "Map.h"
#include <memory>
#include "MapInternal.h"
#include "Utilities.h"

#include <cstdlib>
#include <vector>

// Building and reading the pathfinding gradients (cell values: MapInternal.h).
//
// A gradient is built with Dial's bucket-queue Dijkstra from its seeded cells.
// Every bucket is a FIFO and neighbours are relaxed in tabClose order, so the
// result and every tie-break are fully determined by the input.

namespace
{
	// Cost of entering a water cell per swim class, in gradient units (a land
	// cell costs GRADIENT_STEP). Class 0 cannot swim: its seeds mark water as an
	// obstacle, so its entry is never used.
	constexpr int WATER_STEP[SWIM_CLASS_COUNT] = { 0, 5, 7, 10, 13, 20, 30 };
	constexpr int MAX_STEP = WATER_STEP[SWIM_CLASS_COUNT - 1] * GRADIENT_DIAGONAL_STEP / GRADIENT_STEP;
	constexpr int BUCKETS = MAX_STEP + 1;
	// Costs above this would run into the sentinels; propagation stops there.
	constexpr int COST_LIMIT = GRADIENT_AT_GOAL - GRADIENT_UNREACHABLE - 1 - MAX_STEP;

}

// Propagation scratch: the bucket queues. One per thread that propagates (the
// main thread's below, the worker's on its own stack); capacity is retained
// between fields. Not a cache: it holds no field data between calls.
struct Map::GradientScratch
{
	std::vector<int> buckets[BUCKETS];
};

void Map::GradientScratchDeleter::operator()(GradientScratch *scratch) const
{
	delete scratch;
}

Map::GradientScratchPtr Map::newGradientScratch()
{
	return GradientScratchPtr(new GradientScratch);
}

void Map::propagateGradient(Uint16 *gradient, int swimClass)
{
	// Main-thread callers only (units, AIs, editor), which are serial.
	static GradientScratch mainScratch;
	propagateGradient(gradient, swimClass, mainScratch);
}

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

// Seeds are the cells above GRADIENT_UNREACHABLE; each starts at its own cost
// (0 for GRADIENT_AT_GOAL). Seed costs must be in [0, MAX_STEP], so the initial
// queue fits one bucket rotation. Reseed before reuse; a propagated field is
// not a valid seed buffer. GRADIENT_FORBIDDEN cells are obstacles.
void Map::propagateGradient(Uint16 *gradient, int swimClass, GradientScratch &scratch)
{
	std::vector<int> (&bk)[BUCKETS] = scratch.buckets;
	for (int b = 0; b < BUCKETS; b++)
		bk[b].clear();
	size_t pending = 0;
	for (size_t i = 0; i < size; i++)
		if (gradient[i] > GRADIENT_UNREACHABLE)
		{
			int cost = GRADIENT_AT_GOAL - gradient[i];
			assert(cost <= MAX_STEP);
			bk[cost % BUCKETS].push_back((int)i);
			pending++;
		}
	for (int cur = 0; pending > 0 && cur <= COST_LIMIT; cur++)
	{
		std::vector<int> &bucket = bk[cur % BUCKETS];
		// Relaxations may append to other buckets but never to this one
		// (each step is positive and less than BUCKETS), so iteration is safe.
		for (size_t bi = 0; bi < bucket.size(); bi++)
		{
			int i = bucket[bi];
			pending--;
			if (GRADIENT_AT_GOAL - gradient[i] != cur)
				continue; // stale entry, a cheaper path was found later
			size_t x = i & wMask;
			size_t y = i >> wDec;
			const size_t left = (x - 1) & wMask;
			const size_t right = (x + 1) & wMask;
			const size_t above = ((y - 1) & hMask) << wDec;
			const size_t row = y << wDec;
			const size_t below = ((y + 1) & hMask) << wDec;
			// All reverse edges enter i, so they share its two terrain costs.
			const int cardinalCost = cur + stepCost(1, 0, (size_t)i, swimClass);
			const int diagonalCost = cur + stepCost(1, 1, (size_t)i, swimClass);
			auto& cardinalBucket = bk[cardinalCost % BUCKETS];
			auto& diagonalBucket = bk[diagonalCost % BUCKETS];
			auto relax = [&](size_t n, int cost, std::vector<int>& destination)
			{
				if (gradient[n] != GRADIENT_FORBIDDEN && cost < GRADIENT_AT_GOAL - gradient[n])
				{
					gradient[n] = (Uint16)(GRADIENT_AT_GOAL - cost);
					destination.push_back((int)n);
					pending++;
				}
			};
			// Preserve tabClose order: NW, N, NE, E, SE, S, SW, W.
			relax(above | left, diagonalCost, diagonalBucket);
			relax(above | x, cardinalCost, cardinalBucket);
			relax(above | right, diagonalCost, diagonalBucket);
			relax(row | right, cardinalCost, cardinalBucket);
			relax(below | right, diagonalCost, diagonalBucket);
			relax(below | x, cardinalCost, cardinalBucket);
			relax(below | left, diagonalCost, diagonalBucket);
			relax(row | left, cardinalCost, cardinalBucket);
		}
		bucket.clear();
	}
}

bool Map::directionByGradient(Uint32 teamMask, int swimClass, int x, int y, const Uint16 *gradient, int *dx, int *dy, bool strict) const
{
	const bool canSwim = swimClass > 0;
	Uint16 here = gradient[coordToIndex(x, y)];
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
