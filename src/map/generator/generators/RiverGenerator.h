// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct RiverOptions
{
	int water, sand, grass, desert, smoothing, river_width, fruit, repeat;
	bool winding;
	explicit RiverOptions(const GenerationRequest &r)
		: water(r.option("water")), sand(r.option("sand")), grass(r.option("grass")),
		  desert(r.option("desert")), smoothing(r.option("smoothing")),
		  river_width(r.option("river-width")), fruit(r.option("fruit")), repeat(r.option("repeat")),
		  winding(r.option("winding-river") != 0)
	{
	}
};
GeneratorDefinition riverDefinition();
