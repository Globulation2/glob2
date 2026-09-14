// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct EvergladesOptions
{
	int poolSpacing, poolSize, sloughs, clearingSize, levee;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit EvergladesOptions(const GenerationRequest &r);
};
GeneratorDefinition evergladesDefinition();
