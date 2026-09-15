// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct CaravanseraiOptions
{
	int colonists;
	bool garrison;
	int capitalSize, oasisSpacing, outposts;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit CaravanseraiOptions(const GenerationRequest &r);
};
GeneratorDefinition caravanseraiDefinition();
