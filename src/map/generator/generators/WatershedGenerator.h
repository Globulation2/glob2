// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct WatershedOptions
{
	int riverDensity, riverWidth, dryness, fords;
	int frozenCrossings; // 0 sand fords, 1 every other ford ice, 2 every ford ice
	bool delta, meanders;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit WatershedOptions(const GenerationRequest &r);
};
GeneratorDefinition watershedDefinition();
