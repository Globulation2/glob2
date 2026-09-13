// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct AnthillOptions
{
	int chamberSpacing, chamberSize, queenRoom, tunnelWidth, loops;
	int wheat, wood, algae, fruit; // percentages of the default amounts
	explicit AnthillOptions(const GenerationRequest &r);
};
GeneratorDefinition anthillDefinition();
