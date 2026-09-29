// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct DrownedForestOptions
{
	int connections, neck, clearing;
	int wheat, wood, stone, algae, fruit;
	explicit DrownedForestOptions(const GenerationRequest &);
};
GeneratorDefinition drownedForestDefinition();
