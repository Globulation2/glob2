// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct RingWorldOptions
{
	int beltWidth, coastRoughness, lakeDensity, resourceIslands;
	bool windingBelt, bothCoasts;
	// 2.0: a deep ocean core, the broken rock spine with a quarry tor per colony, the ring road,
	// and a look-only climate (0 random, 1 temperate, 2 steppe, 3 tropical).
	bool deepWater, rockSpine, ringRoad;
	int climate;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit RingWorldOptions(const GenerationRequest &r);
};
GeneratorDefinition ringWorldDefinition();
