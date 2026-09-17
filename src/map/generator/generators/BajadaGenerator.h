// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct BajadaOptions
{
	int rangeSpacing, ridgeWidth, passes, fans, streamReach, playa, homeDesign;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit BajadaOptions(const GenerationRequest &r);
};
GeneratorDefinition bajadaDefinition();
