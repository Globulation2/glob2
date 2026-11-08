// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct WatershedOptions
{
	int riverDensity, riverWidth, dryness, fords;
	bool delta, meanders;
	bool deepWater; // 2.0: deep cores in the sea and the wide channels
	bool outcrops;  // 2.0: rock knots with quarry faces along the catchment divides
	int climate;    // 2.0: 0 random, 1 temperate, 2 steppe, 3 tropical (looks only)
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit WatershedOptions(const GenerationRequest &r);
};
GeneratorDefinition watershedDefinition();
