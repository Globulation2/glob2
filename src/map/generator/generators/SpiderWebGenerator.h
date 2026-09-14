// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct SpiderWebOptions
{
	int spokes, ringSpacing, threadWidth, sag, tornStrands, hubSize, dewDrops, homeSize;
	bool spiral, sandRoads;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit SpiderWebOptions(const GenerationRequest &r);
};
GeneratorDefinition spiderWebDefinition();
