// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct DrumlinFieldOptions
{
	int grain;          // 0 random, 1 horizontal, 2 vertical, 3 diagonal
	int drumlinSpacing; // between drumlin sites across the grain, in tiles
	int drumlinLength;  // a drumlin's length as a percentage of its width
	int waterGap;       // open water kept between any two drumlins, in tiles across the grain
	int homeSize;       // a home drumlin's half width, in tiles
	int eskerLoops;     // extra eskers beyond the tree that joins every drumlin, percent of drumlins
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit DrumlinFieldOptions(const GenerationRequest &r);
};
GeneratorDefinition drumlinFieldDefinition();
