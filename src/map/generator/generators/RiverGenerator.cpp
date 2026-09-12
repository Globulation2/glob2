// SPDX-License-Identifier: GPL-3.0-or-later
#include "RiverGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
using namespace MapGeneration;
static bool generate(Game &game, GenerationContext &context)
{
	const RiverOptions options(context.request);
	HeightFieldOptions terrain{options.water,     options.sand,  options.grass,  options.desert,
							   options.smoothing, options.fruit, options.repeat, false};
	readResourceControls(terrain, context.request);
	if (!generateHeightField(
			game, context, terrain, [&](HeightMap &hm, unsigned w, unsigned h, float smoothing)
			{ hm.makeRiver(options.river_width * (w + h) / 2 / 100, smoothing, options.winding); }))
		return false;
	context.stage = "starts";
	if (!placeStarts(game, context))
		return false;
	openStartsBuriedByAmounts(game, context, terrain);
	return true;
}

GeneratorDefinition riverDefinition()
{
	std::vector<GeneratorControl> controls{
		{"water", "Water weight", 0, 100, 1, 45, ControlGroup::Terrain, false, true},
		{"sand", "Sand weight", 0, 100, 1, 3, ControlGroup::Terrain, false, true},
		{"grass", "Grass weight", 0, 100, 1, 75, ControlGroup::Terrain, false, true},
		{"desert", "Desert weight", 0, 100, 1, 0, ControlGroup::Terrain, false, true},
		{"smoothing", "Smoothing", 1, 8, 1, 4, ControlGroup::Terrain, false},
		{"river-width", "River width", 20, 65, 5, 35, ControlGroup::Terrain, false},
		// Off, the river runs straight across the map instead of meandering.
		GeneratorControl::toggle("winding-river", "Winding river", true, ControlGroup::Terrain),
		{"fruit", "Fruit", 0, 64, 1, 4, ControlGroup::Resources, false}};
	for (auto &c : heightFieldResourceControls())
		controls.push_back(std::move(c));
	controls.push_back({"repeat", "Repeat landscape", 0, 5, 1, 0, ControlGroup::Layout, true});
	return {"river", 2, "River", 2, false, std::move(controls), generate};
}
