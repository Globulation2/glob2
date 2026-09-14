// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct CoralOptions
{
	int branching, forkAngle, branchWidth, straitWidth, lean, landBridges, homeSize;
	bool sandRoads;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit CoralOptions(const GenerationRequest &r);
};
GeneratorDefinition coralDefinition();
