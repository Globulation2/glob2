// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Per-cell entry costs, without the bucket queue (GradientCosts.h) or the
// built-in terrain tables, so registries and cell rules can hold them cheaply.
#include "GradientConstants.h"

namespace gradient_kernel
{
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
}
