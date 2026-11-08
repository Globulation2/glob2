// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct EvergladesOptions
{
	int poolSpacing, poolSize, sloughs, clearingSize, levee;
	int mudflats, hammocks; // percent of the swamp's banks that are mud; hammocks per 128x128
	int season;             // 1 wet, 2 dry (looks only); a random request is resolved from the seed
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit EvergladesOptions(const GenerationRequest &r);
};
GeneratorDefinition evergladesDefinition();
