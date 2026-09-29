// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct GlacisOptions
{
	int fortSize, bastions, glacisWidth, gates, towerLevel;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit GlacisOptions(const GenerationRequest &r);
};
GeneratorDefinition glacisDefinition();
