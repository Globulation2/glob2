// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pathfinding relaxation kernel shared by eager and resumed searches.
// Each call settles one cost layer. The scalar, SSE2 and NEON paths must make
// the same improvements; queue order within a layer may differ, but the final
// field and the completed-layer boundary must not.
#include "GradientCosts.h"
#include "Grid.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Tests can force the scalar path on SIMD hosts with GLOB2_GRADIENT_SCALAR.
// The architecture paths below implement the same neighbor relaxation rule.
#if defined(__SSE2__) && !defined(GLOB2_GRADIENT_SCALAR)
#define GLOB2_GRADIENT_SSE2 1
#include <emmintrin.h>
#elif defined(__ARM_NEON) && !defined(GLOB2_GRADIENT_SCALAR)
#define GLOB2_GRADIENT_NEON 1
#include <arm_neon.h>
#endif

// Building and reading the pathfinding gradients (cell values: GradientConstants.h).
//
// A gradient is built with Dial's bucket-queue Dijkstra from its seeded cells.
// Every cell ends at its cheapest cost to a seed. That cost is unique, so the
// field is fully determined by the input whatever order the queue runs in; a
// paused search stops only between whole cost layers, which are unique too.

namespace gradient_kernel
{
	// Cells expanded between capacity reservations. A cell can append at most
	// four neighbors to one target bucket. Reserve 4 * CHUNK for every target
	// before taking raw end pointers; no append in the inner loop may reallocate.
	constexpr size_t CHUNK = 64;

	// Expand one complete cost layer. queue holds cell indices, including stale
	// entries for cells later reached more cheaply; pending counts entries, not
	// distinct cells. Every edge cost is positive and less than BUCKETS, so a
	// target bucket never aliases the bucket being read during this call.
	// The search runs backward from goals: when expanding i, each neighbor is a
	// possible predecessor, so all eight edges charge the cost of entering i.
	// Weighted layers read isWater(i) for that cost; uniform layers omit the read.
	template<bool Weighted, bool Masked, typename IsWater>
	void expandBucketAddressed(std::uint16_t *__restrict gradient, GradientBucket *queue, size_t &pending,
		int cur, int limit, const field::Grid& grid, EntrySteps waterSteps, IsWater isWater)
	{
		GradientBucket &bucket = queue[unsigned(cur) % BUCKETS];
		const size_t wMask = size_t(grid.width()-1), hMask = size_t(grid.height()-1);
		const unsigned wDec = Masked ? unsigned(grid.widthShift()) : 0;
		const std::uint16_t curValue = std::uint16_t(GRADIENT_AT_GOAL - cur);
		// One is the unreachable sentinel. Returning it for an edge beyond the
		// caller's cap makes that edge unable to improve any neighbor.
		auto valueAfter = [&](unsigned step)
		{
			const unsigned cost = unsigned(cur) + step;
			return cost <= unsigned(limit) ? std::uint16_t(GRADIENT_AT_GOAL - cost) : std::uint16_t(1);
		};
		const std::uint16_t landCardinalValue = valueAfter(LAND_STEPS.cardinal), landDiagonalValue = valueAfter(LAND_STEPS.diagonal);
		const std::uint16_t waterCardinalValue = valueAfter(waterSteps.cardinal), waterDiagonalValue = valueAfter(waterSteps.diagonal);
		GradientBucket &landCardinal = queue[(unsigned(cur) + LAND_STEPS.cardinal) % BUCKETS];
		GradientBucket &landDiagonal = queue[(unsigned(cur) + LAND_STEPS.diagonal) % BUCKETS];
		// In the uniform specialization waterSteps equals LAND_STEPS. Do not bind
		// duplicate buckets or cursors there: distinct water buckets are required
		// only by weighted classes (enforced by weightedStepsDistinct()).
		GradientBucket *waterCardinal = nullptr, *waterDiagonal = nullptr;
		if constexpr (Weighted)
		{
			waterCardinal = &queue[(unsigned(cur) + waterSteps.cardinal) % BUCKETS];
			waterDiagonal = &queue[(unsigned(cur) + waterSteps.diagonal) % BUCKETS];
		}
		auto queued = [&]
		{
			if constexpr (Weighted)
				return landCardinal.size + landDiagonal.size + waterCardinal->size + waterDiagonal->size;
			else
				return landCardinal.size + landDiagonal.size;
		};
		const size_t queuedBefore = queued();
		// The relaxation vectors depend only on the terrain entered, so build both
		// sets once per layer. Compare (old - 1) < (new - 1): old=0 wraps high
		// and stays forbidden; old=1 improves for any reachable new value;
		// an equal or better old value does not change.
#if defined(GLOB2_GRADIENT_SSE2)
		// Lanes 0..3 are NW, N, NE, unused; 4..7 are SW, S, SE, unused.
		// The unused x+2 lanes carry value 1, so they cannot improve.
		auto aroundValues = [](std::uint16_t cardinal, std::uint16_t diagonal)
		{
			return _mm_setr_epi16(short(diagonal), short(cardinal), short(diagonal), 1,
				short(diagonal), short(cardinal), short(diagonal), 1);
		};
		// SSE2 compares signed: biasing both sides by 0x8000 orders them unsigned.
		const __m128i one = _mm_set1_epi16(1);
		const __m128i bias = _mm_set1_epi16(short(0x8000));
		auto biasedLimit = [&](__m128i values) { return _mm_xor_si128(_mm_sub_epi16(values, one), bias); };
		const __m128i landValues = aroundValues(landCardinalValue, landDiagonalValue);
		const __m128i waterValues = aroundValues(waterCardinalValue, waterDiagonalValue);
		const __m128i landLimits = biasedLimit(landValues), waterLimits = biasedLimit(waterValues);
#elif defined(GLOB2_GRADIENT_NEON)
		// Four lanes per row cover x-1..x+2. In the middle row the center and
		// x+2 lanes carry 1; in the outer rows only x+2 carries 1.
		auto outerValues = [](std::uint16_t cardinal, std::uint16_t diagonal)
		{
			const uint16x4_t half = vset_lane_u16(1, vset_lane_u16(cardinal, vdup_n_u16(diagonal), 1), 3);
			return vcombine_u16(half, half);
		};
		auto rowValues = [](std::uint16_t cardinal)
		{
			return vset_lane_u16(cardinal, vset_lane_u16(cardinal, vdup_n_u16(1), 0), 2);
		};
		const uint16x8_t landValues = outerValues(landCardinalValue, landDiagonalValue);
		const uint16x8_t waterValues = outerValues(waterCardinalValue, waterDiagonalValue);
		const uint16x4_t landRowValues = rowValues(landCardinalValue), waterRowValues = rowValues(waterCardinalValue);
		const uint16x8_t landLimits = vsubq_u16(landValues, vdupq_n_u16(1));
		const uint16x8_t waterLimits = vsubq_u16(waterValues, vdupq_n_u16(1));
		const uint16x4_t landRowLimits = vsub_u16(landRowValues, vdup_n_u16(1));
		const uint16x4_t waterRowLimits = vsub_u16(waterRowValues, vdup_n_u16(1));
#endif
		// No target bucket can be the current bucket, so reserving target capacity
		// cannot invalidate this pointer even when a target vector reallocates.
		const std::uint32_t *const cells = bucket.cells.data();
		const size_t count = bucket.size;
		for (size_t chunk = 0; chunk < count; chunk += CHUNK)
		{
			const size_t chunkEnd = std::min(count, chunk + CHUNK);
			const size_t room = 4 * (chunkEnd - chunk);
			landCardinal.reserveExtra(room);
			landDiagonal.reserveExtra(room);
			if constexpr (Weighted)
			{
				waterCardinal->reserveExtra(room);
				waterDiagonal->reserveExtra(room);
			}
			std::uint32_t *landCardinalEnd = landCardinal.cells.data() + landCardinal.size;
			std::uint32_t *landDiagonalEnd = landDiagonal.cells.data() + landDiagonal.size;
			std::uint32_t *waterCardinalEnd = nullptr, *waterDiagonalEnd = nullptr;
			if constexpr (Weighted)
			{
				waterCardinalEnd = waterCardinal->cells.data() + waterCardinal->size;
				waterDiagonalEnd = waterDiagonal->cells.data() + waterDiagonal->size;
			}
			for (size_t ci = chunk; ci < chunkEnd; ci++)
			{
				const size_t i = cells[ci];
				if (gradient[i] != curValue)
					continue; // stale entry, a cheaper path was found later
				const size_t x = Masked ? i & wMask : i % grid.width();
				const size_t y = Masked ? i >> wDec : i / grid.width();
				const size_t left = Masked ? (x - 1) & wMask : grid.wrapX(int(x)-1);
				const size_t right = Masked ? (x + 1) & wMask : grid.wrapX(int(x)+1);
				const size_t above = Masked ? ((y - 1) & hMask) << wDec : grid.wrapY(int(y)-1)*grid.width();
				const size_t row = Masked ? y << wDec : y*grid.width();
				const size_t below = Masked ? ((y + 1) & hMask) << wDec : grid.wrapY(int(y)+1)*grid.width();
				// All reverse edges enter i, so they share its two terrain costs.
				const bool water = Weighted && isWater(i);
				const std::uint16_t cardinalValue = water ? waterCardinalValue : landCardinalValue;
				const std::uint16_t diagonalValue = water ? waterDiagonalValue : landDiagonalValue;
				std::uint32_t *cardinalEnd = water ? waterCardinalEnd : landCardinalEnd;
				std::uint32_t *diagonalEnd = water ? waterDiagonalEnd : landDiagonalEnd;
				// The scalar compare uses 32-bit unsigned subtraction: forbidden zero
				// wraps high, while only strictly better nonzero values pass. Writing
				// *end unconditionally is safe: the cursor advances only on improvement,
				// so a rejected slot remains outside the bucket's logical size.
				const unsigned cardinalLimit = unsigned(cardinalValue) - 1u;
				const unsigned diagonalLimit = unsigned(diagonalValue) - 1u;
				auto relax = [&](size_t n, std::uint16_t value, unsigned valueLimit, std::uint32_t *&end)
				{
					const std::uint16_t g = gradient[n];
					const bool better = unsigned(g) - 1u < valueLimit;
					gradient[n] = better ? value : g;
					*end = std::uint32_t(n);
					end += better;
				};
#ifdef GLOB2_GRADIENT_SSE2
				// Four contiguous cells must fit in each row. Seam-adjacent cells
				// use scalar wrapped indices below; vector loads never cross a row.
				if (x >= 1 && x + 2 <= wMask)
				{
						// movemask gives two bits per 16-bit lane; bits 0,2,4 and
						// 8,10,12 are the six neighbors queued below.
					std::uint16_t *aboveRun = gradient + above + x - 1;
					std::uint16_t *belowRun = gradient + below + x - 1;
					const __m128i g = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)aboveRun),
						_mm_loadl_epi64((const __m128i *)belowRun));
					const __m128i value = water ? waterValues : landValues;
					const __m128i better = _mm_cmplt_epi16(_mm_xor_si128(_mm_sub_epi16(g, one), bias),
						water ? waterLimits : landLimits);
					const unsigned mask = unsigned(_mm_movemask_epi8(better));
					if (mask)
					{
						const __m128i merged = _mm_or_si128(_mm_and_si128(better, value), _mm_andnot_si128(better, g));
						_mm_storel_epi64((__m128i *)aboveRun, merged);
						_mm_storel_epi64((__m128i *)belowRun, _mm_unpackhi_epi64(merged, merged));
						const std::uint32_t a = std::uint32_t(above + x), b = std::uint32_t(below + x);
						*diagonalEnd = a - 1; diagonalEnd += (mask >> 0) & 1;
						*cardinalEnd = a; cardinalEnd += (mask >> 2) & 1;
						*diagonalEnd = a + 1; diagonalEnd += (mask >> 4) & 1;
						*diagonalEnd = b - 1; diagonalEnd += (mask >> 8) & 1;
						*cardinalEnd = b; cardinalEnd += (mask >> 10) & 1;
						*diagonalEnd = b + 1; diagonalEnd += (mask >> 12) & 1;
					}
					relax(row + right, cardinalValue, cardinalLimit, cardinalEnd);
					relax(row + left, cardinalValue, cardinalLimit, cardinalEnd);
				}
				else
#elif defined(GLOB2_GRADIENT_NEON)
				if (x >= 1 && x + 2 <= wMask)
				{
						// Columns x-1..x+2 of all three rows; the same contiguous-row
						// condition as SSE2 is required before loading.
					std::uint16_t *aboveRun = gradient + above + x - 1;
					std::uint16_t *rowRun = gradient + row + x - 1;
					std::uint16_t *belowRun = gradient + below + x - 1;
					const uint16x8_t g = vcombine_u16(vld1_u16(aboveRun), vld1_u16(belowRun));
					const uint16x4_t gRow = vld1_u16(rowRun);
					const uint16x8_t value = water ? waterValues : landValues;
					const uint16x4_t valueRow = water ? waterRowValues : landRowValues;
					const uint16x8_t better = vcltq_u16(vsubq_u16(g, vdupq_n_u16(1)), water ? waterLimits : landLimits);
					const uint16x4_t betterRow = vclt_u16(vsub_u16(gRow, vdup_n_u16(1)), water ? waterRowLimits : landRowLimits);
						// Narrow the outer masks to one byte per lane and view the middle
						// mask as 16-bit lanes. Shifts below match those lane widths.
					const uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(better, 8)), 0);
					const uint64_t rowMask = vget_lane_u64(vreinterpret_u64_u16(betterRow), 0);
					if (mask | rowMask)
					{
						const uint16x8_t merged = vbslq_u16(better, value, g);
						vst1_u16(aboveRun, vget_low_u16(merged));
						vst1_u16(belowRun, vget_high_u16(merged));
						vst1_u16(rowRun, vbsl_u16(betterRow, valueRow, gRow));
						const std::uint32_t a = std::uint32_t(above + x), b = std::uint32_t(below + x), r = std::uint32_t(i);
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
					// This also handles toroidal edges, where a four-cell
					// vector load would cross a physical row boundary.
					relax(above + left, diagonalValue, diagonalLimit, diagonalEnd);
					relax(above + x, cardinalValue, cardinalLimit, cardinalEnd);
					relax(above + right, diagonalValue, diagonalLimit, diagonalEnd);
					relax(row + right, cardinalValue, cardinalLimit, cardinalEnd);
					relax(below + right, diagonalValue, diagonalLimit, diagonalEnd);
					relax(below + x, cardinalValue, cardinalLimit, cardinalEnd);
					relax(below + left, diagonalValue, diagonalLimit, diagonalEnd);
					relax(row + left, cardinalValue, cardinalLimit, cardinalEnd);
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
			if constexpr (Weighted)
			{
				waterCardinal->size = size_t(waterCardinalEnd - waterCardinal->cells.data());
				waterDiagonal->size = size_t(waterDiagonalEnd - waterDiagonal->cells.data());
			}
		}
		// Account for every appended entry, including duplicates that later
		// become stale; consume exactly the original layer's entries.
		pending += queued() - queuedBefore;
		pending -= count;
		bucket.clear();
	}
	// Choose geometry once for a complete layer; both paths share relaxation,
	// bucket accounting and SIMD rules, including thin-grid aliasing.
	template<bool Weighted, typename IsWater>
	void expandBucket(std::uint16_t* gradient, GradientBucket* queue, size_t& pending,
		int cur, int limit, const field::Grid& grid, EntrySteps waterSteps, IsWater isWater)
	{
		if(grid.powerOfTwo())
			expandBucketAddressed<Weighted,true>(gradient,queue,pending,cur,limit,grid,waterSteps,isWater);
		else
			expandBucketAddressed<Weighted,false>(gradient,queue,pending,cur,limit,grid,waterSteps,isWater);
	}

}
