// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct BraidedRiverOptions
{
	int braidWidth;   // the belt of channels and bars, as a percentage of the map's breadth
	int channels;     // channel threads across the belt (clamped so bars stay bar-sized)
	int barSize;      // a bar's length along the river, in tiles
	int extraRiffles; // percentage of the spare bar-to-bar crossings opened beyond the tree
	bool moraine;     // a broken line of stone hummocks along each terrace edge
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit BraidedRiverOptions(const GenerationRequest &r);
};
GeneratorDefinition braidedRiverDefinition();
