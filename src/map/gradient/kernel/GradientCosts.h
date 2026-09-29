// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared field encoding and movement costs. The queue and direction selector
// must use the same costs to agree on the next tile. See MapInternal.h for the
// 0/1/0xFFFF sentinels and the stored value = 0xFFFF - cheapest cost rule.
#include "GradientBucket.h"
#include "MapInternal.h"
#include "UnitConsts.h"

namespace gradient_kernel
{
	// Cost of entering a water cell per swim class, in gradient units (a land
	// cell costs GRADIENT_STEP). Class 0 cannot swim: its seeds mark water as an
	// obstacle, so its entry is never used.
	constexpr int WATER_STEP[SWIM_CLASS_COUNT] = { 0, 5, 7, 10, 13, 20, 30 };
	constexpr int MAX_STEP = WATER_STEP[SWIM_CLASS_COUNT - 1] * GRADIENT_DIAGONAL_STEP / GRADIENT_STEP;
	constexpr unsigned BUCKETS = GradientBucket::COUNT;
	// A future edge cannot circle back to the current bucket during expansion.
	static_assert(BUCKETS > MAX_STEP && (BUCKETS & (BUCKETS - 1)) == 0);
	// Leave one unreachable sentinel below every possible propagated value,
	// including the longest edge from the last expandable cost layer.
	constexpr int COST_LIMIT = GRADIENT_AT_GOAL - GRADIENT_UNREACHABLE - 1 - MAX_STEP;

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
}
