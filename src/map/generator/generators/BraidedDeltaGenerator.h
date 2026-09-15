// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct BraidedDeltaOptions
{
	int braids, rejoins, islandSize, crossingSpacing;
	int wheat, wood, stone, algae, fruit;
	explicit BraidedDeltaOptions(const GenerationRequest &);
};
GeneratorDefinition braidedDeltaDefinition();
