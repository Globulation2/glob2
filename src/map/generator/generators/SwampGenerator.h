// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct SwampOptions
{
	int water, grass, smoothing, fruit, repeat;
	// 2.0: the bog pass's layers (deep pond cores, the share of far beaches that is mud, moss in the
	// hollows, thicket hammocks per 128x128 tiles, boardwalk causeways) and the look-only climate
	// (0 random, 1 temperate bog, 2 tropical mangrove).
	int deepWater, mudflats, moss, hammocks, causeways, climate;
	explicit SwampOptions(const GenerationRequest &r)
		: water(r.option("water")), grass(r.option("grass")), smoothing(r.option("smoothing")),
		  fruit(r.option("fruit")), repeat(r.option("repeat")), deepWater(r.option("deep-water")),
		  mudflats(r.option("mudflats")), moss(r.option("moss")), hammocks(r.option("hammocks")),
		  causeways(r.option("causeways")), climate(r.option("climate"))
	{
	}
};
GeneratorDefinition swampDefinition();
