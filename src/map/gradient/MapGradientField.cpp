// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "MapInternal.h"
#include "BuildingGradientSearch.h"
#include "Utilities.h"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>
// GLOB2_GRADIENT_SCALAR selects the portable relaxation on x86 too, so tests can cover it.
#if defined(__SSE2__) && !defined(GLOB2_GRADIENT_SCALAR)
#define GLOB2_GRADIENT_SSE2 1
#include <emmintrin.h>
#endif

// Building and reading the pathfinding gradients (cell values: MapInternal.h).
//
// A gradient is built with Dial's bucket-queue Dijkstra from its seeded cells.
// Every cell ends at its cheapest cost to a seed. That cost is unique, so the
// field is fully determined by the input whatever order the queue runs in; a
// paused search stops only between whole cost layers, which are unique too.

namespace
{
	// Cost of entering a water cell per swim class, in gradient units (a land
	// cell costs GRADIENT_STEP). Class 0 cannot swim: its seeds mark water as an
	// obstacle, so its entry is never used.
	constexpr int WATER_STEP[SWIM_CLASS_COUNT] = { 0, 5, 7, 10, 13, 20, 30 };
	constexpr int MAX_STEP = WATER_STEP[SWIM_CLASS_COUNT - 1] * GRADIENT_DIAGONAL_STEP / GRADIENT_STEP;
	constexpr unsigned BUCKETS = GradientBucket::COUNT;
	static_assert(BUCKETS > MAX_STEP && (BUCKETS & (BUCKETS - 1)) == 0);
	// Costs above this would run into the sentinels; propagation stops there.
	constexpr int COST_LIMIT = GRADIENT_AT_GOAL - GRADIENT_UNREACHABLE - 1 - MAX_STEP;
	static_assert(COST_LIMIT == Map::GRADIENT_COST_LIMIT);


	// Costs of entering a cell by a cardinal and by a diagonal step.
	struct EntrySteps
	{
		unsigned cardinal, diagonal;
	};

	constexpr EntrySteps entrySteps(int step)
	{
		return { unsigned(step), unsigned(step * GRADIENT_DIAGONAL_STEP / GRADIENT_STEP) };
	}

	constexpr EntrySteps LAND_STEPS = entrySteps(GRADIENT_STEP);

	// Expand one complete cost layer. Positive edge costs cannot append to the
	// current bucket. Sharing this kernel keeps eager and resumed fields equal.
	template<typename StepsAt>
	void expandBucket(Uint16 *__restrict gradient, GradientBucket *queue, size_t &pending,
		int cur, int limit, int widthMask, int heightMask, int widthShift, StepsAt stepsAt)
	{
		GradientBucket &bucket = queue[unsigned(cur) % BUCKETS];
		const size_t wMask = size_t(widthMask), hMask = size_t(heightMask);
		const unsigned wDec = unsigned(widthShift);
		const Uint16 curValue = Uint16(GRADIENT_AT_GOAL - cur);
		// Relaxations append to other buckets but never to this one (each step is
		// positive and less than BUCKETS), so these stay valid.
		const Uint32 *const cells = bucket.cells.data();
		const size_t count = bucket.size;
		for (size_t ci = 0; ci < count; ci++)
		{
			const size_t i = cells[ci];
			if (gradient[i] != curValue)
				continue; // stale entry, a cheaper path was found later
			const size_t x = i & wMask;
			const size_t y = i >> wDec;
			const size_t left = (x - 1) & wMask;
			const size_t right = (x + 1) & wMask;
			const size_t above = ((y - 1) & hMask) << wDec;
			const size_t row = y << wDec;
			const size_t below = ((y + 1) & hMask) << wDec;
			// All reverse edges enter i, so they share its two terrain costs.
			const EntrySteps steps = stepsAt(i);
			const unsigned cardinalCost = unsigned(cur) + steps.cardinal;
			const unsigned diagonalCost = unsigned(cur) + steps.diagonal;
			// A capped value of one admits no cell.
			const Uint16 cardinalValue = cardinalCost <= unsigned(limit) ? Uint16(GRADIENT_AT_GOAL - cardinalCost) : 1;
			const Uint16 diagonalValue = diagonalCost <= unsigned(limit) ? Uint16(GRADIENT_AT_GOAL - diagonalCost) : 1;
			GradientBucket &cardinalBucket = queue[cardinalCost % BUCKETS];
			GradientBucket &diagonalBucket = queue[diagonalCost % BUCKETS];
			cardinalBucket.reserveExtra(4);
			diagonalBucket.reserveExtra(4);
			Uint32 *const cardinalStart = cardinalBucket.cells.data() + cardinalBucket.size;
			Uint32 *const diagonalStart = diagonalBucket.cells.data() + diagonalBucket.size;
			Uint32 *cardinalEnd = cardinalStart;
			Uint32 *diagonalEnd = diagonalStart;
			// Zero wraps to the maximum, so one compare rejects obstacles and cells
			// that are no worse. The slot is always written and kept on improvement.
			auto relax = [&](size_t n, Uint16 value, Uint32 *&end)
			{
				const Uint16 g = gradient[n];
				const bool better = Uint16(g - 1) < Uint16(value - 1);
				gradient[n] = better ? value : g;
				*end = Uint32(n);
				end += better;
			};
#ifdef GLOB2_GRADIENT_SSE2
			if (x >= 1 && x + 2 <= wMask)
			{
				// NW, N, NE and SW, S, SE as two lane groups; lanes 3 and 7 (x + 2) never improve.
				Uint16 *aboveRun = gradient + above + x - 1;
				Uint16 *belowRun = gradient + below + x - 1;
				const __m128i g = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)aboveRun),
					_mm_loadl_epi64((const __m128i *)belowRun));
				const __m128i value = _mm_setr_epi16(short(diagonalValue), short(cardinalValue), short(diagonalValue), 1,
					short(diagonalValue), short(cardinalValue), short(diagonalValue), 1);
				const __m128i one = _mm_set1_epi16(1);
				const __m128i bias = _mm_set1_epi16(short(0x8000));
				const __m128i better = _mm_cmplt_epi16(_mm_xor_si128(_mm_sub_epi16(g, one), bias),
					_mm_xor_si128(_mm_sub_epi16(value, one), bias));
				const unsigned mask = unsigned(_mm_movemask_epi8(better));
				if (mask)
				{
					const __m128i merged = _mm_or_si128(_mm_and_si128(better, value), _mm_andnot_si128(better, g));
					_mm_storel_epi64((__m128i *)aboveRun, merged);
					_mm_storel_epi64((__m128i *)belowRun, _mm_unpackhi_epi64(merged, merged));
					const Uint32 a = Uint32(above | x), b = Uint32(below | x);
					*diagonalEnd = a - 1; diagonalEnd += (mask >> 0) & 1;
					*cardinalEnd = a; cardinalEnd += (mask >> 2) & 1;
					*diagonalEnd = a + 1; diagonalEnd += (mask >> 4) & 1;
					*diagonalEnd = b - 1; diagonalEnd += (mask >> 8) & 1;
					*cardinalEnd = b; cardinalEnd += (mask >> 10) & 1;
					*diagonalEnd = b + 1; diagonalEnd += (mask >> 12) & 1;
				}
				relax(row | right, cardinalValue, cardinalEnd);
				relax(row | left, cardinalValue, cardinalEnd);
			}
			else
#endif
			{
				relax(above | left, diagonalValue, diagonalEnd);
				relax(above | x, cardinalValue, cardinalEnd);
				relax(above | right, diagonalValue, diagonalEnd);
				relax(row | right, cardinalValue, cardinalEnd);
				relax(below | right, diagonalValue, diagonalEnd);
				relax(below | x, cardinalValue, cardinalEnd);
				relax(below | left, diagonalValue, diagonalEnd);
				relax(row | left, cardinalValue, cardinalEnd);
			}
			cardinalBucket.size += size_t(cardinalEnd - cardinalStart);
			diagonalBucket.size += size_t(diagonalEnd - diagonalStart);
			pending += size_t(cardinalEnd - cardinalStart) + size_t(diagonalEnd - diagonalStart);
		}
		pending -= count;
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
	propagateGradientSnapshot(gradient, swimClass, maxCost, gradientWorkspace(), nullptr);
}

void Map::propagateGradientSnapshot(Uint16 *gradient, int swimClass, int maxCost,
	GradientWorkspace &workspace, const std::uint8_t *water)
{
	auto *buckets = workspace.buckets.data();
	auto &deferredSeeds = workspace.deferredSeeds;
	static_assert(std::tuple_size<decltype(workspace.buckets)>::value == BUCKETS);
	const int limit = std::min(maxCost, COST_LIMIT);
	for (auto &bucket : workspace.buckets)
		bucket.clear();
	deferredSeeds.clear();
	size_t pending = 0;
	for (size_t i = 0; i < size; i++)
		if (gradient[i] > GRADIENT_UNREACHABLE)
		{
			int cost = GRADIENT_AT_GOAL - gradient[i];
			if (cost <= MAX_STEP)
			{
				buckets[unsigned(cost) % BUCKETS].push(Uint32(i));
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
				buckets[unsigned(cur) % BUCKETS].push(Uint32(deferredSeeds[nextSeed].second));
				pending++;
			}
			expandBucket(gradient, buckets, pending, cur, limit, wMask, hMask, wDec, stepAt);
		}
	};
	if (swimClass == 0 || swimClass == SWIM_CLASS_EVEN)
		sweep([](size_t) { return LAND_STEPS; });
	else
	{
		const EntrySteps waterSteps = entrySteps(WATER_STEP[swimClass]);
		sweep([&](size_t i) { return (water ? water[i] != 0 : isWater((unsigned)i)) ? waterSteps : LAND_STEPS; });
	}
}

void BuildingGradientSearch::begin(const Map &map, std::uint16_t *seeded, int swim)
{
	gradient = seeded;
	cells = std::size_t(map.getW()) * map.getH();
	widthMask = map.getMaskW();
	heightMask = map.getMaskH();
	widthShift = map.getShiftW();
	swimClass = swim;
	currentCost = 0;
	popped = 0;
	pending = 0;
	for (auto &bucket : buckets) bucket.clear();
	const bool weighted = swim != 0 && swim != Map::SWIM_CLASS_EVEN;
	const bool splitWater = weighted && map.computeEnabled(Map::ComputeInitialize) && cells >= 16384;
	if (weighted) water.resize(cells);
	else water.clear();
	if (splitWater)
	{
		map.initializeGradientCells([&](size_t begin, size_t end) {
			for (size_t i = begin; i < end; ++i) water[i] = map.isWater(static_cast<unsigned>(i));
		});
	}
	// Building fields have only zero-cost seeds, so no deferred seeds are needed.
	for (std::size_t i = 0; i < cells; ++i)
	{
		assert(gradient[i] <= GRADIENT_UNREACHABLE || gradient[i] == GRADIENT_AT_GOAL);
		if (gradient[i] == GRADIENT_AT_GOAL)
		{
			buckets[0].push(static_cast<Uint32>(i));
			++pending;
		}
		if (weighted && !splitWater) water[i] = map.isWater(static_cast<unsigned>(i));
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
			popped += buckets[currentCost % BUCKETS].size;
			expandBucket(gradient, buckets.data(), pending, currentCost, COST_LIMIT,
				widthMask, heightMask, widthShift, stepAt);
			++currentCost;
		}
	};
	if (water.empty())
		sweep([](size_t) { return LAND_STEPS; });
	else
	{
		const EntrySteps waterSteps = entrySteps(WATER_STEP[swimClass]);
		sweep([&](size_t i) { return water[i] ? waterSteps : LAND_STEPS; });
	}
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
