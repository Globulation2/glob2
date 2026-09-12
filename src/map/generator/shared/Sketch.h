// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "TerrainType.h"
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
