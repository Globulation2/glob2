// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "Terrain.h"
#include "BalancedStarts.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GeneratorDefinition.h"
#include "HeightMap.h"
#include "Map.h"
#include "Pipeline.h"
#include "Resources.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
// The height-field pipeline behind Islands, Swamp, River and Crater lakes.
//
// HISTORY. Leo Wandersleb wrote these generators in January 2006 (merged by nct), replacing the
// original random generators (Old random, Old islands), which giszmo brought back as "old" a week
// later. donkyhotay added automatic fruit groves in March 2006 and giszmo cut that code down and
// commented it; giszmo also added "repeative landscapes" (the repeat control) in 2006-07. On this
// branch the pipeline was split into the stages below, colony sites came to be chosen after the
// resources exist and for equality (BalancedStarts), and resource amounts and a hilltop stone
// switch were added; the default maps still come out of Leo's thresholds unchanged.
//
// THE IDEA. One smooth height field decides everything, by rank rather than by value. The lobby's
// terrain weights are turned into tile counts, and a histogram of the field finds the heights below
// which exactly that many tiles lie: the lowest tiles are water, the next beach, the next grass,
// and anything higher is desert. Resources are bands of the same ranking: algae in the deepest
// water, stone just above the beach, wheat and wood on the low grass above the stone. So a
// generator only has to shape the field (islands, craters, a river), and the map always comes out
// in the shares the player asked for, whatever the shape.
//
// GAME RULES IT LEANS ON (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md):
// - Wheat and wood regrow only near water, so farmland is the band of grass just above the
//   waterline, and its thickness is capped by whichever of water and grass is scarcer.
// - Algae needs water with sand in reach, which the shallow rim of the deepest water gives; stone
//   right above the beach is the first land a colony walks onto.
// - Grass may not touch water: controlSand after painting rings every shore.
namespace MapGeneration
{
std::vector<GeneratorControl> heightFieldResourceControls()
{
	return {GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			GeneratorControl::percentage("wood-amount", "Wood amount"),
			GeneratorControl::percentage("stone-amount", "Stone amount"),
			GeneratorControl::percentage("algae-amount", "Algae amount"),
			GeneratorControl::toggle("hilltop-stone", "Stone on hilltops", false,
									 ControlGroup::Resources)};
}

HeightFieldOptions HeightFieldOptions::fromRequest(const GenerationRequest &r, bool swamp)
{
	const auto weight = [&](const char *id) { return r.options.count(id) ? r.option(id) : 0; };
	HeightFieldOptions options{r.option("water"),     weight("sand"),    r.option("grass"),
							   weight("desert"),      r.option("smoothing"), r.option("fruit"),
							   r.option("repeat"),    swamp};
	options.wheat = r.option("wheat-amount");
	options.wood = r.option("wood-amount");
	options.stone = r.option("stone-amount");
	options.algae = r.option("algae-amount");
	options.hilltopStone = r.option("hilltop-stone") != 0;
	return options;
}

void openStartsBuriedByAmounts(Game &game, GenerationContext &context,
							   const HeightFieldOptions &options)
{
	// The resource bands are painted from map-wide noise levels with no awareness of where any team
	// starts, so an amount well above the default widens them until they can wall a colony in.
	// placeStarts has already carved out the swarm's own rectangle by now, so nothing needs to be
	// kept clear for it.
	reopenCrampedStarts(game, context,
						{options.wheat, options.wood, options.stone, options.algae});
}

// Repeat landscape: the field is built at a fraction of the map's size and stamped several times,
// so every colony can get the same patch of terrain. Each of `repeat` halvings goes to the longer
// remaining side, so 1 halves the long side, 2 makes a 2x2 tiling on a square map, and so on up to
// 32 copies. With a patch per colony this is giszmo's fair-by-repetition answer from 2006, long
// before the designed generators' rotation trick.
HeightFieldTiling heightFieldTiling(int w, int h, int repeat)
{
	/// respect symmetry-requirements
	unsigned int wPower2Divider = 0, hPower2Divider = 0;
	for (int i = 0; i < repeat; i++)
		if ((w >> wPower2Divider) > (h >> hPower2Divider))
			wPower2Divider++;
		else
			hPower2Divider++;
	HeightFieldTiling tiling;
	tiling.wRepeat = 1 << wPower2Divider;
	tiling.hRepeat = 1 << hPower2Divider;
	tiling.w = (unsigned int)(w / tiling.wRepeat);
	tiling.h = (unsigned int)(h / tiling.hRepeat);
	return tiling;
}

HeightFieldLevels classifyHeightField(HeightMap &hm, const HeightFieldTiling &tiling,
									  const HeightFieldOptions &options)
{
	const unsigned int wHeightMap = tiling.w, hHeightMap = tiling.h;
	/// the proportions requested through the gui can directly be translated into tile counts of the
	/// undermap.
	unsigned int waterTiles, sandTiles, grassTiles, wheatWoodTiles, algaeTiles;
	/// grass + sand + water + desert as from the gui
	// The fruit control (a grove count, 4 by default) is added to the total too, as if it were a
	// terrain weight: a slip in the original that shrinks every terrain's share a little and hands
	// that share to desert (the part of the field above the grass). At the defaults it is a few
	// percent of the map; it is kept because removing it changes every height-field map.
	unsigned int totalGSWFromUI =
		options.water + options.sand + options.grass + options.desert + options.fruit;
	if (options.swamp)
	{
		waterTiles = options.water * wHeightMap * hHeightMap / (1u + options.water + options.grass);
		sandTiles = 0;
		grassTiles = wHeightMap * hHeightMap - waterTiles;
	}
	else
	{
		waterTiles = static_cast<float>(options.water) / totalGSWFromUI * wHeightMap * hHeightMap;
		sandTiles = static_cast<float>(options.sand) / totalGSWFromUI * wHeightMap * hHeightMap;
		grassTiles = static_cast<float>(options.grass) / totalGSWFromUI * wHeightMap * hHeightMap;
	}
	/// wheat/wood needs ground to stand on and water. So:
	// half the scarcer of water and grass: a watery map has little land to farm, a dry map little
	// shore for fields to regrow against. Algae takes the deepest sixth of the water.
	wheatWoodTiles = waterTiles < grassTiles ? waterTiles / 2 : grassTiles / 2;
	algaeTiles = scaledCount(waterTiles / 6, options.algae);
	// A third of that share is stone, right above the beach, and the rest farmland above the stone;
	// each band is scaled on its own and none reaches past the top of the grass. Hilltop stone
	// takes the highest grass instead, and farmland then starts at the beach.
	const unsigned int landTop = waterTiles + sandTiles + grassTiles;
	const unsigned int stoneTiles = unsigned(
		std::min<std::int64_t>(grassTiles, scaledCount(wheatWoodTiles / 3, options.stone)));
	const unsigned int farmTiles = wheatWoodTiles - wheatWoodTiles / 3;
	const unsigned int stoneTop =
		options.hilltopStone ? landTop : std::min(landTop, waterTiles + sandTiles + stoneTiles);
	const unsigned int stoneFloor = landTop - stoneTiles;
	const unsigned int farmStart = options.hilltopStone ? waterTiles + sandTiles : stoneTop;
	const unsigned int wheatTop = unsigned(
		std::min<std::int64_t>(landTop, farmStart + scaledCount(farmTiles, options.wheat)));
	const unsigned int woodTop =
		unsigned(std::min<std::int64_t>(landTop, farmStart + scaledCount(farmTiles, options.wood)));

	/// histogram[i] collects the count of all terrain levels == i
	// 2048 bins over the normalised 0..1 field; each threshold snaps to a bin edge, so the shares
	// come out close to, not exactly, what was asked.
	int histogram[2048];
	memset(histogram, 0, 2048 * sizeof(int));

	for (unsigned i = 0; i < wHeightMap * hHeightMap; i++)
	{
		histogram[hm.uiLevel(i, 2048)]++;
	}
	unsigned int accumulatedHistogram = 0;
	int i = 0;
	HeightFieldLevels levels;
	while ((levels.water == 0) && (i < 2048))
	{
		accumulatedHistogram += histogram[i++];
		if (levels.algae == 0 && accumulatedHistogram >= algaeTiles)
			levels.algae = (float)(i - 1) / 2048.0;
		if (accumulatedHistogram >= waterTiles)
			levels.water = (float)(i - 1) / 2048.0;
	}
	while ((levels.sand == 0) && (i < 2048))
	{
		accumulatedHistogram += histogram[i++];
		if (accumulatedHistogram >= waterTiles + sandTiles)
			levels.sand = (float)(i - 1) / 2048.0;
	}
	while ((levels.grass == 0) && (i < 2048))
	{
		accumulatedHistogram += histogram[i++];
		if (levels.wheat == 0 && accumulatedHistogram >= wheatTop)
			levels.wheat = (float)(i - 1) / 2048.0;
		if (levels.wood == 0 && accumulatedHistogram >= woodTop)
			levels.wood = (float)(i - 1) / 2048.0;
		if (levels.stoneFloor == 0 && accumulatedHistogram >= stoneFloor)
			levels.stoneFloor = (float)(i - 1) / 2048.0;
		if (levels.stone == 0 && accumulatedHistogram >= stoneTop)
			levels.stone = (float)(i - 1) / 2048.0;
		if (accumulatedHistogram >= landTop)
			levels.grass = (float)(i - 1) / 2048.0;
	}
	return levels;
}

void paintHeightFieldTerrain(Map &map, HeightMap &hm, const HeightFieldTiling &tiling,
							 const HeightFieldLevels &levels)
{
	const int w = map.getW();
	const unsigned int wHeightMap = tiling.w, hHeightMap = tiling.h;
	for (unsigned y = 0; y < hHeightMap; y++)
		for (unsigned x = 0; x < wHeightMap; x++)
		{
			int tmpUndermap;
			if (hm(y * wHeightMap + x) < levels.water)
				tmpUndermap = WATER;
			else if (hm(y * wHeightMap + x) < levels.sand)
				tmpUndermap = SAND;
			else if (hm(y * wHeightMap + x) < levels.grass)
				tmpUndermap = GRASS;
			else
				tmpUndermap = SAND; // desert: the highest ground dries out
			for (int yRepeat = 0; yRepeat < tiling.hRepeat; yRepeat++)
				for (int xRepeat = 0; xRepeat < tiling.wRepeat; xRepeat++)
					map.setUMTerrain(
						(xRepeat * wHeightMap + x + (yRepeat * hHeightMap + y) * w) % w,
						(xRepeat * wHeightMap + x + (yRepeat * hHeightMap + y) * w) / w,
						static_cast<TerrainType>(tmpUndermap));
		}
	map.controlSand();
	map.rebuildTerrain();
}

void paintHeightFieldResources(Map &map, HeightMap &hm, const HeightFieldTiling &tiling,
							   const HeightFieldLevels &levels, const HeightFieldOptions &options)
{
	const unsigned int wHeightMap = tiling.w, hHeightMap = tiling.h;
	for (unsigned y = 0; y < hHeightMap; y++)
	{
		for (unsigned x = 0; x < wHeightMap; x++)
		{
			int tmpResource = NO_RES;
			const float level = hm(x + wHeightMap * y);
			const bool stoneBand = options.hilltopStone
									   ? level >= levels.stoneFloor && level < levels.stone
									   : level < levels.stone;
			if (level < levels.algae)
			{
				if (options.algae > 0)
					tmpResource = ALGA;
				// following places stone next to sand & water and keeps wheat & wood more inland
				// without clogging up the interior too badly
			}
			else if (stoneBand && (options.stone > 0 || !options.hilltopStone))
			{
				if (options.stone > 0)
					tmpResource = STONE;
			}
			// patch to get smooth areas of wheat and wood:
			// if the map is ascending at x+w/2,y set wheat. else set wood
			//
			// The slope is read half the field away, not at the tile itself: on the tile's own
			// slope, every hill would come out wheat on one side and wood on the other, in rings
			// round it. Read far away it is an unrelated smooth pattern, so the two crops form
			// blobs across the farmland band instead.
			else if (hm((x + wHeightMap / 2) % wHeightMap + wHeightMap * y) <
					 hm((x + wHeightMap / 2 + 1) % wHeightMap + wHeightMap * y))
			{
				if (level < levels.wheat)
					tmpResource = CORN;
			}
			else if (level < levels.wood)
			{
				tmpResource = WOOD;
			}
			if (tmpResource != NO_RES)
			{
				for (int yRepeat = 0; yRepeat < tiling.hRepeat; yRepeat++)
				{
					for (int xRepeat = 0; xRepeat < tiling.wRepeat; xRepeat++)
					{
						map.setResource(xRepeat * wHeightMap + x, yRepeat * hHeightMap + y,
										tmpResource, 1);
					}
				}
			}
		}
	}
}

bool chooseHeightFieldStarts(Game &game, GenerationContext &context)
{
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	// Choosing where the colonies go *after* the resources exist is what makes a fair choice
	// possible at all: the search scores a site by how far its workers must actually walk to wood
	// and wheat, which is unknowable while the map is still bare. The legacy search — largest grass
	// rectangle first, and everyone after it takes what is left — stays as a fallback for maps
	// where no set of sites can reach both resources at all.
	int nbTeams = context.request.nbTeams;
	// A fifth of each colony's share of the map, as a squared distance: colonies at least the side
	// of a square of that area apart (about 57 tiles on a 256x256 map with 4 colonies). Looser than
	// Old random's full share, because the balanced search below then prefers equal sites over
	// distant ones; the fifth leaves it enough candidate sets to choose from.
	int minDistSquare = (int)((double)w * h / (double)nbTeams / 5);
	if (minDistSquare <= 0)
	{
		return false;
	}
	int *bootX = context.bootX.data();
	int *bootY = context.bootY.data();
	if (chooseBalancedStarts(game, context, minDistSquare))
		return true;
	// TODO: First pass to find the number of available places.
	for (int team = 0; team < nbTeams; team++)
	{
		int maxSurface = 0;
		int maxX = 0;
		int maxY = 0;
		for (int y = 0; y < h; y++)
		{
			int width = 0;
			int startX = 0;
			for (int x = 0; x < w; x++)
			{
				int a = map.getUMTerrain(x, y);
				if (a == GRASS)
					width++;
				else
				{
					if (width > 7)
					{
						int centerX = ((x + startX) >> 1);
						int top, bot;
						for (top = 0; top < h; top++)
							if (map.getUMTerrain(centerX, y - top) != GRASS)
								break;
						for (bot = 0; bot < h; bot++)
							if (map.getUMTerrain(centerX, y + bot) != GRASS)
								break;
						int height = top + bot - 1;
						int surface = height * width;
						assert(surface > 0);

						int centerY = y + ((bot - top) >> 1);
						bool farEnough = true;
						for (int ti = 0; ti < team; ti++)
							if (map.warpDistSquare(centerX, centerY, bootX[ti], bootY[ti]) <
								minDistSquare)
							{
								farEnough = false;
								break;
							}

						if (surface > maxSurface && farEnough)
						{
							maxSurface = surface;
							maxX = centerX;
							maxY = centerY;
						}
					}
					width = 0;
					startX = x;
				}
			}
		}

		if (maxSurface <= 0)
		{
			return false;
		}
		assert(maxSurface);
		bootX[team] = maxX;
		bootY[team] = maxY;
	}
	return true;
}

// donkyhotay's fruit groves (2006): each grove picks a kind of fruit and a random free grass tile,
// then walks to free grass neighbours planting 1 to 10 trees, so a grove is a small winding cluster
// of one kind, copied into every repeat patch. The count is fixed, not scaled by map size (see the
// TODO below). Groves land anywhere, near a colony or not, so which colony can stock all three
// fruits in its inns (and so convert hungry enemy units) is luck of the draw on these maps, unless
// repeat gives each colony its own patch.
bool plantHeightFieldGroves(Map &map, GenerationContext &context, const HeightFieldTiling &tiling,
							int count)
{
	const unsigned int wHeightMap = tiling.w, hHeightMap = tiling.h;
	// TODO: count of groves does not scale with mapsize, so it has to be adjusted higher on
	// bigger maps now.
	for (int q1 = 0; q1 < count; q1++) // counting groves
	{
		// choose fruit
		int fruit;
		switch (context.stream("resources")() % 3)
		{
		case 0:
			fruit = CHERRY;
			break;
		case 1:
			fruit = ORANGE;
			break;
		case 2:
		default:
			fruit = PRUNE;
			break;
		}
		// choose coordinate where there is grass but no resource yet
		int x, y;
		int attempts = 0;
		do
		{
			if (++attempts > int(wHeightMap * hHeightMap * 4))
			{
				context.detail = "No free grass for fruit";
				return false;
			}
			x = (context.stream("resources")() % wHeightMap);
			y = (context.stream("resources")() % hHeightMap);
		} while (map.getUMTerrain(x, y) != GRASS || map.isResource(x, y));
		// choose size of grove (tree count)
		int grovesize = (context.stream("resources")() % 10) + 1;
		for (int i = 0; i < grovesize; i++)
		{
			for (int yRepeat = 0; yRepeat < tiling.hRepeat; yRepeat++)
				for (int xRepeat = 0; xRepeat < tiling.wRepeat; xRepeat++)
					map.setResource(xRepeat * wHeightMap + x, yRepeat * hHeightMap + y, fruit, 1);
			// find a valid neighbor of actual coordinate
			for (int iTry = 0; iTry < 100; iTry++)
			{
				int xNew = x + context.stream("resources")() % 3 - 1;
				int yNew = y + context.stream("resources")() % 3 - 1;
				if (map.getUMTerrain(xNew, yNew) == GRASS && !map.isResource(xNew, yNew))
				{
					x = xNew;
					y = yNew;
					break;
				}
			}
		}
	}
	return true;
}

bool generateHeightField(Game &game, GenerationContext &context, const HeightFieldOptions &options,
						 const HeightFieldBuilder &build)
{
	Map &map = game.map;
	/// to influence the roughness
	// The noise's feature size in tiles: 15 at smoothing 1 up to 36 at smoothing 8 (the defaults, 4
	// or 6, give 24 or 30). The +4 keeps even the roughest setting's features several swarms wide,
	// so land stays in pieces big enough to build on.
	const float smoothingFactor = (float)(options.smoothing + 4) * 3;
	const HeightFieldTiling tiling = heightFieldTiling(map.getW(), map.getH(), options.repeat);
	/// lets generate a patch of perlin noise. That's a smooth mapping R^2 to ]0;1[
	HeightMap hm(tiling.w, tiling.h, context.stream("heightmap"));
	build(hm, tiling.w, tiling.h, smoothingFactor);
	const HeightFieldLevels levels = classifyHeightField(hm, tiling, options);
	paintHeightFieldTerrain(map, hm, tiling, levels);
	context.stage = "resources";
	paintHeightFieldResources(map, hm, tiling, levels, options);
	context.stage = "starting locations";
	if (!chooseHeightFieldStarts(game, context))
		return false;
	// Fairness guarantee: the bands above are painted from map-wide noise levels with no
	// awareness of where any team actually starts, so a team's assigned tile can land in a
	// stretch of grass the noise field never happens to touch — or a straight-line-nearby
	// deposit can sit across water a worker can never walk to. Top up any team without wheat or
	// wood within comfortable working range with one small guaranteed clump placed through the
	// same walkable-space flood used to judge that distance, so it's reachable by construction;
	// teams the noise pass already served are left untouched. This still has to run before
	// placeStarts() carves out a small rectangle by the boot tile for the swarm and its workers
	// (StartingPositions.cpp's setNoResource calls), so skip that band or a clump placed here
	// would just be wiped a moment later.
	guaranteeStartingResources(game, context, 24, 32, /*clearRadius=*/6);
	return options.fruit <= 0 || plantHeightFieldGroves(map, context, tiling, options.fruit);
}
} // namespace MapGeneration
