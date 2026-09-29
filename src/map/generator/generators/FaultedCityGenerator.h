// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct FaultedCityOptions
{
	int displacement, faultWidth, ruins, junctions;
	int wheat, wood, stone, algae, fruit;
	explicit FaultedCityOptions(const GenerationRequest &);
};
GeneratorDefinition faultedCityDefinition();
