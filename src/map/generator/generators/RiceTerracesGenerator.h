// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct RiceTerracesOptions
{
	int hillsides, slant, terraces, bandWidth, waviness, stairSpacing, homeSize;
	bool river;
	int wheat, wood, stone, algae, fruit;
	explicit RiceTerracesOptions(const GenerationRequest &);
};
GeneratorDefinition riceTerracesDefinition();
