// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct ConcreteIslandsOptions
{
	int channel_width, extra_islands;
	bool sandy_beaches;
	int wheat, wood, stone, algae, fruit; // percentages of the default amounts
	explicit ConcreteIslandsOptions(const GenerationRequest &r)
		: channel_width(r.option("channel-width")), extra_islands(r.option("extra-islands")),
		  sandy_beaches(r.option("sandy-beaches") != 0), wheat(r.option("wheat-amount")),
		  wood(r.option("wood-amount")), stone(r.option("stone-amount")),
		  algae(r.option("algae-amount")), fruit(r.option("fruit-amount"))
	{
	}
};
GeneratorDefinition concreteIslandsDefinition();
