// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct BraidedDeltaOptions
{
	int flood, distributaries, islands, crossings, dunes;
	int wheat, wood, stone, algae, fruit;
	explicit BraidedDeltaOptions(const GenerationRequest &);
};
GeneratorDefinition braidedDeltaDefinition();
