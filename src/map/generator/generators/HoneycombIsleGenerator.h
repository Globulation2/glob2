// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct HoneycombIsleOptions
{
	int blockShape, blockSize, streetWidth, warp, blocksPerColony, rubble, outlineGaps, craters;
	int riverWidth, bridges, wheatFields;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit HoneycombIsleOptions(const GenerationRequest &r);
};
GeneratorDefinition honeycombIsleDefinition();
