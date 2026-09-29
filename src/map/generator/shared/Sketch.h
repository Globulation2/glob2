// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "TerrainType.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
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

/// The undermap corners of every tile in `tiles`: the reverse of pureTiles, the vertices a design must
/// set for those tiles to take a terrain of their own.
std::vector<unsigned char> tileCorners(const Torus &, const std::vector<unsigned char> &tiles);

/// The tiles whose four undermap corners all hold `type` (Map::regenerateMap reads tile (x, y) from
/// corners (x, y), (x + 1, y), (x, y + 1) and (x + 1, y + 1)): what the sketch will draw as pure grass,
/// sand or water. Buildings and deposits need pure grass, and the engine's growth tests read pure
/// water and pure sand, so this is the sketch as the game will see it.
std::vector<unsigned char> pureTiles(const TerrainSketch &, const Torus &, TerrainType type);

/// The same pure-terrain mask from a finished map's rendered tiles. Use after all terrain repairs
/// when validating a growth or shoreline promise; mixed beach tiles belong to none of the masks.
std::vector<unsigned char> pureTiles(const Map &, TerrainType);

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

/// A sand road kept inland: clears every road vertex fewer than `gap` steps from `water`. Sand that
/// touches a beach carries the sea's margin along it, and stone only stands on grass, so a road that
/// reached a walled coast would open a gap in the wall.
void keepRoadInland(std::vector<unsigned char> &road, const Torus &,
					const std::vector<unsigned char> &water, int gap);

/// The tiles a sand road spoils for building and deposits: a tile takes its terrain from its four
/// corners, so every tile touching one of the road's vertices is no longer pure grass.
std::vector<unsigned char> roadTiles(const Torus &, const std::vector<unsigned char> &road);

/// Grows one body of water to exactly `target` tiles from its seed, always taking the frontier tile
/// with the lowest `key(tile)` next, so it fills a hollow the way water would: a key of depth plus
/// distance from the seed keeps the outline round while the depth shapes it. The frontier moves
/// over the four cardinal neighbours onto tiles `eligible(tile)` allows, and never onto a tile
/// already in `water`; the seed itself is taken whatever `eligible` says. `queued` is scratch the
/// caller owns and `stamp` a value no earlier call used, so many bodies can share one buffer
/// without clearing it. Returns how many tiles it added; fewer than `target` only when the
/// eligible ground ran out.
template <typename Eligible, typename Key>
int growWater(const Torus &t, std::vector<unsigned char> &water, int seed, int target,
			  Eligible eligible, Key key, std::vector<int> &queued, int stamp)
{
	using Entry = std::pair<long long, int>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> frontier;
	frontier.push({key(seed), seed});
	queued[seed] = stamp;
	int grown = 0;
	while (!frontier.empty() && grown < target)
	{
		const int tile = frontier.top().second;
		frontier.pop();
		if (water[tile])
			continue;
		water[tile] = 1;
		++grown;
		for (const auto &s : kCardinalSteps)
		{
			const int next = t.at(tile % t.w + s[0], tile / t.w + s[1]);
			if (eligible(next) && !water[next] && queued[next] != stamp)
			{
				queued[next] = stamp;
				frontier.push({key(next), next});
			}
		}
	}
	return grown;
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
