// SPDX-License-Identifier: GPL-3.0-or-later
#include "CraterLakesGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
using namespace MapGeneration;
static bool generate(Game &game, GenerationContext &context)
{
	const CraterLakesOptions options(context.request);
	HeightFieldOptions terrain{
		options.water,	   options.sand,  options.grass,  options.desert,
		options.smoothing, options.fruit, options.repeat, false};
	readResourceControls(terrain, context.request);
	if (!generateHeightField(game, context, terrain,
							 [&](HeightMap &hm, unsigned w, unsigned h, float smoothing)
							 {
								 hm.makeCraters(w * h * options.lake_density / 30000,
												options.lake_size, smoothing);
							 }))
		return false;
	context.stage = "starts";
	if (!placeStarts(game, context))
		return false;
	openStartsBuriedByAmounts(game, context, terrain);
	return true;
}

GeneratorDefinition craterLakesDefinition()
{
	std::vector<GeneratorControl> controls{
		{"water", "Water weight", 0, 100, 1, 25, ControlGroup::Terrain, false, true},
		{"sand", "Sand weight", 0, 100, 1, 3, ControlGroup::Terrain, false, true},
		{"grass", "Grass weight", 0, 100, 1, 75, ControlGroup::Terrain, false, true},
		{"desert", "Desert weight", 0, 100, 1, 0, ControlGroup::Terrain, false, true},
		{"smoothing", "Smoothing", 1, 8, 1, 6, ControlGroup::Terrain, false},
		{"lake-density", "Lake density", 10, 50, 5, 25, ControlGroup::Terrain, false},
		{"lake-size", "Lake size", 10, 40, 5, 25, ControlGroup::Terrain, false},
		{"fruit", "Fruit", 0, 64, 1, 4, ControlGroup::Resources, false}};
	for (auto &c : heightFieldResourceControls())
		controls.push_back(std::move(c));
	controls.push_back({"repeat", "Repeat landscape", 0, 5, 1, 0, ControlGroup::Layout, true});
	return {"crater-lakes", 4, "Crater lakes", 2, false, std::move(controls), generate};
}
