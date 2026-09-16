// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct CityStatesOptions
{
	int commonsSize, straitWidth, causewayWidth, coastRoughness, valleys, islands, sand, frontier;
	bool stoneWalls, sandRoads;
	bool cobblestoneRoads; // the sand roads' surface: cobblestone instead of sand
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit CityStatesOptions(const GenerationRequest &r);
};
GeneratorDefinition cityStatesDefinition();
