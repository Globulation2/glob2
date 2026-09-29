// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct HungryMarchesOptions
{
	int ration, concentration, wheat, wood, stone, algae;
	explicit HungryMarchesOptions(const GenerationRequest &);
};
GeneratorDefinition hungryMarchesDefinition();
