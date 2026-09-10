// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct LatticeOptions
{
	int isletSize, channelWidth, homeRadius;
	int corn, wood, stone, algae, fruit;
	explicit LatticeOptions(const GenerationRequest &r);
};
GeneratorDefinition latticeDefinition();
