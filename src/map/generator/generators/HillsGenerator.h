// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct HillsOptions
{
	int extraHills, hillRadius, bandWidth, stairs, towers;
	bool river;
	int wheat, wood, stone, algae, fruit;
	explicit HillsOptions(const GenerationRequest &);
};
GeneratorDefinition hillsDefinition();
