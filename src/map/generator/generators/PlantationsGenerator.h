// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct PlantationsOptions
{
	int plotSize, farmWidth, straitWidth, islandsPerColony, coastRoughness;
	bool outpostInns, causeways;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit PlantationsOptions(const GenerationRequest &r);
};
GeneratorDefinition plantationsDefinition();
