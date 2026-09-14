// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct SwampOptions
{
	int water, grass, smoothing, fruit, repeat;
	explicit SwampOptions(const GenerationRequest &r)
		: water(r.option("water")), grass(r.option("grass")), smoothing(r.option("smoothing")),
		  fruit(r.option("fruit")), repeat(r.option("repeat"))
	{
	}
};
GeneratorDefinition swampDefinition();
