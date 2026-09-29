// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct SavannahOptions
{
	int wateringHoles, dryPatches;
	int wheat, wood, stone, algae, fruit; // ambient percentages; starter minima are independent
	explicit SavannahOptions(const GenerationRequest &);
};
GeneratorDefinition savannahDefinition();
