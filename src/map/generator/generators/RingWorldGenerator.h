// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct RingWorldOptions
{
	int beltWidth, coastRoughness, lakeDensity, resourceIslands;
	bool windingBelt, bothCoasts;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit RingWorldOptions(const GenerationRequest &r);
};
GeneratorDefinition ringWorldDefinition();
