// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct MazeOptions
{
	// channelWidth is the moat's width since revision 17 (the control keeps its id).
	int cellShape, cellSize, channelWidth, wallThickness, loopiness, warp, gardens;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	bool pavedRoads, treasure;
	explicit MazeOptions(const GenerationRequest &r);
};
GeneratorDefinition mazeDefinition();
