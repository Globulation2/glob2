// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct EvenGroundOptions
{
	int water;       // the share of the map the solver is asked to put under water, percent
	int balance;     // how heavily equal catchments weigh against the other targets, 0 to 100
	int passes;      // how tight the ways between neighbouring colonies should be, 0 to 100
	int effort;      // how long the solver searches: 0 brief, 1 normal, 2 patient
	int wheat, wood, stone, algae; // percentages of the default amounts
	explicit EvenGroundOptions(const GenerationRequest &r);
};
GeneratorDefinition evenGroundDefinition();
