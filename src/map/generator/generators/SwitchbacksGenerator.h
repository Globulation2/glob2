// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct SwitchbacksOptions
{
	int trailWidth, legWall, plateauSize, homeSize, towers, towerCount;
	bool sandRoads, farmPlots;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit SwitchbacksOptions(const GenerationRequest &r);
};
GeneratorDefinition switchbacksDefinition();
