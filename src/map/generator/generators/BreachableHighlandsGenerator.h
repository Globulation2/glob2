// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct BreachableHighlandsOptions
{
	int valleySize, ridgeDepth, passWidth, extraPasses, saddles, warp, pondSize;
	int wheat, wood, algae, fruit;
	explicit BreachableHighlandsOptions(const GenerationRequest &);
};
GeneratorDefinition breachableHighlandsDefinition();
