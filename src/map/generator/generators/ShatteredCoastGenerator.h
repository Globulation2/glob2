// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct ShatteredCoastOptions
{
	int water, sand, grass, smoothing;
	bool colony_meadows;
	int wheat, wood, stone, algae; // percentages of the default amounts
	explicit ShatteredCoastOptions(const GenerationRequest &r)
		: water(r.option("water")), sand(r.option("sand")), grass(r.option("grass")),
		  smoothing(r.option("smoothing")), colony_meadows(r.option("colony-meadows") != 0),
		  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
		  stone(r.option("stone-amount")), algae(r.option("algae-amount"))
	{
	}
	int percent(int resourceType) const
	{
		return resourceType == CORN    ? wheat
			   : resourceType == WOOD  ? wood
			   : resourceType == STONE ? stone
									   : algae;
	}
};
GeneratorDefinition shatteredCoastDefinition();
