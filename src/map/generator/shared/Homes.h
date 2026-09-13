// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Drawing.h"
#include "FertilityField.h"
#include "GenerationContext.h"
#include "Geometry.h"
#include "Grid.h"
#include "Planting.h"
#include "Resources.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>
namespace MapGeneration
{
// Homes that stand alone in the sea, as a round island turned to face out from the map's middle
// with its door on the inner side: the swarm stands in from the middle towards the door, one pond
// lies out behind it and a second, if any, on one flank. Every pond keeps a gap of land to the home's
// coast, so its beach never meets the sea's. Carousel and Switchbacks both build their homes this way.

/// The farthest a pond home's first pond lies from its middle beyond its own radius, and the swarm from
/// its middle: a short walk apart however big the home, so the kit round the pond is near the swarm.
constexpr double kPondHomeReach = 8;

/// How a pond home is proportioned: its radius, its ponds' radius, the land kept between a pond and
/// the coast, and how many ponds (0 to 2).
struct PondHomeStyle
{
	double radius, pondRadius, lakeSeaGap;
	int ponds;
};

/// Stamps a pond home: `visit(tile)` for every tile of the `home` outline centred at `centre` and
/// turned to `axis` (the heading out from the map's middle), and its ponds' outlines into `pondMask`,
/// turned the same way. Returns the first pond's centre, where the kit is planted.
template <typename Visit>
ShapePoint stampPondHome(const Torus &t, ShapePoint centre, double axis, const RadialShape &home,
						 const RadialShape &pond, const PondHomeStyle &style,
						 std::vector<unsigned char> &pondMask, Visit visit)
{
	forEachTileInShape(t, centre.x, centre.y, home, axis, [&](int i, double, double) { visit(i); });
	// The first pond lies out from the middle, as far out as the coast gap allows but never more than
	// a short walk from the swarm, so a big home's kit still grows beside water; a second on the flank,
	// as close.
	const double along = std::min({0.4 * style.radius, style.radius - style.pondRadius - style.lakeSeaGap - 1,
								   style.pondRadius + kPondHomeReach});
	const double flank = std::min(0.55 * style.radius, style.pondRadius + kPondHomeReach + 4);
	const double offsets[2][2] = {{along, 0}, {-0.05 * style.radius, -flank}};
	ShapePoint first = centre;
	for (int p = 0; p < std::clamp(style.ponds, 0, 2); ++p)
	{
		const double px = centre.x + offsets[p][0] * std::cos(axis) - offsets[p][1] * std::sin(axis);
		const double py = centre.y + offsets[p][0] * std::sin(axis) + offsets[p][1] * std::cos(axis);
		if (p == 0)
			first = {px, py};
		fillShape(pondMask, t, px, py, pond, axis);
	}
	return first;
}

/// Where a pond home's swarm goes: in from the middle towards the door, as the top-left tile of its
/// 4x4 footprint, which is what placeSettlement measures from.
inline MapGeneratorPoint pondHomeAnchor(ShapePoint centre, double axis, double radius)
{
	const ShapePoint p = polarPoint(centre.x, centre.y, std::min(0.3 * radius, kPondHomeReach), axis + kPi);
	return MapGeneratorPoint(int(std::lround(p.x)) - 2, int(std::lround(p.y)) - 2);
}

/// A pond home's starter kit: wheat and wood either side of its first pond on the swarm's side, where
/// they regrow, and a stone clump out behind the pond.
template <typename Eligible>
void plantPondHomeKit(Map &map, const Torus &t, GenerationContext &context, ShapePoint firstPond,
					  double axis, double pondRadius, int wheat, int wood, Eligible eligible)
{
	const KitFrame frame{int(std::lround(firstPond.x)), int(std::lround(firstPond.y)), axis};
	const double reach = pondRadius * 1.2 + 3;
	const Kit kit{frame.at(-0.4 * reach, -reach, 12), frame.at(-0.4 * reach, reach, 12),
				  frame.at(reach + 3, 0, 10), wheat, wood, 2};
	plantKit(map, t, context, kit, eligible);
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
	scatterClumps(context, t, ground, amounts.groves, fruitStream, eligible,
				  [&](MapGeneratorPoint p)
				  {
					  placeResourceClump(map, context, p,
										 CHERRY + int(context.bounded(fruitStream, 3)), 1);
				  });
}
} // namespace MapGeneration
