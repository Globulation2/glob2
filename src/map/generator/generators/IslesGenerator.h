// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct IslesOptions
{
	int island_size, bridge_width;
	bool land_bridges, sandy_beaches;
	int wheat, wood, stone, algae; // percentages of the default amounts
	explicit IslesOptions(const GenerationRequest &r)
		: island_size(r.option("island-size")), bridge_width(r.option("bridge-width")),
		  land_bridges(r.option("land-bridges") != 0), sandy_beaches(r.option("sandy-beaches") != 0),
		  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
		  stone(r.option("stone-amount")), algae(r.option("algae-amount"))
	{
	}
};
GeneratorDefinition islesDefinition();
