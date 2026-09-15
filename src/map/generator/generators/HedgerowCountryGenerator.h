// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct HedgerowCountryOptions
{
	int fieldSize, hedgeThickness, gateways, woodedShare, wheat, wood, stone, fruit;
	explicit HedgerowCountryOptions(const GenerationRequest &);
};
GeneratorDefinition hedgerowCountryDefinition();
