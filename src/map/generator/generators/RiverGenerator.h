// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct RiverOptions
{
	int water, sand, grass, desert, smoothing, river_width, fruit, repeat;
	bool winding;
	// River 2.0 (revision 6): the banks' variant (0 random, 1 temperate, 2 desert), the deep channel
	// and the cutbank bluffs, and the stone amount, which sets how many bluffs carry a quarry face.
	int banks = 0, stone = 100;
	bool deepChannel = true, bluffs = true;
	explicit RiverOptions(const GenerationRequest &r)
		: water(r.option("water")), sand(r.option("sand")), grass(r.option("grass")),
		  desert(r.option("desert")), smoothing(r.option("smoothing")),
		  river_width(r.option("river-width")), fruit(r.option("fruit")),
		  repeat(r.option("repeat")), winding(r.option("winding-river") != 0),
		  banks(r.option("banks")), stone(r.option("stone-amount")),
		  deepChannel(r.option("deep-channel") != 0), bluffs(r.option("bluffs") != 0)
	{
	}
};
GeneratorDefinition riverDefinition();
