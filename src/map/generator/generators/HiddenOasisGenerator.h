// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct HiddenOasisOptions
{
	int pondSize;   // the oasis pond's radius in tiles
	int algae;      // algae tiles on the pond: the only algae on the map
	int towers;     // whether every colony starts with its level-1 tower over the gorge (else open pads)
	int school;     // whether every colony starts with a finished level-0 school (off: the total lock)
	int desert;     // the share of the country that is bare sand, in percent
	int buttes, springs;            // how many of each the country has, 0 to 10
	int wheat, wood, stone, fruit;  // percentages of the default amounts
	explicit HiddenOasisOptions(const GenerationRequest &r);
};
GeneratorDefinition hiddenOasisDefinition();
