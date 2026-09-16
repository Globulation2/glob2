// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct RiceTerracesOptions
{
	int extraHills, hillRadius, bandWidth, stairs, towers;
	bool river;
	int wheat, wood, stone, algae, fruit;
	explicit RiceTerracesOptions(const GenerationRequest &);
};
GeneratorDefinition riceTerracesDefinition();
