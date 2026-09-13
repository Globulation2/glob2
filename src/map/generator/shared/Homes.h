// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Drawing.h"
#include "FertilityField.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Planting.h"
#include "Resources.h"
#include "Territories.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>
namespace MapGeneration
{
// Round homes: a home that stands alone as a round patch of ground turned to face along `axis`, its door
// on the side opposite: out from the map's middle on a ring of homes (Carousel, Switchbacks), or away
// from the way in on a map with no middle. The swarm stands in from the middle towards the door, and the
// home's starter kit lies within a short walk of it. A round home may carry ponds of its own - one out
// behind the swarm and a second on a flank - for a map that gives its homes no other water; every pond
// keeps a gap of land to the home's edge, so where the home is a coast its beach never meets the sea's.
// Carousel's homes have no ponds (their farms are their water); Switchbacks' have two only on a map too
// small for farms.

/// How far a round home's first pond lies from its middle beyond its own radius, and its swarm from its
/// middle: a short walk apart however big the home, so the kit planted round the pond is near the swarm.
constexpr double kHomeSwarmReach = 8;
/// A pond's radius as a share of its home's, clamped; the land a pond keeps from the home's edge; and
/// the room a home needs beyond those for its swarm.
constexpr double kHomePondShare = 0.18;
constexpr double kHomeSmallestPond = 2.5;
constexpr double kHomeLargestPond = 7;
constexpr double kHomePondGap = 5;
constexpr double kHomeSwarmRoom = 4;

/// The radius of a round home's ponds, and of the circle its kit is planted round.
inline double homePondRadius(double homeRadius)
{
	return std::clamp(kHomePondShare * homeRadius, kHomeSmallestPond, kHomeLargestPond);
}

/// Whether a round home is big enough for its swarm, its kit and a pond with its gap to the edge. The
/// same floor holds whether or not the home has ponds, so a design refuses the same crowded maps.
inline bool homeHasRoom(double homeRadius)
{
	return homeRadius >= homePondRadius(homeRadius) + kHomePondGap + kHomeSwarmRoom;
}

/// Stamps a round home: `visit(tile)` for every tile of the `home` outline centred at `centre` and
/// turned to `axis` (the heading away from its door; out from the map's middle on a ring), and `ponds` (0 to 2) of the `pond` outline
/// into `pondMask`, turned the same way; `pond` may be null when there are none. Returns where the kit
/// goes: the first pond's centre, or the home's middle without ponds.
template <typename Visit>
ShapePoint stampRoundHome(const Torus &t, ShapePoint centre, double axis, const RadialShape &home,
						  double radius, int ponds, const RadialShape *pond,
						  std::vector<unsigned char> &pondMask, Visit visit)
{
	forEachTileInShape(t, centre.x, centre.y, home, axis, [&](int i, double, double) { visit(i); });
	// The first pond lies out from the middle, as far out as the edge gap allows but never more than a
	// short walk from the swarm, so a big home's kit still grows beside water; a second on the flank,
	// as close.
	const double pondRadius = homePondRadius(radius);
	const double along = std::min(
		{0.4 * radius, radius - pondRadius - kHomePondGap - 1, pondRadius + kHomeSwarmReach});
	const double flank = std::min(0.55 * radius, pondRadius + kHomeSwarmReach + 4);
	const double offsets[2][2] = {{along, 0}, {-0.05 * radius, -flank}};
	ShapePoint first = centre;
	for (int p = 0; p < std::clamp(ponds, 0, 2) && pond; ++p)
	{
		const double px =
			centre.x + offsets[p][0] * std::cos(axis) - offsets[p][1] * std::sin(axis);
		const double py =
			centre.y + offsets[p][0] * std::sin(axis) + offsets[p][1] * std::cos(axis);
		if (p == 0)
			first = {px, py};
		fillShape(pondMask, t, px, py, *pond, axis);
	}
	return first;
}

/// Where a round home's swarm goes: in from the middle towards the door, as the top-left tile of its
/// 4x4 footprint, which is what placeSettlement measures from.
inline MapGeneratorPoint homeSwarmSite(ShapePoint centre, double axis, double radius)
{
	const ShapePoint p =
		polarPoint(centre.x, centre.y, std::min(0.3 * radius, kHomeSwarmReach), axis + kPi);
	return MapGeneratorPoint(int(std::lround(p.x)) - 2, int(std::lround(p.y)) - 2);
}

/// A round home's starter kit, round `kitCentre` (stampRoundHome's result): a wheat patch and a wood
/// patch on either side, towards the swarm, where a pond's water would regrow them. No stone: these
/// homes are walled in stone already, so a clump would only take room.
template <typename Eligible>
void plantHomeKit(Map &map, const Torus &t, GenerationContext &context, ShapePoint kitCentre,
				  double axis, double homeRadius, int wheat, int wood, Eligible eligible)
{
	const KitFrame frame{int(std::lround(kitCentre.x)), int(std::lround(kitCentre.y)), axis};
	const double reach = homePondRadius(homeRadius) * 1.2 + 3;
	const Kit kit{frame.at(-0.4 * reach, -reach, 12),
				  frame.at(-0.4 * reach, reach, 12),
				  frame.at(reach + 3, 0, 10),
				  wheat,
				  wood,
				  -1};
	plantKit(map, t, context, kit, eligible);
}

/// Round homes at given sites, all turned to `axis`: each stamped with stampRoundHome (its tiles marked
/// with the home's index in `homeOf`, its `ponds` ponds into `pondMask`), and the point its kit goes
/// returned per home. Homes on a lattice with no centre (Orbits.h's latticeSites) use axis 0, so exact
/// copies of one another stay exact.
inline std::vector<ShapePoint> stampRoundHomes(const Torus &t, const std::vector<ShapePoint> &sites,
											   double axis, const RadialShape &home, double radius,
											   int ponds, const RadialShape *pond,
											   std::vector<unsigned char> &pondMask,
											   std::vector<int> &homeOf)
{
	std::vector<ShapePoint> kits;
	for (size_t k = 0; k < sites.size(); ++k)
		kits.push_back(stampRoundHome(t, sites[k], axis, home, radius, ponds, pond, pondMask,
									  [&](int i) { homeOf[i] = int(k); }));
	return kits;
}

/// A round home's starter kit with a quarry, for a home that is not walled in stone: wheat and wood
/// either side of `kitCentre` as plantHomeKit lays them, and a stone clump of `quarry` radius beyond
/// the pond on the far side from the swarm.
template <typename Eligible>
void plantOpenHomeKit(Map &map, const Torus &t, GenerationContext &context, ShapePoint kitCentre,
					  double axis, double homeRadius, int wheat, int wood, int quarry,
					  Eligible eligible)
{
	const KitFrame frame{int(std::lround(kitCentre.x)), int(std::lround(kitCentre.y)), axis};
	const double reach = homePondRadius(homeRadius) * 1.2 + 3;
	const Kit kit{frame.at(-0.4 * reach, -reach, 12),
				  frame.at(-0.4 * reach, reach, 12),
				  frame.at(reach + 3, 0, 10),
				  wheat,
				  wood,
				  quarry};
	plantKit(map, t, context, kit, eligible);
}

/// A home in ground of any shape: a Voronoi cell, a chamber, a town block, a territory. The swarm stands
/// the same walk from the home's way in as every other colony's (siteAtDepth over walking steps from the
/// `door` tiles through `region`), on the roomiest such tile; `axis` points from the door towards the
/// swarm, so plantHomeKit and homeSwarmSite read it as they read a round home's. -1 site when no tile is
/// `depth` (give or take `spread`) steps in with at least `minimumRoom` steps to the region's edge or to
/// `keepClear`.
struct RegionHome
{
	int site = -1;                 // the swarm's centre tile
	MapGeneratorPoint swarm{0, 0}; // its footprint's top-left tile, as placeSettlement measures
	double axis = 0;               // from the door towards the swarm
	ShapePoint kitCentre{0, 0};    // kHomeSwarmReach further along the axis: where the kit goes
};
inline RegionHome regionHome(const Torus &t, const std::vector<unsigned char> &region,
							 const std::vector<int> &door, int depth, int spread, int minimumRoom,
							 const std::vector<unsigned char> *keepClear = nullptr)
{
	RegionHome home;
	if (door.empty())
		return home;
	const std::vector<int> steps = stepsFrom(t, tileMask(t, door), region);
	std::vector<unsigned char> blocked(region.size(), 0);
	for (size_t i = 0; i < region.size(); ++i)
		blocked[i] = !region[i] || (keepClear && (*keepClear)[i]);
	std::vector<int> room = stepsFrom(t, blocked);
	std::vector<int> depthIn(steps);
	for (size_t i = 0; i < region.size(); ++i)
		if (!region[i])
			depthIn[i] = -1;
	home.site = siteAtDepth(depthIn, room, depth, minimumRoom, spread);
	if (home.site < 0)
		return home;
	const int sx = home.site % t.w, sy = home.site / t.w;
	// The door's middle, the short way round from the site.
	double ox = 0, oy = 0;
	for (int d : door)
	{
		ox += t.offsetX(sx, d % t.w);
		oy += t.offsetY(sy, d / t.w);
	}
	ox /= double(door.size());
	oy /= double(door.size());
	home.axis = (ox == 0 && oy == 0) ? 0 : std::atan2(-oy, -ox);
	home.swarm = MapGeneratorPoint(sx - 2, sy - 2);
	home.kitCentre = polarPoint(sx, sy, kHomeSwarmReach, home.axis);
	return home;
}

/// What a stretch of ground carries (a home beyond its starter kit, a commons, a plaza), given how many
/// tiles it has.
struct GroundAmounts
{
	int wheat, wood, outcrops, groves;
};

/// A ground's ambient layer (a home's after its kit, a plaza's), planted on its `eligible` tiles:
/// farmland on the fertile ground in patches, then stone outcrops and fruit groves dropped at random.
///
/// Farmland takes the fertile tiles whose `patchAt(tile)` is in the top 55% of the fertile ground's
/// (so it lies in patches with gaps to walk and build in), most fertile first, and deals them into
/// wheat and wood by `splitAt(tile)` (plantFields). `amountsFor(area)` sizes all four layers from the
/// ground's tile count. Outcrops are one-tile stone clumps from `stoneStream`; groves are one-tile
/// clumps of a fruit drawn from `fruitStream`. A generator that stamps one design into every wedge
/// samples `patchAt` and `splitAt` in the wedge frame, so every home gets the same patches.
template <typename Eligible, typename PatchAt, typename SplitAt, typename AmountsFor>
void furnishGround(Map &map, const Torus &t, GenerationContext &context,
				   const Fertility::Field &fertility, Eligible eligible, PatchAt patchAt,
				   SplitAt splitAt, AmountsFor amountsFor, const char *stoneStream,
				   const char *fruitStream)
{
	const int n = t.w * t.h;
	std::vector<int> ground;
	std::vector<std::pair<double, int>> farm;
	std::vector<float> levels;
	for (int i = 0; i < n; ++i)
		if (eligible(i))
		{
			ground.push_back(i);
			if (fertility.at(i % t.w, i / t.w) > 0)
				levels.push_back(patchAt(i));
		}
	float cut = 0;
	if (!levels.empty())
	{
		std::nth_element(levels.begin(), levels.begin() + levels.size() * 45 / 100, levels.end());
		cut = levels[levels.size() * 45 / 100];
	}
	for (int i : ground)
	{
		const std::uint32_t f = fertility.at(i % t.w, i / t.w);
		if (f > 0 && patchAt(i) >= cut)
			farm.push_back({-double(f), i});
	}
	std::stable_sort(farm.begin(), farm.end());
	std::vector<int> chosen;
	for (const auto &entry : farm)
		chosen.push_back(entry.second);
	const GroundAmounts amounts = amountsFor(int(ground.size()));
	plantFields(map, t, chosen, amounts.wheat, amounts.wood, splitAt);
	scatterClumps(context, t, ground, amounts.outcrops, stoneStream, eligible,
				  [&](MapGeneratorPoint p) { placeResourceClump(map, context, p, STONE, 1); });
	scatterClumps(
		context, t, ground, amounts.groves, fruitStream, eligible, [&](MapGeneratorPoint p)
		{ placeResourceClump(map, context, p, CHERRY + int(context.bounded(fruitStream, 3)), 1); });
}
} // namespace MapGeneration
