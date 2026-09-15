// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct GlacisOptions
{
	int colonists;
	bool garrison;
	int compoundSize, wallGates, wadis, fordSpacing, towerLevel, towerCount;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit GlacisOptions(const GenerationRequest &r);
};
GeneratorDefinition glacisDefinition();
