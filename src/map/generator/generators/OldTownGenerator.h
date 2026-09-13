// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct OldTownOptions
{
	int citySize, blockSize, streetWidth, warp, plazas, farmPlots;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit OldTownOptions(const GenerationRequest &r);
};
GeneratorDefinition oldTownDefinition();
