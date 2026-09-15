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
	std::vector<unsigned char> reserved, wheat, wood, objectives, crossings, growthRestricted;
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
bool bankFarm(Layout &, RegionBounds, bool timber);
void stampCrossings(Layout &, const CrossingSelection &, GenerationContext &);
bool furnishAndSettle(Game &, GenerationContext &, const Layout &);
std::string validate(const Game &, const GenerationContext &, const Layout &);
std::vector<GeneratorControl> resourceControls();
} // namespace FractalMaps
