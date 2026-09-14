// SPDX-License-Identifier: GPL-3.0-or-later
#include "CraterLakesGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
using namespace MapGeneration;
// Crater lakes (id 4): Leo Wandersleb's crater map from January 2006, on the shared height-field
// pipeline (see Terrain.cpp). The field is round bowls pressed at random into high ground and then
// roughened (makeCraters in HeightMap.cpp).
//
// Lake density sets the bowl count, width * height * density / 30000, so it scales with map area
// (54 bowls at 256x256 and the default 25); lake size is each bowl's radius in tiles. The water
// weight (25, about 23% of the map at the defaults) then decides how deep the lakes fill. Because
// the water share is fixed, the two controls only share it out. Renders at 256x256, seed 1, water
// about 20% in every case: density 10 gives about 20 lakes, big and far apart; density 50 about a
// hundred small ponds; lake size 40 gives round, clean-edged lakes, since the wide bowls outweigh
// the noise; lake size 10 gives narrower bowls, and the noise then floods its own hollows into
// ragged lakes between them.
//
// Game rules: lakes are local obstacles, not barriers between colonies, so this plays as an open
// land map where water shapes the paths; farmland rings each lake, since wheat and wood regrow only
// near water.
static bool generate(Game &game, GenerationContext &context)
{
	const CraterLakesOptions options(context.request);
	const HeightFieldOptions terrain = HeightFieldOptions::fromRequest(context.request, false);
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
