// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct GauntletOptions
{
	int courtSize, gateWidth, partitionWall, towers;
	int wheat, wood, algae, fruit;
	explicit GauntletOptions(const GenerationRequest &);
};
GeneratorDefinition gauntletDefinition();
