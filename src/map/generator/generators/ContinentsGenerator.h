// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct ContinentsOptions
{
	int continent;   // 0 for a random one, else 1 + its index in the world atlas
	int orientation; // 0 turns the continent a quarter turn when that fits the map larger
	bool mountains;  // stone scree on the named ranges
	bool rivers;     // the great rivers as lines of water with fords
	bool islets;     // small islands raised in the empty ocean round the continent
	int oases;       // percentage of the pond dug for a colony that starts with no water in reach
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit ContinentsOptions(const GenerationRequest &r);
};
GeneratorDefinition continentsDefinition();
