// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct CentralQuarryOptions
{
	int lakeSize;   // the lake's radius in tiles (shrunk to fit a small map)
	int islandSize; // the island's radius, as a percentage of the lake's
	int quarrySize; // stone tiles in the island's outcrop: the only stone on the map
	int sandBars;   // walkable sand bars from the shore to the island
	int woodland;   // percentage of the country under woodland rather than open farmland
	int wheat, wood, algae, fruit; // percentages of the default amounts
	explicit CentralQuarryOptions(const GenerationRequest &r);
};
GeneratorDefinition centralQuarryDefinition();
