// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Grid.h"
#include "Map.h"
#include "Resources.h"
#include "Sketch.h"
#include <climits>
#include <vector>
struct GenerationContext;
namespace MapGeneration
{
// Deposits placed by hand: starter kits, prizes, algae. These are the primitives a generator's
// own resource layout is built from; scatterResources (Resources.h) is the ambient layer that
// fills in between them.

/// Clear ground round every swarm, where nothing is planted or dug: the ring the workers walk
/// out through.
constexpr int kSwarmClearance = 2;

/// Grass with nothing on it: no deposit, no building, no unit.
bool clearGround(const Map &, int x, int y);

/// Grows a compact patch of one resource outward from a seed tile, breadth-first over the four
/// cardinal neighbours, onto tiles the predicate allows. Returns how many tiles it placed.
template <typename Eligible>
int growPatch(Map &map, const Torus &t, int seed, int type, int count, Eligible eligible)
{
	std::vector<unsigned char> queued(size_t(t.w) * t.h, 0);
	std::vector<int> frontier{seed};
	queued[seed] = 1;
	int placed = 0;
	const auto &steps = kCardinalSteps;
	for (size_t head = 0; head < frontier.size() && placed < count; ++head)
	{
		const int i = frontier[head], x = i % t.w, y = i / t.w;
		if (!eligible(i) || !map.isResourceAllowed(x, y, type))
			continue;
		map.setResource(x, y, type, 1);
		++placed;
		for (const auto &step : steps)
		{
			const int n = t.at(x + step[0], y + step[1]);
			if (!queued[n])
			{
				queued[n] = 1;
				frontier.push_back(n);
			}
		}
	}
	return placed;
}

/// The nearest eligible tile to a point, within a box; -1 if there is none.
template <typename Eligible>
int seedNear(const Torus &t, int ax, int ay, int within, Eligible eligible)
{
	int seed = -1, nearest = INT_MAX;
	for (int dy = -within; dy <= within; ++dy)
		for (int dx = -within; dx <= within; ++dx)
		{
			const int i = t.at(ax + dx, ay + dy);
			if (eligible(i) && dx * dx + dy * dy < nearest)
			{
				nearest = dx * dx + dy * dy;
				seed = i;
			}
		}
	return seed;
}

/// Where a kit's three deposits go: each grows from the nearest eligible tile to its point,
/// searched `within` tiles of it.
struct KitSeed
{
	int x, y, within;
};
struct Kit
{
	KitSeed wheat, wood, stone;
	int wheatTiles, woodTiles; // patch sizes
	int stoneRadius;           // a clump
};
/// A home's starter kit: a wheat patch, a wood patch and a stone clump, each from the nearest
/// eligible tile to its seed, in that order. A deposit whose seed finds no eligible tile is left
/// out, since the guarantee that follows tops a colony up.
template <typename Eligible>
void plantKit(Map &map, const Torus &t, GenerationContext &context, const Kit &kit,
			  Eligible eligible)
{
	if (const int seed = seedNear(t, kit.wheat.x, kit.wheat.y, kit.wheat.within, eligible);
		seed >= 0)
		growPatch(map, t, seed, CORN, kit.wheatTiles, eligible);
	if (const int seed = seedNear(t, kit.wood.x, kit.wood.y, kit.wood.within, eligible); seed >= 0)
		growPatch(map, t, seed, WOOD, kit.woodTiles, eligible);
	if (const int seed = seedNear(t, kit.stone.x, kit.stone.y, kit.stone.within, eligible);
		seed >= 0)
		placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w), STONE,
						   kit.stoneRadius);
}

/// A mask of the tiles within `clearance` of every colony's swarm footprint: the ground a kit
/// must leave alone.
std::vector<unsigned char> swarmSurroundings(const Torus &, const GenerationContext &,
											 int clearance = kSwarmClearance);

/// Clears every deposit within kSwarmClearance of a swarm's footprint, except tiles of `keep`
/// (a designed wall beside it, say).
void clearAroundSwarms(Map &, const GenerationContext &, const Torus &,
					   const std::vector<unsigned char> *keep = nullptr);

/// Where algae goes: any water, or only water this many steps offshore.
struct AlgaeBand
{
	int nearestOffshore = -1, farthestOffshore = -1;
	/// One clump per this many eligible water tiles at an amount of 100, each this big.
	int tilesPerClump = 40;
	int clumpRadius = 1;
	static AlgaeBand anyWater(int tilesPerClump = 40) { return {-1, -1, tilesPerClump, 1}; }
	static AlgaeBand shallows(int nearest, int farthest, int tilesPerClump = 90)
	{
		return {nearest, farthest, tilesPerClump, 1};
	}
};

/// Algae clumps over the band, scaled by the amount.
void seedAlgae(Map &, GenerationContext &, const Torus &, const char *stream, int algaePercent,
			   const AlgaeBand &);

/// Each island carries one themed prize at its middle: stone, a fruit or wheat, so finding one
/// feels like a distinct find rather than an interchangeable resource dump.
void stockIslands(Map &, GenerationContext &, const std::vector<Island> &, const char *stream);
} // namespace MapGeneration
