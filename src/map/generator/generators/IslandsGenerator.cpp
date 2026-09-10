// SPDX-License-Identifier: GPL-3.0-or-later
#include "IslandsGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
using namespace MapGeneration;
static bool generate(Game &game, GenerationContext &context)
{
	const IslandsOptions options(context.request);
	const HeightFieldOptions terrain{
		options.water,	   options.sand,  options.grass,  options.desert,
		options.smoothing, options.fruit, options.repeat, false};
	if (!generateHeightField(game, context, terrain,
							 [&](HeightMap &hm, unsigned w, unsigned h, float smoothing)
							 {
								 hm.makeIslands(
									 std::max(1, (context.request.nbTeams + options.extra_islands) /
													 (1 << options.repeat)),
									 smoothing);
							 }))
		return false;
	context.stage = "starts";
	return placeStarts(game, context);
}

GeneratorDefinition islandsDefinition()
{
	return {"islands",
			3,
			"Islands",
			1,
			false,
			{{"water", "Water weight", 0, 100, 1, 55, ControlGroup::Terrain, false, true},
			 {"sand", "Sand weight", 0, 100, 1, 3, ControlGroup::Terrain, false, true},
			 {"grass", "Grass weight", 0, 100, 1, 75, ControlGroup::Terrain, false, true},
			 {"desert", "Desert weight", 0, 100, 1, 0, ControlGroup::Terrain, false, true},
			 {"smoothing", "Smoothing", 1, 8, 1, 4, ControlGroup::Terrain, false},
			 {"extra-islands", "Extra islands", 0, 8, 1, 0, ControlGroup::Terrain, false},
			 {"fruit", "Fruit", 0, 64, 1, 4, ControlGroup::Resources, false},
			 {"repeat", "Repeat landscape", 0, 5, 1, 0, ControlGroup::Layout, true}},
			generate};
}
