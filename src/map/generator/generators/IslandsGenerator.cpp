// SPDX-License-Identifier: GPL-3.0-or-later
#include "IslandsGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "Pipeline.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
using namespace MapGeneration;
// Islands (id 3): Leo Wandersleb's island map from January 2006, one of the four maps built on the
// shared height-field pipeline (see Terrain.cpp for how a field becomes terrain and resources, and
// makeIslands in HeightMap.cpp for the shape).
//
// The field gets one hill per colony plus the extra islands, divided among the repeat patches, and
// the water weight sinks it until the requested share is sea. At the defaults (water 55, sand 3,
// grass 75, fruit 4) that is about 40% water, 55% grass and a few percent desert on the peaks.
// Nothing forces one island per colony: hills that overlap cancel into channels, noise breaks
// coasts, and the balanced start search picks the sites. So two colonies can share an island.
//
// Game rules: water blocks ground units until a colony trains swimmers, so the channels are a
// timer on first contact rather than a wall (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md).
// Wheat and wood lie on the grass just above the shore, where they can regrow.
static bool generate(Game &game, GenerationContext &context)
{
	const IslandsOptions options(context.request);
	const HeightFieldOptions terrain = HeightFieldOptions::fromRequest(context.request, false);
	if (!generateHeightField(game, context, terrain,
							 [&](HeightMap &hm, unsigned w, unsigned h, float smoothing)
							 {
								 context.telemetry.measure(
									 "islands.hills.requested",
									 std::max(1, (context.request.nbTeams + options.extra_islands) /
													 (1 << options.repeat)));
								 hm.makeIslands(
									 std::max(1, (context.request.nbTeams + options.extra_islands) /
													 (1 << options.repeat)),
									 smoothing);
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

GeneratorDefinition islandsDefinition()
{
	std::vector<GeneratorControl> controls{
		{"water", "Water weight", 0, 100, 1, 55, ControlGroup::Terrain, false, true},
		{"sand", "Sand weight", 0, 100, 1, 3, ControlGroup::Terrain, false, true},
		{"grass", "Grass weight", 0, 100, 1, 75, ControlGroup::Terrain, false, true},
		{"desert", "Desert weight", 0, 100, 1, 0, ControlGroup::Terrain, false, true},
		{"smoothing", "Smoothing", 1, 8, 1, 4, ControlGroup::Terrain, false},
		{"extra-islands", "Extra islands", 0, 8, 1, 0, ControlGroup::Terrain, false},
		{"fruit", "Fruit", 0, 64, 1, 4, ControlGroup::Resources, false}};
	for (auto &c : heightFieldResourceControls())
		controls.push_back(std::move(c));
	controls.push_back({"repeat", "Repeat landscape", 0, 5, 1, 0, ControlGroup::Layout, true});
	return {"islands", 3, "Islands", 3, false, std::move(controls), generate, true, nullptr,
			validateWorld};
}
