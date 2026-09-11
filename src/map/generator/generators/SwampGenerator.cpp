// SPDX-License-Identifier: GPL-3.0-or-later
#include "SwampGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
using namespace MapGeneration;
static bool generate(Game &game, GenerationContext &context)
{
	const SwampOptions options(context.request);
	HeightFieldOptions terrain{
		options.water, 0, options.grass, 0, options.smoothing, options.fruit, options.repeat, true};
	readResourceControls(terrain, context.request);
	if (!generateHeightField(game, context, terrain,
							 [&](HeightMap &hm, unsigned w, unsigned h, float smoothing)
							 { hm.makeSwamp(smoothing); }))
		return false;
	context.stage = "starts";
	if (!placeStarts(game, context))
		return false;
	openStartsBuriedByAmounts(game, context, terrain);
	return true;
}

GeneratorDefinition swampDefinition()
{
	std::vector<GeneratorControl> controls{
		{"water", "Water weight", 0, 100, 1, 35, ControlGroup::Terrain, false, true},
		{"grass", "Grass weight", 0, 100, 1, 60, ControlGroup::Terrain, false, true},
		{"smoothing", "Smoothing", 1, 8, 1, 6, ControlGroup::Terrain, false},
		{"fruit", "Fruit", 0, 64, 1, 4, ControlGroup::Resources, false}};
	for (auto &c : heightFieldResourceControls())
		controls.push_back(std::move(c));
	controls.push_back({"repeat", "Repeat landscape", 0, 5, 1, 0, ControlGroup::Layout, true});
	return {"swamp", 1, "Swamp", 2, false, std::move(controls), generate};
}
