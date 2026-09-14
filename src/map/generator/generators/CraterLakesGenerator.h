// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct CraterLakesOptions
{
	int water, sand, grass, desert, smoothing, lake_density, lake_size, fruit, repeat;
	explicit CraterLakesOptions(const GenerationRequest &r)
		: water(r.option("water")), sand(r.option("sand")), grass(r.option("grass")),
		  desert(r.option("desert")), smoothing(r.option("smoothing")),
		  lake_density(r.option("lake-density")), lake_size(r.option("lake-size")),
		  fruit(r.option("fruit")), repeat(r.option("repeat"))
	{
	}
};
GeneratorDefinition craterLakesDefinition();
