// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct PolderOptions
{
	int rowAngle, dykeSpacing, villageSize;
	bool hamlets;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit PolderOptions(const GenerationRequest &r);
};
GeneratorDefinition polderDefinition();
