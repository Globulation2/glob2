// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct KarstTowersOptions
{
	int towerSpacing, towerDensity, homeDesign, riverWidth, fords, paddyDepth, homeSize;
	int wheat, wood, algae, fruit; // percentages of the default amounts
	explicit KarstTowersOptions(const GenerationRequest &r);
};
GeneratorDefinition karstTowersDefinition();
