// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationRequest.h"
#include "GeneratorDefinition.h"
struct ShatteredCoastOptions
{
	int water, sand, grass, smoothing;
	bool colony_meadows;
	// 2.0: the coast pass over the finished revision-8 map (deep cores, mudflats, rock).
	bool deepWater, mudflats, outcrops;
	int climate; // 0 random, 1 temperate, 2 steppe, 3 tropical (looks only)
	int wheat, wood, stone, algae; // percentages of the default amounts
	explicit ShatteredCoastOptions(const GenerationRequest &r)
		: water(r.option("water")), sand(r.option("sand")), grass(r.option("grass")),
		  smoothing(r.option("smoothing")), colony_meadows(r.option("colony-meadows") != 0),
		  deepWater(r.option("deep-water") != 0), mudflats(r.option("mudflats") != 0),
		  outcrops(r.option("outcrops") != 0), climate(r.option("climate")),
		  wheat(r.option("wheat-amount")), wood(r.option("wood-amount")),
		  stone(r.option("stone-amount")), algae(r.option("algae-amount"))
	{
	}
	int percent(int resourceType) const
	{
		return resourceType == WHEAT    ? wheat
			   : resourceType == WOOD  ? wood
			   : resourceType == STONE ? stone
									   : algae;
	}
};
GeneratorDefinition shatteredCoastDefinition();
