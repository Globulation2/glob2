// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Drawing.h"
#include "FertilityField.h"
#include "Grid.h"
#include "Sketch.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>
class Map;
namespace MapGeneration
{
// Where wheat and wood can spread: sketch fertility and finished-map containment.
//
// A crop spreads when a probe up to 15 tiles away on each axis finds pure water and the probe mirrored
// through the tile finds no pure sand (Map::growResources; Fertility::Field computes the exact chance
// for every tile). Planting.h's algaeGrowthChance answers the same question for algae on a finished
// map; the sketch operations answer it before anything is written, so a design can size
// its farmland to its water, keep a forest from ever growing back, or prove a region stays dry.

/// Existing wheat/wood seeds inside a tile mask (one entry per map tile). Use this after
/// resource guarantees and repairs to check that reserved building ground stayed seed-free.
int cropSeedsIn(const Map &, const std::vector<unsigned char> &region);

/// Conservative future crop footprint on a finished map: flood from all wheat/wood through
/// eight-connected pure grass, wrapping at map edges. Ignores fertility, buildings and other
/// deposits, so this is a containment test, not a prediction of growth speed or harvests.
/// Sand barriers stop the flood; sources themselves are included even on non-grass terrain.
/// Neither operation mutates the map or consumes random numbers.
Flood cropSpreadEnvelope(const Map &);

/// How far, on each axis, the engine's crop growth probe reaches for water.
constexpr int kCropProbeReach = 15;

/// Every tile's crop growth chance on Fertility::kScale, from the sketch as the game will draw it
/// (pure water and pure sand tiles, pureTiles). Unlike Fertility::forMap it is not gated on deposits
/// being able to reach a tile: this is where crops would regrow if planted.
Fertility::Field cropGrowthField(const TerrainSketch &, const Torus &);

/// The share (0 to 1) of `region`'s tiles whose chance is at least `minimum`; 0 for an empty region.
double wateredShare(const Fertility::Field &, const std::vector<unsigned char> &region,
					std::uint32_t minimum = 1);

/// How many of `region`'s tiles have any chance at all: a design that promises a region stays dry
/// (a forest that never grows back) checks this is 0.
int wetTiles(const Fertility::Field &, const std::vector<unsigned char> &region);

/// The tiles where water would give some tile of `region` a chance to regrow: every tile within
/// kCropProbeReach on each axis. Keep water out of it and the region stays dry.
std::vector<unsigned char> dryZone(const Torus &, const std::vector<unsigned char> &region);

/// Turns every water corner of the sketch inside `zone` to grass, and returns how many it changed. Run
/// it before layBeaches, since beaches are drawn from the water that is left.
int drainWithin(TerrainSketch &, const std::vector<unsigned char> &zone);

/// The mean crop growth chance (Fertility::kScale) over the square `radius` tiles round a site: what
/// a colony's fields there could regrow. Under about 2000 a start starves once its kit is cut (games
/// on real geography, 2026-09-15); a design that cannot move the site can dig it a pond (digPond).
std::uint32_t meanFertilityAround(const Fertility::Field &, const Torus &, int site, int radius);
/// meanFertilityAround for every tile at once, from running sums along rows then columns across the
/// wrap: the same integer means, in time independent of the radius. For a search that asks the
/// question of thousands of candidate sites.
std::vector<std::uint32_t> meanFertilityField(const Fertility::Field &, const Torus &, int radius);

/// A pond dug for a site that has no water: `corners` water corners grown (growWater) from the
/// roomiest eligible corner between `nearest` and `farthest` Chebyshev steps of `site`, nearest the
/// middle of that band on a tie, shaped by `noiseAt(corner)` (0 to 1) so it is a pond and not a
/// disc. The pond keeps `nearest` steps from the site on every side, so a swarm, its clearing and
/// its workers' exits stay dry, and grows only onto corners `eligible(corner)` allows (the site's own
/// ground, say, and not a road). Fewer corners than asked come back only when the eligible ground
/// ran out, and a pond under half the size asked is not dug at all, since a puddle waters nothing.
/// Returns how many corners it dug. A landscape with real water where the geography puts it, and
/// colonies where the fairness puts them, digs one of these wherever the two disagree.
template <typename Eligible, typename NoiseAt>
int digPond(TerrainSketch &sketch, const Torus &t, int site, int nearest, int farthest, int corners,
			Eligible eligible, NoiseAt noiseAt, std::vector<int> &queued, int stamp)
{
	const int n = t.size(), sx = site % t.w, sy = site / t.w;
	std::vector<unsigned char> roomy(size_t(n), 0), water(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		water[i] = sketch[i] == WATER;
		roomy[i] = !water[i] && eligible(i) && t.chebyshev(sx, sy, i % t.w, i / t.w) >= nearest;
	}
	// Room is steps from anything the pond may not touch, so the seed sits away from the site's
	// ground's edge and the pond grows into open ground rather than along a coast.
	std::vector<unsigned char> blocked(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		blocked[i] = !roomy[i];
	const std::vector<int> room = stepsFrom(t, blocked);
	const int enough = 1 + int(std::sqrt(corners / 3.14159265358979));
	const int middle = (nearest + farthest) / 2;
	int seed = -1;
	long long seedScore = 0;
	for (int i = 0; i < n; ++i)
	{
		const int d = t.chebyshev(sx, sy, i % t.w, i / t.w);
		if (!roomy[i] || d > farthest)
			continue;
		const long long score = 4LL * std::min(room[i], enough) - std::abs(d - middle);
		if (seed < 0 || score > seedScore)
		{
			seed = i;
			seedScore = score;
		}
	}
	if (seed < 0)
		return 0;
	const int grown = growWater(
		t, water, seed, corners, [&](int i) { return roomy[i] != 0; },
		[&](int i)
		{
			const double d = std::sqrt(double(t.dist2(seed % t.w, seed / t.w, i % t.w, i / t.w)));
			return std::int64_t(d * 1000) + std::int64_t(noiseAt(i) * 2500);
		},
		queued, stamp);
	if (grown * 2 < corners)
		return 0;
	int dug = 0;
	for (int i = 0; i < n; ++i)
		if (water[i] && sketch[i] != WATER)
		{
			sketch[i] = WATER;
			++dug;
		}
	return dug;
}
/// How a dry start is watered (waterDrySite): up to `ponds` ponds of `corners` corners each, the
/// first dug between `nearest` and `farthest` Chebyshev steps of the site and each next one `step`
/// steps farther out on both bounds, so it lands beyond the last.
struct DryStartPonds
{
	int ponds, corners, nearest, farthest, step;
};
/// What waterDrySite did: the site's mean crop growth chance before and after, and the corners each
/// attempt dug in order (a final 0 when the eligible ground ran out before the floor was met).
struct DryStartWatering
{
	std::uint32_t before = 0, after = 0;
	std::vector<int> dug;
};
/// Digs ponds (digPond) beside a site until the mean crop growth chance over the square `radius`
/// round it, measured on the sketch as the game will draw it (beaches laid, cropGrowthField),
/// reaches `floor`, or the ponds run out. A landscape that places colonies for fairness rather than
/// beside water (Continents, Central Quarry) waters the ones that landed dry. Attempt `p` stamps its
/// flood with `stampBase + p`; share `queued` across calls as digPond asks.
template <typename Eligible, typename NoiseAt>
DryStartWatering waterDrySite(TerrainSketch &sketch, const Torus &t, int site, int radius,
							  std::uint32_t floor, const DryStartPonds &plan, Eligible eligible,
							  NoiseAt noiseAt, std::vector<int> &queued, int stampBase)
{
	// The mean only reads tiles within `radius` of the site, a tile's growth chance only reads pure
	// water and sand within kCropProbeReach, and a beach and a pure tile only look a corner or two
	// away: so the sketch is cut to a window that reaches past all of them, and the window, a torus
	// of its own, gives exactly the whole map's answer without its wrap ever being read. Building the
	// whole map's field for every pond was most of the cost on 512 maps.
	const int half = radius + kCropProbeReach + 5;
	const bool windowed = 2 * half + 1 < t.w && 2 * half + 1 < t.h;
	const auto mean = [&]()
	{
		if (!windowed)
		{
			TerrainSketch drawn = sketch;
			layBeaches(drawn, t);
			return meanFertilityAround(cropGrowthField(drawn, t), t, site, radius);
		}
		const Torus window(2 * half + 1, 2 * half + 1);
		const int sx = site % t.w, sy = site / t.w;
		TerrainSketch drawn(size_t(window.size()));
		for (int y = 0; y < window.h; ++y)
			for (int x = 0; x < window.w; ++x)
				drawn[size_t(y * window.w + x)] = sketch[size_t(t.at(sx - half + x, sy - half + y))];
		layBeaches(drawn, window);
		return meanFertilityAround(cropGrowthField(drawn, window), window, half * window.w + half, radius);
	};
	DryStartWatering result;
	result.before = result.after = mean();
	for (int p = 0; result.after < floor && p < plan.ponds; ++p)
	{
		const int dug = digPond(sketch, t, site, plan.nearest + p * plan.step,
								plan.farthest + p * plan.step, plan.corners, eligible, noiseAt,
								queued, stampBase + p);
		result.dug.push_back(dug);
		if (dug == 0)
			break;
		result.after = mean();
	}
	return result;
}

// There is deliberately no helper here for the engine's saved canResourcesGrow flag.
// Generated maps may not disable resource growth anywhere: no-growth zones are for
// hand-made scenarios such as the tutorial, and validateGeneratedWorld refuses any generated
// world that has one. Contain crops with terrain — sand, or ground the water probe cannot
// reach — as docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md describes.

} // namespace MapGeneration
