// SPDX-License-Identifier: GPL-3.0-or-later
#include "RiverGenerator.h"
#include "Game.h"
#include "GenerationContext.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include <algorithm>
using namespace MapGeneration;
// River (id 2): Leo Wandersleb's river map from January 2006 (nct fixed it on merge, giszmo patched
// gaps in the river bed a week later), on the shared height-field pipeline (see Terrain.cpp). The
// shape is one river bed that loops across the whole torus (makeRiver in HeightMap.cpp).
//
// River width is the bed's diameter as a percentage of the mean side: 35% is about 90 tiles on a
// 256x256 map. The water weight, not the width, sets how much of the map floods (35% at the
// defaults), so the two trade off. Renders at 256x256, seed 3: at width 20 the bed holds a fraction
// of the water and the rest floods the noise's hollows as ponds all over both banks; at 35 it is
// one river with a few ponds; at 65 the whole share fits in the bed, whose gentle slopes turn the
// resource bands into long parallel stripes of stone, wheat and wood along each bank, with dry,
// resource-less grass beyond. The default sits where the river reads as a river yet ponds still
// bring water, and so regrowing farmland, inland. The winding switch only removes the meander;
// everything else stays the same.
//
// Game rules: the river splits the map into banks that meet only across water, so colonies on
// opposite banks cannot fight until they swim, and the banks are the best farmland on the map
// (wheat and wood regrow only near water).
static bool generate(Game &game, GenerationContext &context)
{
	const RiverOptions options(context.request);
	const HeightFieldOptions terrain = HeightFieldOptions::fromRequest(context.request, false);
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
