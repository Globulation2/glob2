// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct SavannahOptions
{
	int wateringHoles, dryPatches, grassland, kopjes;
	bool trails;
	// Ambient percentages; starter minima are independent. Wheat also scales maize, rice and potatoes.
	int wheat, wood, stone, algae, fruit, fish, camps;
	explicit SavannahOptions(const GenerationRequest &);
};
GeneratorDefinition savannahDefinition();
