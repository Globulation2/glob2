// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationContext.h"
#include "HierarchicalCrossings.h"
#include "RecursiveGeometry.h"
#include "Sketch.h"
class Game;
namespace FractalMaps
{
using namespace MapGeneration;
// Economic policy shared only by the two fractal maps. It lives with the generators, not in
// the geometry toolkit: a 56-tile home and these crop budgets are design decisions, not engine rules.
constexpr int kHomeHalf = 28;
struct Home
{
	int x, y;
};
struct Layout
{
	Torus t{1, 1};
	TerrainSketch terrain;
	std::vector<Home> homes;
	std::vector<unsigned char> reserved, wheat, wood, objectives, crossings;
	/// The footprint of everything a garden path may lead to: home modules, beds, bank plots,
	/// lakes and crossing landings. Appended as each is laid; read by gardenPaths.
	std::vector<RegionBounds> features;
	/// Pairs of features already joined by something other than a path — the two landings of
	/// one crossing — so the path tree does not try to walk round the water between them.
	std::vector<std::pair<int, int>> featureLinks;
	/// Per feature, the axis a path must arrive along: 0 any, 1 horizontal, 2 vertical. A
	/// crossing's landings are met straight along the bridge.
	std::vector<int> featureAxis;
	/// Water whose shore gets spots of wheat: Hilbert's river, Gardens' central lake.
	std::vector<unsigned char> wheatShore;
	std::string failure;
};
void initialize(Layout &, const GenerationRequest &);
/// Bounded, deterministic maximin search over legal 8-tile lattice anchors. Tries every initial
/// anchor in a bounded shortlist; never changes seed or cuts terrain to make a home fit.
bool reserveHomes(Layout &, GenerationContext &, const std::vector<Home> &preferred = {},
				  GenerationTelemetry *observations = nullptr);
bool overlapsHome(const Layout &, RegionBounds, int margin = 0);
void layHomeEconomies(Layout &);
/// A sand-contained bank plot; only stamps existing unreserved grass, never a crossing.
/// Slides along the bank and tries once two tiles smaller before reporting an omission.
bool bankFarm(Layout &, RegionBounds, bool timber);
/// Square garden pools on a jittered lattice through the open land, each with crop plots on
/// two of its banks: the water these maps need away from their one fractal channel, in the
/// same square-on-a-grid language as the rest of the design. Only stamps clean unreserved
/// grass with a walking margin round it, so it can neither cut a designed route nor touch a
/// home module. Returns the number of pools placed.
int gardenBeds(Layout &, GenerationContext &, int spacing, int half, int margin = 6);
void stampCrossings(Layout &, const CrossingSelection &, GenerationContext &);
/// Square garden paths joining every recorded feature: a spanning tree built shortest edge
/// first, each edge an L or a Z of straight horizontal and vertical runs over open grass only,
/// so a path can meet a feature but never cross one. Call after the last feature is laid and
/// before the final shoreline pass. Returns the number of features left unjoined.
int gardenPaths(Layout &, GenerationContext &);
bool furnishAndSettle(Game &, GenerationContext &, const Layout &);
std::string validate(const Game &, const GenerationContext &, const Layout &);
std::vector<GeneratorControl> resourceControls();
} // namespace FractalMaps
