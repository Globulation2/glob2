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

/// Place a compact resource patch near an intended gathering point. Search and
/// growth share the same eligibility mask, so a fallback seed cannot cross into
/// protected home ground. Returns the actual count (including zero when no seed
/// fits), letting callers distinguish an optional deposit from a required kit.
/// This deliberately makes no random draws: equal translated entrances get the
/// same bounded search and cardinal growth order.
template <typename Eligible>
int plantPatchNear(Map &map, const Torus &t, const KitSeed &at, int type, int count,
				   Eligible eligible)
{
	const int seed = seedNear(t, at.x, at.y, at.within, eligible);
	return seed < 0 ? 0 : growPatch(map, t, seed, type, count, eligible);
}
struct Kit
{
	KitSeed wheat, wood, stone;
	int wheatTiles, woodTiles; // patch sizes
	int stoneRadius;           // a clump; below 0 for none
};
/// A home's starter kit: a wheat patch, a wood patch and a stone clump (none with a negative radius), each from the nearest
/// eligible tile to its seed, in that order. A deposit whose seed finds no eligible tile is left
/// out, since the guarantee that follows tops a colony up.
template <typename Eligible>
void plantKit(Map &map, const Torus &t, GenerationContext &context, const Kit &kit,
			  Eligible eligible)
{
	plantPatchNear(map, t, kit.wheat, WHEAT, kit.wheatTiles, eligible);
	plantPatchNear(map, t, kit.wood, WOOD, kit.woodTiles, eligible);
	if (kit.stoneRadius < 0)
		return;
	if (const int seed = seedNear(t, kit.stone.x, kit.stone.y, kit.stone.within, eligible);
		seed >= 0)
		placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w), STONE,
						   kit.stoneRadius);
}

/// A kit whose crops and quarry go on different ground: the wheat and wood patches on tiles
/// `cropsEligible` allows and the stone clump on tiles `stoneEligible` allows (a farm tail and a
/// town head, a watered chamber and a dry one), each from the nearest such tile to its seed.
template <typename CropsEligible, typename StoneEligible>
void plantSplitKit(Map &map, const Torus &t, GenerationContext &context, const Kit &kit,
				   CropsEligible cropsEligible, StoneEligible stoneEligible)
{
	Kit crops = kit;
	crops.stoneRadius = -1;
	plantKit(map, t, context, crops, cropsEligible);
	if (kit.stoneRadius < 0)
		return;
	if (const int seed = seedNear(t, kit.stone.x, kit.stone.y, kit.stone.within, stoneEligible);
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
		map.setResource(tiles[k] % t.w, tiles[k] / t.w, k < wheatShare ? WHEAT : WOOD, 1);
}

/// Dense cover: one tile of `type` on every tile of `region` that `eligible(tile)` allows and the engine
/// accepts, in index order. A forest that fills a continent, a wheat plain, the undergrowth of a
/// swamp; what stays open is whatever `eligible` refuses (roads, clearings, a pattern's gaps). Every
/// deposit is a clearable wall until workers cut it, so a map built on cover should say what keeps
/// its colonies connected (openColonyRoutes, Roads.h). Returns how many tiles it planted.
template <typename Eligible>
int plantCover(Map &map, const Torus &t, const std::vector<unsigned char> &region, int type,
			   Eligible eligible)
{
	int planted = 0;
	for (int i = 0; i < t.w * t.h; ++i)
	{
		const int x = i % t.w, y = i / t.w;
		if (!region[i] || !eligible(i) || !map.isResourceAllowed(x, y, type))
			continue;
		map.setResource(x, y, type, 1);
		++planted;
	}
	return planted;
}

/// Clumps round a circle, the same at every angle: at each of `angles` (radians) round (cx, cy), a
/// clump of `type` and `clumpRadius` grown from the eligible tile nearest the point on the circle of
/// `radius`, searched within `within` tiles. A prize or an outcrop designed once per colony lands the
/// same way at every colony's angle. Returns how many were placed.
template <typename Eligible>
int plantRound(Map &map, const Torus &t, GenerationContext &context, double cx, double cy,
			   double radius, const std::vector<double> &angles, int type, int clumpRadius,
			   int within, Eligible eligible)
{
	int planted = 0;
	for (const double a : angles)
	{
		const int seed = seedNear(t, int(std::lround(cx + radius * std::cos(a))),
								  int(std::lround(cy + radius * std::sin(a))), within, eligible);
		if (seed < 0)
			continue;
		placeResourceClump(map, context, MapGeneratorPoint(seed % t.w, seed / t.w), type,
						   clumpRadius);
		++planted;
	}
	return planted;
}

/// An orchard of the three fruits round a circle: at each of `angles` (radians) round (cx, cy), a grove
/// of cherries, one of oranges and one of prunes, `spacing` tiles apart along the circle of `radius`,
/// each a clump of `clumpRadius` grown from the eligible tile nearest its point within `within`. Every
/// colony finds the same three fruits at its own angle, which is how a shared prize stays fair.
/// Returns how many groves were planted.
template <typename Eligible>
int plantOrchard(Map &map, const Torus &t, GenerationContext &context, double cx, double cy,
				 double radius, const std::vector<double> &angles, double spacing, int within,
				 int clumpRadius, Eligible eligible)
{
	int planted = 0;
	for (const double angle : angles)
		for (int fruit = 0; fruit < 3; ++fruit)
			planted += plantRound(map, t, context, cx, cy, radius,
								  {angle + (fruit - 1) * spacing / std::max(1.0, radius)},
								  CHERRY + fruit, clumpRadius, within, eligible);
	return planted;
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

/// Clears every deposit on the land tiles of `region`, except tiles of `keep` (a designed wall):
/// the ground a design promised to leave open (a ford's landings, a lane past a swarm), whatever a
/// later layer dropped there. Algae on water is left alone. Returns how many tiles were cleared.
int clearDeposits(Map &, const Torus &, const std::vector<unsigned char> &region,
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
/// The same, with the clumps shared out equally between labelled groups of water that are not wedges
/// (every colony's own bay, say): `groupOf` gives each tile's group, 0 to `groups` - 1, or -1 for
/// water that belongs to none and takes no clump.
void seedAlgae(Map &, GenerationContext &, const Torus &, const char *stream, int algaePercent,
			   const AlgaeBand &, const std::vector<int> &groupOf, int groups);

/// Each island carries one themed prize at its middle: stone, a fruit or wheat, so finding one
/// feels like a distinct find rather than an interchangeable resource dump.
void stockIslands(Map &, GenerationContext &, const std::vector<Island> &, const char *stream);
} // namespace MapGeneration
