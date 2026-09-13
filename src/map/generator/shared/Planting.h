// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GenerationContext.h"
#include "Grid.h"
#include "Map.h"
#include "Resources.h"
#include "Sketch.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <vector>
struct GenerationContext;
namespace MapGeneration
{
struct WedgeFrame;
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

/// A home's own frame for laying out its kit: an origin and a facing, so a kit designed once as
/// offsets along and across the facing lands the same way round every home.
struct KitFrame
{
	int x, y;
	double angle;
	/// The seed `along` tiles down the facing and `across` tiles to its left, searched `within`.
	KitSeed at(double along, double across, int within) const
	{
		return KitSeed{x + int(std::lround(along * std::cos(angle) - across * std::sin(angle))),
					   y + int(std::lround(along * std::sin(angle) + across * std::cos(angle))),
					   within};
	}
};

/// Farmland on chosen ground: the first `wheat` + `wood` of `tiles` (already in order of
/// preference, most fertile first, say) are taken, then ordered by `splitKey(tile)` and dealt
/// wheat first, in proportion. Splitting by a field unrelated to the preference makes the two
/// crops form separate patches rather than rings sorted by fertility.
template <typename SplitKey>
void plantFields(Map &map, const Torus &t, std::vector<int> tiles, int wheat, int wood,
				 SplitKey splitKey)
{
	const int total = std::min(int(tiles.size()), wheat + wood);
	if (total <= 0)
		return;
	tiles.resize(total);
	std::stable_sort(tiles.begin(), tiles.end(),
					 [&](int a, int b) { return splitKey(a) < splitKey(b); });
	const int wheatShare = int(std::int64_t(total) * wheat / std::max(1, wheat + wood));
	for (int k = 0; k < total; ++k)
		map.setResource(tiles[k] % t.w, tiles[k] / t.w, k < wheatShare ? CORN : WOOD, 1);
}

/// `count` clumps dropped on random tiles of `ground`: for each, up to `attempts` tiles are drawn
/// from `stream` and the first `eligible` one gets `place(point)`, which places the clump and may
/// draw its type and size from the same stream. Returns how many were placed.
template <typename Eligible, typename Place>
int scatterClumps(GenerationContext &context, const Torus &t, const std::vector<int> &ground,
				  int count, const char *stream, Eligible eligible, Place place, int attempts = 100)
{
	int placed = 0;
	for (int k = 0; k < count && !ground.empty(); ++k)
		for (int attempt = 0; attempt < attempts; ++attempt)
		{
			const int at = ground[context.bounded(stream, std::uint32_t(ground.size()))];
			if (eligible(at))
			{
				place(MapGeneratorPoint(at % t.w, at / t.w));
				++placed;
				break;
			}
		}
	return placed;
}

/// A mask of the tiles within `clearance` of every colony's swarm footprint: the ground a kit
/// must leave alone.
std::vector<unsigned char> swarmSurroundings(const Torus &, const GenerationContext &,
											 int clearance = kSwarmClearance);

/// Clears every deposit within kSwarmClearance of a swarm's footprint, except tiles of `keep`
/// (a designed wall beside it, say).
void clearAroundSwarms(Map &, const GenerationContext &, const Torus &,
					   const std::vector<unsigned char> *keep = nullptr);

/// The chance, per water tile, that algae there passes Map::growResources' test to grow or spread
/// when the engine visits it. The engine draws an offset of up to 15 tiles each way (the difference
/// of two draws of 0 to 15) and grows the algae only if the tile at that offset is water and the
/// tile at the offset turned a quarter and doubled is sand. So algae thrives in water near beaches
/// and sand, and algae with no sand within 30 tiles never grows at all. Land tiles get 0.
std::vector<double> algaeGrowthChance(const Map &, const Torus &);

/// Where algae goes: any water, or only water this many steps offshore.
struct AlgaeBand
{
	int nearestOffshore = -1, farthestOffshore = -1;
	/// One clump per this many eligible water tiles at an amount of 100, each this big.
	int tilesPerClump = 40;
	int clumpRadius = 1;
	/// Above 0, the clumps counted over the whole band all go on this share of it where
	/// algaeGrowthChance is highest, so they sit where algae can regrow.
	double bestShare = 0;
	/// The same band, with its clumps placed on its best-growing `share` of water.
	AlgaeBand thriving(double share) const
	{
		AlgaeBand band = *this;
		band.bestShare = share;
		return band;
	}
	static AlgaeBand anyWater(int tilesPerClump = 40) { return {-1, -1, tilesPerClump, 1}; }
	static AlgaeBand shallows(int nearest, int farthest, int tilesPerClump = 90)
	{
		return {nearest, farthest, tilesPerClump, 1};
	}
};

/// Algae clumps over the band, scaled by the amount. With a wedge frame the clumps are shared out
/// equally between the colonies' wedges, each wedge's placed on its own best-growing water: the
/// engine's growth test is not symmetric under rotation (it turns its offset by reflecting it), so
/// one best share taken over the whole map would favour some wedges' water over others'.
void seedAlgae(Map &, GenerationContext &, const Torus &, const char *stream, int algaePercent,
			   const AlgaeBand &, const WedgeFrame *wedges = nullptr);

/// Each island carries one themed prize at its middle: stone, a fruit or wheat, so finding one
/// feels like a distinct find rather than an interchangeable resource dump.
void stockIslands(Map &, GenerationContext &, const std::vector<Island> &, const char *stream);
} // namespace MapGeneration
