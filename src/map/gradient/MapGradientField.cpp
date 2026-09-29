// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#include <PerformanceTelemetry.h>
#include "Map.h"
#include "MapInternal.h"
#include "BuildingGradientSearch.h"
#include "Utilities.h"

#include <algorithm>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <vector>
// GLOB2_GRADIENT_SCALAR selects the portable relaxation on SIMD targets too, so tests can cover it.
#if defined(__SSE2__) && !defined(GLOB2_GRADIENT_SCALAR)
#define GLOB2_GRADIENT_SSE2 1
#include <emmintrin.h>
#elif defined(__ARM_NEON) && !defined(GLOB2_GRADIENT_SCALAR)
#define GLOB2_GRADIENT_NEON 1
#include <arm_neon.h>
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

	// Shared scratch storage retains capacity between fields. Calls must be serial
	// and non-reentrant, including calls on different Maps. Before parallelizing
	// propagation, give each worker its own workspace; these are not cached fields.
	GradientBucket buckets[BUCKETS];
	// Seeds whose cost lies beyond the bucket window, sorted by (cost, cell);
	// each enters its bucket once the sweep reaches its cost.
	std::vector<std::pair<int, int>> deferredSeeds;

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

	// Classes 0 and EVEN pay the land rate everywhere; the others are weighted.
	constexpr bool weightedClass(int swimClass)
	{
		return swimClass != 0 && WATER_STEP[swimClass] != GRADIENT_STEP;
	}

	// A weighted layer appends to four buckets, one per entry step, so each keeps
	// its own cursor. That needs the four steps to be distinct in every class.
	constexpr bool weightedStepsDistinct()
	{
		for (int c = 0; c < SWIM_CLASS_COUNT; c++)
		{
			if (!weightedClass(c))
				continue;
			const EntrySteps water = entrySteps(WATER_STEP[c]);
			const unsigned steps[4] = { LAND_STEPS.cardinal, LAND_STEPS.diagonal, water.cardinal, water.diagonal };
			for (int a = 0; a < 4; a++)
				for (int b = a + 1; b < 4; b++)
					if (steps[a] == steps[b])
						return false;
		}
		return true;
	}
	static_assert(weightedStepsDistinct());

	// Cells expanded between capacity reservations. Each appends at most four
	// entries to each target bucket, so reserving 4 * CHUNK lets the loop append
	// without checks while holding the bucket ends in registers.
	constexpr size_t CHUNK = 64;

	// Expand one complete cost layer. Positive edge costs cannot append to the
	// current bucket. Sharing this kernel keeps eager and resumed fields equal.
	// Weighted layers read isWater(i) and charge waterSteps to enter water cells.
	template<bool Weighted, typename IsWater>
	void expandBucket(Uint16 *__restrict gradient, GradientBucket *queue, size_t &pending,
		int cur, int limit, int widthMask, int heightMask, int widthShift, EntrySteps waterSteps, IsWater isWater)
	{
		GradientBucket &bucket = queue[unsigned(cur) % BUCKETS];
		const size_t wMask = size_t(widthMask), hMask = size_t(heightMask);
		const unsigned wDec = unsigned(widthShift);
		const Uint16 curValue = Uint16(GRADIENT_AT_GOAL - cur);
		// A capped value of one admits no cell.
		auto valueAfter = [&](unsigned step)
		{
			const unsigned cost = unsigned(cur) + step;
			return cost <= unsigned(limit) ? Uint16(GRADIENT_AT_GOAL - cost) : Uint16(1);
		};
		const Uint16 landCardinalValue = valueAfter(LAND_STEPS.cardinal), landDiagonalValue = valueAfter(LAND_STEPS.diagonal);
		const Uint16 waterCardinalValue = valueAfter(waterSteps.cardinal), waterDiagonalValue = valueAfter(waterSteps.diagonal);
		GradientBucket &landCardinal = queue[(unsigned(cur) + LAND_STEPS.cardinal) % BUCKETS];
		GradientBucket &landDiagonal = queue[(unsigned(cur) + LAND_STEPS.diagonal) % BUCKETS];
		GradientBucket &waterCardinal = queue[(unsigned(cur) + waterSteps.cardinal) % BUCKETS];
		GradientBucket &waterDiagonal = queue[(unsigned(cur) + waterSteps.diagonal) % BUCKETS];
		auto queued = [&]
		{
			return landCardinal.size + landDiagonal.size + (Weighted ? waterCardinal.size + waterDiagonal.size : 0);
		};
		const size_t queuedBefore = queued();
		// Relaxations append to other buckets but never to this one (each step is
		// positive and less than BUCKETS), so these stay valid.
		const Uint32 *const cells = bucket.cells.data();
		const size_t count = bucket.size;
		for (size_t chunk = 0; chunk < count; chunk += CHUNK)
		{
			const size_t chunkEnd = std::min(count, chunk + CHUNK);
			const size_t room = 4 * (chunkEnd - chunk);
			landCardinal.reserveExtra(room);
			landDiagonal.reserveExtra(room);
			if (Weighted)
			{
				waterCardinal.reserveExtra(room);
				waterDiagonal.reserveExtra(room);
			}
			Uint32 *landCardinalEnd = landCardinal.cells.data() + landCardinal.size;
			Uint32 *landDiagonalEnd = landDiagonal.cells.data() + landDiagonal.size;
			Uint32 *waterCardinalEnd = Weighted ? waterCardinal.cells.data() + waterCardinal.size : nullptr;
			Uint32 *waterDiagonalEnd = Weighted ? waterDiagonal.cells.data() + waterDiagonal.size : nullptr;
			for (size_t ci = chunk; ci < chunkEnd; ci++)
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
				const bool water = Weighted && isWater(i);
				const Uint16 cardinalValue = water ? waterCardinalValue : landCardinalValue;
				const Uint16 diagonalValue = water ? waterDiagonalValue : landDiagonalValue;
				Uint32 *cardinalEnd = water ? waterCardinalEnd : landCardinalEnd;
				Uint32 *diagonalEnd = water ? waterDiagonalEnd : landDiagonalEnd;
				// Zero wraps to the maximum, so one compare rejects obstacles and cells
				// that are no worse. The slot is always written and kept on improvement.
				// Comparing in 32 bits spares a 16-bit truncation on targets like ARM.
				const unsigned cardinalLimit = unsigned(cardinalValue) - 1u;
				const unsigned diagonalLimit = unsigned(diagonalValue) - 1u;
				auto relax = [&](size_t n, Uint16 value, unsigned valueLimit, Uint32 *&end)
				{
					const Uint16 g = gradient[n];
					const bool better = unsigned(g) - 1u < valueLimit;
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
					relax(row | right, cardinalValue, cardinalLimit, cardinalEnd);
					relax(row | left, cardinalValue, cardinalLimit, cardinalEnd);
				}
				else
#elif defined(GLOB2_GRADIENT_NEON)
				if (x >= 1 && x + 2 <= wMask)
				{
					// Columns x - 1 .. x + 2 of the three rows. Lanes at x + 2 and at the
					// cell itself get value 1, which never improves.
					Uint16 *aboveRun = gradient + above + x - 1;
					Uint16 *rowRun = gradient + row + x - 1;
					Uint16 *belowRun = gradient + below + x - 1;
					const uint16x8_t g = vcombine_u16(vld1_u16(aboveRun), vld1_u16(belowRun));
					const uint16x4_t gRow = vld1_u16(rowRun);
					const uint16x4_t one = vdup_n_u16(1);
					const uint16x4_t outer = vset_lane_u16(1, vset_lane_u16(cardinalValue, vdup_n_u16(diagonalValue), 1), 3);
					const uint16x8_t value = vcombine_u16(outer, outer);
					const uint16x4_t valueRow = vset_lane_u16(cardinalValue, vset_lane_u16(cardinalValue, one, 0), 2);
					const uint16x8_t better = vcltq_u16(vsubq_u16(g, vdupq_n_u16(1)), vsubq_u16(value, vdupq_n_u16(1)));
					const uint16x4_t betterRow = vclt_u16(vsub_u16(gRow, one), vsub_u16(valueRow, one));
					// One byte per above/below lane, sixteen bits per row lane.
					const uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(better, 8)), 0);
					const uint64_t rowMask = vget_lane_u64(vreinterpret_u64_u16(betterRow), 0);
					if (mask | rowMask)
					{
						const uint16x8_t merged = vbslq_u16(better, value, g);
						vst1_u16(aboveRun, vget_low_u16(merged));
						vst1_u16(belowRun, vget_high_u16(merged));
						vst1_u16(rowRun, vbsl_u16(betterRow, valueRow, gRow));
						const Uint32 a = Uint32(above | x), b = Uint32(below | x), r = Uint32(i);
						*diagonalEnd = a - 1; diagonalEnd += (mask >> 0) & 1;
						*cardinalEnd = a; cardinalEnd += (mask >> 8) & 1;
						*diagonalEnd = a + 1; diagonalEnd += (mask >> 16) & 1;
						*diagonalEnd = b - 1; diagonalEnd += (mask >> 32) & 1;
						*cardinalEnd = b; cardinalEnd += (mask >> 40) & 1;
						*diagonalEnd = b + 1; diagonalEnd += (mask >> 48) & 1;
						*cardinalEnd = r - 1; cardinalEnd += (rowMask >> 0) & 1;
						*cardinalEnd = r + 1; cardinalEnd += (rowMask >> 32) & 1;
					}
				}
				else
#endif
				{
					relax(above | left, diagonalValue, diagonalLimit, diagonalEnd);
					relax(above | x, cardinalValue, cardinalLimit, cardinalEnd);
					relax(above | right, diagonalValue, diagonalLimit, diagonalEnd);
					relax(row | right, cardinalValue, cardinalLimit, cardinalEnd);
					relax(below | right, diagonalValue, diagonalLimit, diagonalEnd);
					relax(below | x, cardinalValue, cardinalLimit, cardinalEnd);
					relax(below | left, diagonalValue, diagonalLimit, diagonalEnd);
					relax(row | left, cardinalValue, cardinalLimit, cardinalEnd);
				}
				if (water)
				{
					waterCardinalEnd = cardinalEnd;
					waterDiagonalEnd = diagonalEnd;
				}
				else
				{
					landCardinalEnd = cardinalEnd;
					landDiagonalEnd = diagonalEnd;
				}
			}
			landCardinal.size = size_t(landCardinalEnd - landCardinal.cells.data());
			landDiagonal.size = size_t(landDiagonalEnd - landDiagonal.cells.data());
			if (Weighted)
			{
				waterCardinal.size = size_t(waterCardinalEnd - waterCardinal.cells.data());
				waterDiagonal.size = size_t(waterDiagonalEnd - waterDiagonal.cells.data());
			}
		}
		pending += queued() - queuedBefore;
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
	const int limit = std::min(maxCost, COST_LIMIT);
	for (auto &bucket : buckets)
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
	// Specializing this common case skips the per-cell terrain lookup.
	auto sweep = [&](auto weighted, EntrySteps waterSteps, auto waterAt)
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
			expandBucket<decltype(weighted)::value>(gradient, buckets, pending, cur, limit, wMask, hMask, wDec,
				waterSteps, waterAt);
		}
	};
	if (!weightedClass(swimClass))
		sweep(std::false_type(), LAND_STEPS, [](size_t) { return false; });
	else
		sweep(std::true_type(), entrySteps(WATER_STEP[swimClass]), [&](size_t i) { return isWater((unsigned)i); });
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
	pending = 0;
	for (auto &bucket : buckets) bucket.clear();
	const bool weighted = weightedClass(swim);
	if (weighted) water.resize(cells);
	else water.clear();
	// Building fields have only zero-cost seeds, so no deferred seeds are needed.
	for (std::size_t i = 0; i < cells; ++i)
	{
		assert(gradient[i] <= GRADIENT_UNREACHABLE || gradient[i] == GRADIENT_AT_GOAL);
		if (gradient[i] == GRADIENT_AT_GOAL)
		{
			buckets[0].push(static_cast<Uint32>(i));
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
	auto sweep = [&](auto weighted, EntrySteps waterSteps, auto waterAt)
	{
		while (pending && (target == cells || !resolved(target)))
		{
			assert(currentCost <= COST_LIMIT);
			expandBucket<decltype(weighted)::value>(gradient, buckets.data(), pending, currentCost, COST_LIMIT,
				widthMask, heightMask, widthShift, waterSteps, waterAt);
			++currentCost;
		}
	};
	if (water.empty())
		sweep(std::false_type(), LAND_STEPS, [](size_t) { return false; });
	else
	{
		const std::uint8_t *const waterCells = water.data();
		sweep(std::true_type(), entrySteps(WATER_STEP[swimClass]), [waterCells](size_t i) { return waterCells[i] != 0; });
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
