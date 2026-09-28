// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "MapInternal.h"
#include "BuildingGradientSearch.h"
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

	// Shared scratch storage retains capacity between fields. Calls must be serial
	// and non-reentrant, including calls on different Maps. Before parallelizing
	// propagation, give each worker its own workspace; these are not cached fields.
	std::vector<int> buckets[BUCKETS];
	// Seeds whose cost lies beyond the bucket window, sorted by (cost, cell);
	// each enters its bucket once the sweep reaches its cost.
	std::vector<std::pair<int, int>> deferredSeeds;

	// Expand one complete cost layer. Positive edge costs cannot append to the
	// current bucket. Sharing this kernel keeps eager and resumed tie-breaks equal.
	template<typename StepAt>
	void expandBucket(Uint16 *gradient, std::vector<int> *queue, size_t &pending,
		int cur, int limit, int widthMask, int heightMask, int widthShift, StepAt stepAt)
	{
		std::vector<int> &bucket = queue[cur % BUCKETS];
		// Relaxations may append to other buckets but never to this one
		// (each step is positive and less than BUCKETS), so iteration is safe.
		for (size_t bi = 0; bi < bucket.size(); bi++)
		{
			int i = bucket[bi];
			pending--;
			if (GRADIENT_AT_GOAL - gradient[i] != cur)
				continue; // stale entry, a cheaper path was found later
			size_t x = i & widthMask;
			size_t y = i >> widthShift;
			const size_t left = (x - 1) & widthMask;
			const size_t right = (x + 1) & widthMask;
			const size_t above = ((y - 1) & heightMask) << widthShift;
			const size_t row = y << widthShift;
			const size_t below = ((y + 1) & heightMask) << widthShift;
			// All reverse edges enter i, so they share its two terrain costs.
			const int step = stepAt(i);
			const int cardinalCost = cur + step;
			const int diagonalCost = cur + step * GRADIENT_DIAGONAL_STEP / GRADIENT_STEP;
			const unsigned cardinalValue = cardinalCost <= limit ? GRADIENT_AT_GOAL - cardinalCost : 1;
			const unsigned diagonalValue = diagonalCost <= limit ? GRADIENT_AT_GOAL - diagonalCost : 1;
			auto& cardinalBucket = queue[cardinalCost % BUCKETS];
			auto& diagonalBucket = queue[diagonalCost % BUCKETS];
			auto relax = [&](size_t n, unsigned value, std::vector<int>& destination)
			{
				// Zero wraps to UINT_MAX; a capped value of one admits no cell.
				if (static_cast<unsigned>(gradient[n] - 1) < value - 1)
				{
					gradient[n] = (Uint16)value;
					destination.push_back((int)n);
					pending++;
				}
			};
			// Preserve tabClose order: NW, N, NE, E, SE, S, SW, W.
			relax(above | left, diagonalValue, diagonalBucket);
			relax(above | x, cardinalValue, cardinalBucket);
			relax(above | right, diagonalValue, diagonalBucket);
			relax(row | right, cardinalValue, cardinalBucket);
			relax(below | right, diagonalValue, diagonalBucket);
			relax(below | x, cardinalValue, cardinalBucket);
			relax(below | left, diagonalValue, diagonalBucket);
			relax(row | left, cardinalValue, cardinalBucket);
		}
		bucket.clear();
	}
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
// (0 for GRADIENT_AT_GOAL, anything up to COST_LIMIT otherwise, e.g. a resource
// tile seeded with its distance to a building). Seeds within one bucket rotation
// start in the queue; dearer ones join it when the frontier reaches their cost.
// Reseed before reuse; a propagated field is not a valid seed buffer.
// GRADIENT_FORBIDDEN cells are obstacles.
void Map::propagateGradient(Uint16 *gradient, int swimClass, int maxCost)
{
	PERF_SCOPE_TIME(Propagation);
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
	// Class zero never enters water; class EVEN pays the land rate there.
	// Specializing this common case lets the compiler hoist both edge costs
	// and queue references out of the per-cell loop.
	auto sweep = [&](auto stepAt)
	{
		size_t nextSeed = 0;
		for (int cur = 0; (pending > 0 || nextSeed < deferredSeeds.size()) && cur <= limit; cur++)
		{
			if (pending == 0)
				cur = deferredSeeds[nextSeed].first; // the queue is empty: jump to the next seed
			for (; nextSeed < deferredSeeds.size() && deferredSeeds[nextSeed].first == cur; nextSeed++)
			{
				buckets[cur % BUCKETS].push_back(deferredSeeds[nextSeed].second);
				pending++;
			}
			expandBucket(gradient, buckets, pending, cur, limit, wMask, hMask, wDec, stepAt);
		}
	};
	if (swimClass == 0 || swimClass == SWIM_CLASS_EVEN)
		sweep([](int) { return GRADIENT_STEP; });
	else
		sweep([&](int i) { return isWater((unsigned)i) ? WATER_STEP[swimClass] : GRADIENT_STEP; });
}

bool BuildingGradientSearch::enabled()
{
	static const bool useLazy = [] {
		const char *value = std::getenv("GLOB2_LAZY_BUILDING_GRADIENTS");
		return value && std::strcmp(value, "1") == 0;
	}();
	return useLazy;
}

void BuildingGradientSearch::begin(const Map &map, std::uint16_t *seeded, int swim)
{
	static_assert(BucketCount == BUCKETS);
	gradient = seeded;
	cells = std::size_t(map.getW()) * map.getH();
	widthMask = map.getMaskW();
	heightMask = map.getMaskH();
	widthShift = map.getShiftW();
	swimClass = swim;
	currentCost = 0;
	pending = 0;
	for (auto &bucket : buckets) bucket.clear();
	const bool weighted = swim != 0 && swim != Map::SWIM_CLASS_EVEN;
	if (weighted) water.resize(cells);
	else water.clear();
	// Building fields have only zero-cost seeds, so no deferred seeds are needed.
	for (std::size_t i = 0; i < cells; ++i)
	{
		assert(gradient[i] <= GRADIENT_UNREACHABLE || gradient[i] == GRADIENT_AT_GOAL);
		if (gradient[i] == GRADIENT_AT_GOAL)
		{
			buckets[0].push_back(static_cast<int>(i));
			++pending;
		}
		if (weighted) water[i] = map.isWater(static_cast<unsigned>(i));
	}
}

bool BuildingGradientSearch::resolved(std::size_t target) const
{
	assert(target < cells);
	const auto value = gradient[target];
	return complete() || value == GRADIENT_FORBIDDEN || value == GRADIENT_AT_GOAL
		|| (value > GRADIENT_UNREACHABLE && GRADIENT_AT_GOAL - value < currentCost);
}

void BuildingGradientSearch::resolve(std::size_t target)
{
	assert(target <= cells);
	if (complete() || (target < cells && resolved(target))) return;
	PERF_SCOPE_TIME(BuildingGradientResume);
	auto sweep = [&](auto stepAt)
	{
		while (pending && (target == cells || !resolved(target)))
		{
			assert(currentCost <= COST_LIMIT);
			expandBucket(gradient, buckets.data(), pending, currentCost, COST_LIMIT,
				widthMask, heightMask, widthShift, stepAt);
			++currentCost;
		}
	};
	if (water.empty())
		sweep([](int) { return GRADIENT_STEP; });
	else
		sweep([&](int i) { return water[i] ? WATER_STEP[swimClass] : GRADIENT_STEP; });
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
