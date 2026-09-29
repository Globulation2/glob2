// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct CaravanseraiOptions
{
	int oasisSize, oases, caravanserais, desert;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit CaravanseraiOptions(const GenerationRequest &r);
};
GeneratorDefinition caravanseraiDefinition();
