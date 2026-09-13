// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct IslandsOptions
{
	int water, sand, grass, desert, smoothing, extra_islands, fruit, repeat;
	explicit IslandsOptions(const GenerationRequest &r)
		: water(r.option("water")), sand(r.option("sand")), grass(r.option("grass")),
		  desert(r.option("desert")), smoothing(r.option("smoothing")),
		  extra_islands(r.option("extra-islands")), fruit(r.option("fruit")),
		  repeat(r.option("repeat"))
	{
	}
};
GeneratorDefinition islandsDefinition();
