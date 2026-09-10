// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct ShatteredCoastOptions
{
	int water, sand, grass, smoothing;
	explicit ShatteredCoastOptions(const GenerationRequest &r)
		: water(r.option("water")), sand(r.option("sand")), grass(r.option("grass")),
		  smoothing(r.option("smoothing"))
	{
	}
};
GeneratorDefinition shatteredCoastDefinition();
