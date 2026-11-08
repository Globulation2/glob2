// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct CraterLakesOptions
{
	int water, sand, grass, desert, smoothing, lake_density, lake_size, fruit, repeat;
	// 2.0: crater-type (0 random, 1 volcanic maars, 2 glacial kettles), the deep-water and
	// crater-rims switches, and the stone amount (how many rim arcs carry a quarry face).
	int crater_type, deep_water, crater_rims, stone;
	explicit CraterLakesOptions(const GenerationRequest &r)
		: water(r.option("water")), sand(r.option("sand")), grass(r.option("grass")),
		  desert(r.option("desert")), smoothing(r.option("smoothing")),
		  lake_density(r.option("lake-density")), lake_size(r.option("lake-size")),
		  fruit(r.option("fruit")), repeat(r.option("repeat")), crater_type(r.option("crater-type")),
		  deep_water(r.option("deep-water")), crater_rims(r.option("crater-rims")),
		  stone(r.option("stone-amount"))
	{
	}
};
GeneratorDefinition craterLakesDefinition();
