// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "TerrainType.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
class Map;
struct GenerationContext;
namespace MapGeneration
{
/// A map's undermap as one TerrainType per tile, designed in memory before anything is written
/// to the Map: the buffer a designed generator stamps its terrain into, lays beaches on, carves
/// lakes and raises islands in, and finally writes out with writeUndermap.
using TerrainSketch = std::vector<unsigned char>;

/// Grass may never touch water (Map::regenerateMap reads each tile from its four undermap
/// corners), so every land tile beside water becomes sand. Unlike Map::controlSand() this reads
/// only the original terrain, so the result doesn't depend on scan order and water is never
/// eaten away.
void layBeaches(TerrainSketch &, const Torus &);

/// Writes the sketch to the map's undermap and rebuilds the tiles from it.
void writeUndermap(Map &, const TerrainSketch &);

int countTiles(const TerrainSketch &, TerrainType);

/// Decorative patches of sand inside the land, away from any shore: of the grass tiles `eligible`
/// allows that lie at least `inland` steps from water, the `share` (0 to 1) with the highest
/// `noiseAt(tile)` turn to sand. Sampling smooth noise gives a few rounded patches rather than
/// speckle, and `inland` keeps a strip of grass between every patch and its beach, so the land
/// still reads as grass with the odd dry clearing. A generator that stamps one design into every
/// colony's wedge samples its noise in the wedge frame, so every colony gets the same patches.
/// Sand holds no deposit and no building, so patches are laid before resources and kept small.
template <typename NoiseAt>
void sprinkleSand(TerrainSketch &sketch, const Torus &t, const std::vector<unsigned char> &eligible,
				  double share, int inland, NoiseAt noiseAt)
{
	std::vector<unsigned char> water(sketch.size(), 0);
	for (size_t i = 0; i < sketch.size(); ++i)
		water[i] = sketch[i] == WATER;
	const std::vector<int> steps = stepsFrom(t, water);
	std::vector<std::pair<double, int>> ranked;
	for (int i = 0; i < t.size(); ++i)
		if (eligible[i] && sketch[i] == GRASS && steps[i] >= inland)
			ranked.push_back({-noiseAt(i), i});
	std::stable_sort(ranked.begin(), ranked.end());
	const size_t patches = size_t(std::lround(ranked.size() * std::clamp(share, 0.0, 1.0)));
	for (size_t k = 0; k < patches; ++k)
		sketch[ranked[k].second] = SAND;
}

/// A small island raised out in open water: its centre, how far its outline can reach, and
/// every tile it covers.
struct Island
{
	int x, y;
	double reach;
	std::vector<int> tiles;
};

struct IslandPlacement
{
	const char *stream;
	/// How many islands to raise; each costs at most attemptsPerIsland candidate draws.
	int wanted;
	int attemptsPerIsland;
	/// Water kept between an island and any coast, and between two islands.
	double moat;
};

/// Small islands out in the open water, each well clear of every coast and of each other, so
/// they are only ever reached by swimming. Islands never shrink below their size on a 128-tile
/// map: any smaller and the beach leaves no grass for a prize.
std::vector<Island> raiseIslands(TerrainSketch &, const Torus &, GenerationContext &,
								 const IslandPlacement &);
} // namespace MapGeneration
