// SPDX-License-Identifier: GPL-3.0-or-later
#include "SwampGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Pipeline.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
using namespace MapGeneration;
// Swamp (id 1): Leo Wandersleb's swamp from January 2006, on the shared height-field pipeline (see
// Terrain.cpp). The field is almost pure noise (makeSwamp), so the water is scattered ponds and
// sloughs everywhere rather than one shape.
//
// Swamp has no sand or desert weight: water is water / (1 + water + grass) of the map, 36% at the
// defaults, and the rest is grass (the 1 only guards against both weights at 0). Its only sand is
// the beaches controlSand has to lay, because grass may not touch water, so every pond is ringed
// in sand. With water everywhere, wheat and wood can regrow almost anywhere, and a swamp's economy
// is rich; the cost is that movement and building room are broken up by ponds a colony must walk
// round until it can swim. Smoothing defaults to 6 (features about 30 tiles) so ponds are big
// enough to read, and grass between them wide enough to build on.
static bool generate(Game &game, GenerationContext &context)
{
	const HeightFieldOptions terrain = HeightFieldOptions::fromRequest(context.request, true);
	if (!generateHeightField(game, context, terrain,
							 [&](HeightMap &hm, unsigned w, unsigned h, float smoothing)
							 {
								 context.telemetry.choice("swamp.field.shape", "noise");
								 context.telemetry.measure("swamp.field.smoothing", smoothing);
								 hm.makeSwamp(smoothing);
							 }))
		return false;
	context.stage = "starts";
	if (!placeStarts(game, context))
		return false;
	// The resource bands are painted from map-wide noise with no awareness of where any colony
	// starts, so they can wall one into a pocket with nowhere to build — a raised amount widens
	// them, but the defaults manage it too. placeStarts has already carved out the swarm's own
	// rectangle by now, so nothing needs to be kept clear for it.
	openStartsBuriedByResources(game, context);
	return true;
}

// Nothing here designs an economy: the height field decides where land is and placeStarts picks
// the best spots it left. So the only promise this landscape can make is the floor every colony
// needs — wheat and wood it can walk to, and room for a first base — and the service rolls
// another seed when a field does not leave one.
static std::string validateWorld(const Game &game, const GenerationContext &context)
{
	return startingFloorFailure(game.map, context.request.nbTeams);
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
	return {"swamp", 1, "Swamp", 3, false, std::move(controls), generate, true, nullptr,
			validateWorld};
}
