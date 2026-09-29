// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct OldGrowthOptions
{
	int forestDensity, lakes, lakeSize, homeSize, homePools;
	bool hiddenGroves, trails;
	int wheat, stone, algae, fruit; // percentages of the default amounts
	explicit OldGrowthOptions(const GenerationRequest &r);
};
GeneratorDefinition oldGrowthDefinition();
