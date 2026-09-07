// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include "Map.h"
#include "MapInternal.h"
#include "Utilities.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>
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
	static_assert(COST_LIMIT == Map::GRADIENT_COST_LIMIT);

	std::vector<int> buckets[BUCKETS];
	// Seeds whose cost lies beyond the bucket window, sorted by (cost, cell);
	// each enters its bucket once the sweep reaches its cost.
	std::vector<std::pair<int, int>> deferredSeeds;

	bool anyTraffic(const Uint8 *counts)
	{
		Uint64 all;
		memcpy(&all, counts, sizeof all);
		return all != 0;
	}
}

static_assert(WATER_STEP[Map::SWIM_CLASS_EVEN] == GRADIENT_STEP);

namespace
{
	// Full lane penalty, in gradient units (GRADIENT_STEP = one land step),
	// reached when this many more units recently entered the passage against
	// us than our way. Anything less pays in proportion, so a slight imbalance
	// already tips the next units toward another gap and the lane reinforces
	// itself.
	constexpr int LANE_PENALTY = 30;
	constexpr int LANE_FULL_EXCESS = 16;
}

void Map::recordTraffic(int x, int y, int direction)
{
	assert(direction >= 0 && direction < 8);
	Uint8 &count = trafficDirection[coordToIndex(x, y) * 8 + direction];
	if (count < 255)
		count++;
}

void Map::decayTraffic()
{
	for (size_t i = 0; i < size * 8; i++)
		trafficDirection[i] >>= 1;
}

bool Map::isPassage(size_t index, const Uint16 *gradient) const
{
	size_t x = index & wMask;
	size_t y = index >> wDec;
	for (int side = 1; side < 4; side += 2) // N and S, then E and W
		if (gradient[coordToIndex(x + tabClose[side][0], y + tabClose[side][1])] == GRADIENT_FORBIDDEN
			&& gradient[coordToIndex(x + tabClose[side + 4][0], y + tabClose[side + 4][1])] == GRADIENT_FORBIDDEN)
			return true;
	return false;
}

int Map::lanePenalty(size_t index, int direction) const
{
	const Uint8 *counts = trafficDirection + index * 8;
	// Units that headed within 45 degrees of our way, and of the opposite way.
	int same = counts[direction] + counts[(direction + 1) & 7] + counts[(direction + 7) & 7];
	int opposing = counts[(direction + 4) & 7] + counts[(direction + 3) & 7] + counts[(direction + 5) & 7];
	if (opposing <= same)
		return 0;
	return std::min(LANE_PENALTY, (opposing - same) * LANE_PENALTY / LANE_FULL_EXCESS);
}

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
// (0 for GRADIENT_AT_GOAL, anything up to COST_LIMIT otherwise, e.g. a resource
// tile seeded with its distance to a building). GRADIENT_FORBIDDEN cells are
// obstacles.
void Map::propagateGradient(Uint16 *gradient, int swimClass, int maxCost)
{
	const int limit = std::min(maxCost, COST_LIMIT);
	for (int b = 0; b < BUCKETS; b++)
		buckets[b].clear();
	deferredSeeds.clear();
	size_t pending = 0;
	for (size_t i = 0; i < size; i++)
		if (gradient[i] > GRADIENT_UNREACHABLE)
		{
			int cost = GRADIENT_AT_GOAL - gradient[i];
			if (cost <= MAX_STEP)
			{
				buckets[cost % BUCKETS].push_back((int)i);
				pending++;
			}
			else
				deferredSeeds.push_back({cost, (int)i});
		}
	std::sort(deferredSeeds.begin(), deferredSeeds.end());
	size_t nextSeed = 0;
	for (int cur = 0; (pending > 0 || nextSeed < deferredSeeds.size()) && cur <= limit; cur++)
	{
		if (pending == 0)
			cur = deferredSeeds[nextSeed].first;
		for (; nextSeed < deferredSeeds.size() && deferredSeeds[nextSeed].first == cur; nextSeed++)
		{
			buckets[cur % BUCKETS].push_back(deferredSeeds[nextSeed].second);
			pending++;
		}
		std::vector<int> &bucket = buckets[cur % BUCKETS];
		// Relaxations may append to other buckets but never to this one
		// (every step costs at least 5), so iterating by index is safe.
		for (size_t bi = 0; bi < bucket.size(); bi++)
		{
			int i = bucket[bi];
			pending--;
			if (GRADIENT_AT_GOAL - gradient[i] != cur)
				continue; // stale entry, a cheaper path was found later
			size_t x = i & wMask;
			size_t y = i >> wDec;
			const bool lane = trafficDirection != NULL && anyTraffic(trafficDirection + (size_t)i * 8) && isPassage((size_t)i, gradient);
			for (int d = 0; d < 8; d++)
			{
				int dx = tabClose[d][0];
				int dy = tabClose[d][1];
				size_t n = (((y + dy) & hMask) << wDec) | ((x + dx) & wMask);
				if (gradient[n] == GRADIENT_FORBIDDEN)
					continue;
				// The step from n to i enters i, so it is charged i's terrain;
				// it heads the opposite way of d.
				int cost = cur + stepCost(dx, dy, (size_t)i, swimClass);
				if (lane)
					cost += lanePenalty((size_t)i, (d + 4) & 7);
				if (cost <= limit && cost < GRADIENT_AT_GOAL - gradient[n])
				{
					gradient[n] = (Uint16)(GRADIENT_AT_GOAL - cost);
					buckets[cost % BUCKETS].push_back((int)n);
					pending++;
				}
			}
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
			if (trafficDirection != NULL && isPassage(n, gradient))
				score -= lanePenalty(n, d);
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
