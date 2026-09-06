// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "Map.h"
#include "MapInternal.h"
#include "PathfindStats.h"

#include <vector>

// Weighted distance fields (alternative pathfinder, see PathfindPolicy.h).
//
// A field holds, per cell, the integer cost of the cheapest path from that
// cell to any goal cell. Costs are octile: a cardinal step costs
// WEIGHT_CARDINAL, a diagonal step WEIGHT_DIAGONAL, and entering a water
// cell (only possible for swimmers, otherwise water is an obstacle) is
// WATER_MULTIPLIER times as expensive because swimming is slower than
// walking. The field is built with Dial's bucket-queue Dijkstra: every
// bucket is a FIFO and neighbours are relaxed in tabClose order, so the
// result and every tie-break are fully determined by the input.

namespace
{
	constexpr int WEIGHT_CARDINAL = 10;
	constexpr int WEIGHT_DIAGONAL = 14;
	constexpr int WATER_MULTIPLIER = 2;
	constexpr int MAX_EDGE = WEIGHT_DIAGONAL * WATER_MULTIPLIER;
	constexpr int BUCKETS = MAX_EDGE + 1;
	// Costs above this are treated as unreachable so cost + MAX_EDGE never wraps.
	constexpr int COST_LIMIT = Map::COST_INFINITY - MAX_EDGE - 1;

	std::vector<int> buckets[BUCKETS];
}

int Map::weightedStepCost(int dx, int dy, size_t targetIndex, bool canSwim) const
{
	int base = (dx != 0 && dy != 0) ? WEIGHT_DIAGONAL : WEIGHT_CARDINAL;
	if (canSwim && isWater((unsigned)targetIndex))
		base *= WATER_MULTIPLIER;
	return base;
}

void Map::buildWeightedField(const Uint8 *seed, Uint16 *cost, bool canSwim)
{
	PathfindStats::Scope pfScope(PathfindStats::get().weightedField);
	for (int b = 0; b < BUCKETS; b++)
		buckets[b].clear();

	size_t pending = 0;
	for (size_t i = 0; i < size; i++)
	{
		if (seed[i] == GRADIENT_AT_GOAL)
		{
			cost[i] = 0;
			buckets[0].push_back((int)i);
			pending++;
		}
		else
			cost[i] = COST_INFINITY;
	}

	for (int cur = 0; pending > 0 && cur <= COST_LIMIT; cur++)
	{
		std::vector<int> &bucket = buckets[cur % BUCKETS];
		// Relaxations may append to other buckets but never to this one
		// (every edge costs at least WEIGHT_CARDINAL > 0), so iterating by
		// index while the vector is stable is safe.
		for (size_t bi = 0; bi < bucket.size(); bi++)
		{
			int i = bucket[bi];
			pending--;
			if (cost[i] != cur)
				continue; // stale entry, a cheaper path was found later
			size_t x = i & wMask;
			size_t y = i >> wDec;
			for (int d = 0; d < 8; d++)
			{
				int dx = tabClose[d][0];
				int dy = tabClose[d][1];
				size_t nx = (x + dx) & wMask;
				size_t ny = (y + dy) & hMask;
				size_t n = (ny << wDec) | nx;
				if (seed[n] == GRADIENT_FORBIDDEN)
					continue;
				// The step from n to i enters i; charge i's terrain.
				int nc = cur + weightedStepCost(dx, dy, (size_t)i, canSwim);
				if (nc < cost[n])
				{
					cost[n] = (Uint16)nc;
					buckets[nc % BUCKETS].push_back((int)n);
					pending++;
				}
			}
		}
		bucket.clear();
	}
}

void Map::writeGradientFromCost(const Uint16 *cost, Uint8 *gradient) const
{
	for (size_t i = 0; i < size; i++)
	{
		if (gradient[i] == GRADIENT_FORBIDDEN)
			continue;
		if (cost[i] == COST_INFINITY)
			gradient[i] = GRADIENT_UNREACHABLE;
		else
		{
			int tiles = (cost[i] + WEIGHT_CARDINAL / 2) / WEIGHT_CARDINAL;
			if (tiles > GRADIENT_AT_GOAL - 2)
				tiles = GRADIENT_AT_GOAL - 2;
			gradient[i] = (Uint8)(GRADIENT_AT_GOAL - tiles);
		}
	}
}

bool Map::directionByCost(Uint32 teamMask, bool canSwim, int x, int y, const Uint16 *cost, int *dx, int *dy, bool strict) const
{
	PathfindStats::get().directionByCostCalls++;
	size_t here = coordToIndex(x, y);
	int current = cost[here];
	if (current == COST_INFINITY)
		return false;
	if (current == 0)
	{
		*dx = 0;
		*dy = 0;
		return true;
	}
	int best = COST_INFINITY;
	int bestD = -1;
	for (int d = 0; d < 8; d++)
	{
		int ddx = tabClose[d][0];
		int ddy = tabClose[d][1];
		size_t n = coordToIndex(x + ddx, y + ddy);
		int c = cost[n];
		if (c == COST_INFINITY)
			continue;
		if (!isFreeForGroundUnit(x + ddx, y + ddy, canSwim, teamMask))
			continue;
		// Total cost of going through n: the step there plus n's cost-to-go.
		int total = c + weightedStepCost(ddx, ddy, n, canSwim);
		if (total < best)
		{
			best = total;
			bestD = d;
		}
	}
	if (bestD < 0)
		return false;
	// Strict: only move if the neighbour is genuinely closer to the goal.
	// Non-strict (used when blocked): accept a sidestep that is no farther.
	int nextCost = cost[coordToIndex(x + tabClose[bestD][0], y + tabClose[bestD][1])];
	if (strict ? (nextCost >= current) : (nextCost > current))
		return false;
	*dx = tabClose[bestD][0];
	*dy = tabClose[bestD][1];
	return true;
}
