// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct CarouselOptions
{
	int homeSize, corridorWidth, spokeWidth, courtSize, courtWall, plazaSize, towers, towerCount;
	bool sandRoads, farmPlots;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit CarouselOptions(const GenerationRequest &r);
};
GeneratorDefinition carouselDefinition();
