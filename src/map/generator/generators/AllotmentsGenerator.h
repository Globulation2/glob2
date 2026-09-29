// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct AllotmentsOptions
{
	int siteSize, stripWidth, plotMix, commons;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit AllotmentsOptions(const GenerationRequest &r);
};
GeneratorDefinition allotmentsDefinition();
