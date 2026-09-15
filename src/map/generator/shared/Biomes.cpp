// SPDX-License-Identifier: GPL-3.0-or-later
#include "Biomes.h"
#include "FertilityField.h"
#include "GenerationContext.h"
#include "Homes.h"
#include "LatticeNoise.h"
#include "Map.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Resources.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace MapGeneration
{
BiomeKit fertilePlain()
{
	BiomeKit kit;
	kit.name = "fertile plain";
	// Four ponds of ninety tiles per 10000 (six on a quarter of a 256 map): a plain with a few big
	// ponds, not a marsh; each pond waters a thirty-tile square round it.
	kit.pondsPer10000 = 4;
	kit.pondTiles = 90;
	kit.farmPerMille = 450;
	kit.outcropsPer1000 = 2;
	kit.grovesPer1000 = 1;
	return kit;
}

BiomeKit stoneFortress()
{
	BiomeKit kit;
	kit.name = "stone fortress";
	kit.pondsPer10000 = 2;
	kit.pondTiles = 40;
	kit.wallThickness = 2;
	kit.farmPerMille = 300;
	kit.outcropsPer1000 = 8;
	kit.grovesPer1000 = 1;
	return kit;
}

BiomeKit orchardIsland()
{
	BiomeKit kit;
	kit.name = "orchard island";
	kit.pondsPer10000 = 1;
	kit.pondTiles = 260;
	kit.farmPerMille = 250;
	kit.outcropsPer1000 = 2;
	kit.orchardIsland = true;
	return kit;
}

BiomeKit forest()
{
	BiomeKit kit;
	kit.name = "forest";
	kit.pondsPer10000 = 3;
	kit.pondTiles = 60;
	kit.farmPerMille = 250;
	kit.woodPercent = 60;
	kit.outcropsPer1000 = 2;
	kit.grovesPer1000 = 2;
	kit.coverPercent = 55;
	return kit;
}

BiomeKit farmland()
{
	BiomeKit kit;
	kit.name = "farmland";
	// Nearly half the watered ground under crops, a third of it wood: the plains are where colonies
	// feed. A fifth of the dry ground carries finite reserves for whoever walks out to them.
	kit.farmPerMille = 450;
	kit.woodPercent = 35;
	kit.outcropsPer1000 = 2;
	kit.grovesPer1000 = 1;
	kit.dryFarmPerMille = 200;
	return kit;
}

BiomeKit woodland()
{
	BiomeKit kit;
	kit.name = "woodland";
	// Wood over half the ground in patches (the gaps are where a colony builds), fields where the
	// water is, and the most fruit of any land: a forest is worth cutting into.
	kit.farmPerMille = 250;
	kit.woodPercent = 60;
	kit.outcropsPer1000 = 2;
	kit.grovesPer1000 = 3;
	kit.coverPercent = 55;
	kit.dryFarmPerMille = 120;
	return kit;
}

BiomeKit savanna()
{
	BiomeKit kit;
	kit.name = "savanna";
	// Dry grassland: crops only where the geography waters it and thin even there, a few finite
	// patches, and outcrops rather than groves.
	kit.farmPerMille = 180;
	kit.woodPercent = 25;
	kit.outcropsPer1000 = 3;
	kit.grovesPer1000 = 1;
	kit.dryFarmPerMille = 60;
	return kit;
}

BiomeKit barrens()
{
	BiomeKit kit;
	kit.name = "barrens";
	// Tundra: grass a colony can build on but little grows; scrub wood, outcrops, no fruit.
	kit.farmPerMille = 120;
	kit.woodPercent = 50;
	kit.outcropsPer1000 = 4;
	kit.dryFarmPerMille = 30;
	return kit;
}

BiomeKit highland()
{
	BiomeKit kit;
	kit.name = "highland";
	// A range: stone over 40% of the ground in patches. Below the eight-connected percolation
	// threshold of about 59% open (41% blocked) the gaps still thread through, so a range is slow
	// and winding to cross on foot but not a wall, and the route opener (Roads.h) cuts a pass only
	// where no gap serves. Thin fields in the valleys; every outcrop is a quarry already.
	kit.farmPerMille = 150;
	kit.woodPercent = 40;
	kit.outcropsPer1000 = 1;
	kit.grovesPer1000 = 1;
	kit.coverPercent = 40;
	kit.coverResource = STONE;
	kit.dryFarmPerMille = 50;
	return kit;
}

BiomeKit scaledBiome(const BiomeKit &kit, const ResourceAmounts &amounts)
{
	BiomeKit scaled = kit;
	// The farmland is one number split by woodPercent; scale each share by its own amount and
	// recombine, so a map with no wood at all still has its wheat.
	const auto rescale = [&](int perMille, int &outPerMille, int &outWoodPercent)
	{
		const std::int64_t wheat = scaledCount(std::int64_t(perMille) * (100 - kit.woodPercent), amounts.wheat);
		const std::int64_t wood = scaledCount(std::int64_t(perMille) * kit.woodPercent, amounts.wood);
		outPerMille = int((wheat + wood + 50) / 100);
		outWoodPercent = wheat + wood > 0 ? int(wood * 100 / (wheat + wood)) : kit.woodPercent;
	};
	int woodPercent = kit.woodPercent;
	rescale(kit.farmPerMille, scaled.farmPerMille, woodPercent);
	int dryWoodPercent = kit.woodPercent;
	rescale(kit.dryFarmPerMille, scaled.dryFarmPerMille, dryWoodPercent);
	scaled.woodPercent = woodPercent;
	scaled.outcropsPer1000 = int(scaledCount(kit.outcropsPer1000, amounts.stone));
	scaled.grovesPer1000 = int(scaledCount(kit.grovesPer1000, amounts.fruit));
	scaled.coverPercent = std::min(
		100, int(scaledCount(kit.coverPercent, kit.coverResource == STONE ? amounts.stone : amounts.wood)));
	return scaled;
}

namespace
{
// A kit's worth per tile before normalising, from the start scorer's weights: fertility (0.25) from
// how much of the ground its ponds water (a pond waters about thirty tiles square, 900 tiles), wheat, wood and
// their depth (0.56) from the farmland on that watered ground, room (0.10) from the open ground left,
// stone (0.09) for having any, and a judgement for fruit, which the scorer doesn't measure but which
// pulls enemy units across (GAME_RULES_FOR_MAP_DESIGN.md).
double rawWorth(const BiomeKit &kit)
{
	const double pondShare = kit.pondsPer10000 * kit.pondTiles / 10000.0;
	const double watered = std::min(1.0, kit.pondsPer10000 * 900 / 10000.0);
	const double farm = kit.farmPerMille / 1000.0 * watered;
	const double open = std::max(0.0, 1 - pondShare - farm - kit.coverPercent / 100.0 * (1 - farm));
	const double stone = kit.wallThickness > 0 || kit.outcropsPer1000 > 0 ? 1 : 0;
	const double fruit = std::min(1.0, kit.grovesPer1000 * 0.05 + (kit.orchardIsland ? 0.3 : 0));
	return 0.25 * watered + 0.56 * farm + 0.10 * open + 0.09 * stone + 0.2 * fruit;
}
} // namespace

double biomeWorth(const BiomeKit &kit)
{
	return rawWorth(kit) / rawWorth(fertilePlain());
}

BiomeTerrain sketchBiome(TerrainSketch &sketch, const Torus &t,
						 const std::vector<unsigned char> &region,
						 const std::vector<unsigned char> &doors, const BiomeKit &kit,
						 GenerationContext &context, const std::string &stream)
{
	const int n = t.size();
	BiomeTerrain terrain;
	terrain.water.assign(size_t(n), 0);
	terrain.wall.assign(size_t(n), 0);
	terrain.island.assign(size_t(n), 0);
	int tiles = 0;
	for (unsigned char r : region)
		tiles += r;
	// Ponds keep four tiles inside the region (past the wall), so their beaches stay inside it.
	const std::vector<unsigned char> inner = erode(t, region, 4 + kit.wallThickness);
	std::vector<int> candidates;
	for (int i = 0; i < n; ++i)
		if (inner[i])
			candidates.push_back(i);
	const int ponds = kit.pondsPer10000 > 0 ? std::max(1, tiles * kit.pondsPer10000 / 10000) : 0;
	const double pondRadius = std::sqrt(std::max(1, kit.pondTiles) / kPi);
	const int spacing2 = int(std::lround(9 * pondRadius * pondRadius));
	std::vector<int> seeds;
	for (int p = 0; p < ponds && !candidates.empty(); ++p)
		for (int attempt = 0; attempt < 50; ++attempt)
		{
			const int at = candidates[context.bounded(stream, std::uint32_t(candidates.size()))];
			bool apart = true;
			for (int s : seeds)
				apart = apart && t.dist2(at % t.w, at / t.w, s % t.w, s / t.w) >= spacing2;
			if (apart)
			{
				seeds.push_back(at);
				break;
			}
		}
	std::vector<int> queued(size_t(n), 0);
	constexpr double kIslandRadius = 4.5;
	for (size_t p = 0; p < seeds.size(); ++p)
	{
		const int seed = seeds[p];
		const bool island = kit.orchardIsland && p == 0;
		const int target =
			kit.pondTiles + (island ? int(std::lround(kPi * kIslandRadius * kIslandRadius)) : 0);
		growWater(
			t, terrain.water, seed, target, [&](int i) { return inner[i] != 0; }, [&](int i)
			{ return t.dist2(seed % t.w, seed / t.w, i % t.w, i / t.w); }, queued, int(p) + 1);
		if (island)
			for (int i = 0; i < n; ++i)
				if (terrain.water[i] && t.dist2(seed % t.w, seed / t.w, i % t.w, i / t.w) <=
											kIslandRadius * kIslandRadius)
				{
					terrain.water[i] = 0;
					terrain.island[i] = 1;
				}
	}
	for (int i = 0; i < n; ++i)
		if (terrain.water[i])
			sketch[i] = WATER;
	if (kit.wallThickness > 0)
	{
		const std::vector<int> depth = clearance(t, region);
		for (int i = 0; i < n; ++i)
			terrain.wall[i] = region[i] && depth[i] <= kit.wallThickness && !doors[i];
	}
	return terrain;
}

void furnishBiome(Map &map, const Torus &t, GenerationContext &context,
				  const std::vector<unsigned char> &region, const BiomeTerrain &terrain,
				  const BiomeKit &kit, const std::vector<unsigned char> &keepClear,
				  const std::string &stream, const std::vector<unsigned char> *noCover)
{
	const int n = t.size();
	for (int i = 0; i < n; ++i)
		if (terrain.wall[i] && !keepClear[i] && map.isResourceAllowed(i % t.w, i / t.w, STONE))
			map.setResource(i % t.w, i / t.w, STONE, 1);
	// Outcrops and groves are clumps a tile across (placeResourceClump), so everything keeps a tile in
	// from the ground's edge, the wall, the island and the clear ground.
	std::vector<unsigned char> ground(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		ground[i] = region[i] && !terrain.wall[i] && !terrain.island[i] && !keepClear[i];
	const std::vector<unsigned char> inside = erode(t, ground, 1);
	const auto eligible = [&](int i) { return inside[i] && clearGround(map, i % t.w, i / t.w); };
	const Fertility::Field fertility = Fertility::forMap(map, false);
	const std::vector<int> patch = periodicNoise(t.w, t.h, 12, context.stream(stream + "-patch"));
	const std::vector<int> split = periodicNoise(t.w, t.h, 6, context.stream(stream + "-split"));
	int fertile = 0;
	for (int i = 0; i < n; ++i)
		fertile += eligible(i) && fertility.at(i % t.w, i / t.w) > 0;
	const int farm = fertile * kit.farmPerMille / 1000;
	const std::string stoneStream = stream + "-stone", fruitStream = stream + "-fruit";
	furnishGround(
		map, t, context, fertility, eligible, [&](int i) { return float(patch[i]); },
		[&](int i) { return split[i]; },
		[&](int area)
		{
			return GroundAmounts{farm * (100 - kit.woodPercent) / 100, farm * kit.woodPercent / 100,
								 area * kit.outcropsPer1000 / 1000,
								 area * kit.grovesPer1000 / 1000};
		},
		stoneStream.c_str(), fruitStream.c_str());
	int fruit = 0;
	for (int i = 0; i < n; ++i)
		if (terrain.island[i] && !keepClear[i] && map.isResourceAllowed(i % t.w, i / t.w, CHERRY))
			map.setResource(i % t.w, i / t.w, CHERRY + fruit++ % 3, 1);
	// The dry reserve: finite crops on the ground where nothing regrows, in the same patches (the
	// top of the patch noise) so they read as fields and not speckle, dealt by the split noise.
	if (kit.dryFarmPerMille > 0)
	{
		std::vector<int> dry;
		std::vector<int> levels;
		for (int i = 0; i < n; ++i)
			if (eligible(i) && fertility.at(i % t.w, i / t.w) == 0)
			{
				dry.push_back(i);
				levels.push_back(patch[i]);
			}
		const int reserve = int(dry.size()) * kit.dryFarmPerMille / 1000;
		if (reserve > 0)
		{
			// Fields on the patchiest 45% of the dry ground, as furnishGround lays the wet ones.
			const int cut = percentile(levels, 55);
			std::vector<int> fields;
			for (int i : dry)
				if (patch[i] >= cut)
					fields.push_back(i);
			plantFields(map, t, fields, reserve * (100 - kit.woodPercent) / 100,
						reserve * kit.woodPercent / 100, [&](int i) { return split[i]; });
		}
	}
	if (kit.coverPercent <= 0)
		return;
	const std::vector<int> cover = periodicNoise(t.w, t.h, 10, context.stream(stream + "-cover"));
	std::vector<int> open;
	for (int i = 0; i < n; ++i)
		if (eligible(i))
			open.push_back(cover[i]);
	if (open.empty())
		return;
	const int level = percentile(open, 100 - std::clamp(kit.coverPercent, 0, 100));
	plantCover(map, t, region, kit.coverResource, [&](int i)
			   { return eligible(i) && cover[i] >= level && !(noCover && (*noCover)[i]); });
}
} // namespace MapGeneration
