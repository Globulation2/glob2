// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "Terrain.h"
#include "Distances.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GeneratorDefinition.h"
#include "GlobalContainer.h"
#include "HeightMap.h"
#include "Map.h"
#include "Regions.h"
#include "Resources.h"
#include "StartingPositions.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
using namespace MapGeneration;
std::vector<GeneratorControl> MapGeneration::heightFieldResourceControls()
{
	return {GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			GeneratorControl::percentage("wood-amount", "Wood amount"),
			GeneratorControl::percentage("stone-amount", "Stone amount"),
			GeneratorControl::percentage("algae-amount", "Algae amount"),
			GeneratorControl::toggle("hilltop-stone", "Stone on hilltops", false,
									 ControlGroup::Resources)};
}
void MapGeneration::readResourceControls(HeightFieldOptions &options,
										 const GenerationRequest &request)
{
	options.wheat = request.option("wheat-amount");
	options.wood = request.option("wood-amount");
	options.stone = request.option("stone-amount");
	options.algae = request.option("algae-amount");
	options.hilltopStone = request.option("hilltop-stone") != 0;
}
void MapGeneration::openStartsBuriedByAmounts(Game &game, GenerationContext &context,
											  const HeightFieldOptions &options)
{
	if (options.wheat == 100 && options.wood == 100 && options.stone == 100 && options.algae == 100)
		return;
	// The resource bands are painted from map-wide noise levels with no awareness of where any team
	// starts, so an amount well above the default widens them until they can wall a colony in with
	// nowhere left to build — which the wheat/wood guarantee inside generateHeightField doesn't
	// address, since a colony buried in wheat has wheat at its feet. placeStarts has already carved
	// out the swarm's own rectangle by now, so nothing needs to be kept clear for it, and the
	// guarantee runs once more in case the clearing took the colony's nearest crop with the wall.
	openCrampedStarts(game, context);
	guaranteeStartingResources(game, context, 24, 32);
}
bool MapGeneration::generateHeightField(Game &game, GenerationContext &context,
										const HeightFieldOptions &options,
										const HeightFieldBuilder &build)
{
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();

	/// all under waterLevel is water, under sandLevel is beach, under grassLevel is grass and above
	/// grasslevel is desert
	float waterLevel, sandLevel, grassLevel, wheatLevel, woodLevel, algaeLevel, stoneLevel,
		stoneFloorLevel;
	/// to influence the roughness
	float smoothingFactor = (float)(options.smoothing + 4) * 3;
	/// the proportions requested through the gui can directly be translated into tile counts of the
	/// undermap.
	unsigned int waterTiles, sandTiles, grassTiles, wheatWoodTiles, algaeTiles;
	/// grass + sand + water + desert as from the gui
	unsigned int totalGSWFromUI =
		options.water + options.sand + options.grass + options.desert + options.fruit;
	/// respect symmetry-requirements
	unsigned int wPower2Divider = 0, hPower2Divider = 0;
	int power2Divider = options.repeat;
	for (int i = 0; i < power2Divider; i++)
		if ((w >> wPower2Divider) > (h >> hPower2Divider))
			wPower2Divider++;
		else
			hPower2Divider++;
	int wRepeat = 1 << wPower2Divider;
	int hRepeat = 1 << hPower2Divider;
	unsigned int wHeightMap = (unsigned int)(w / wRepeat);
	unsigned int hHeightMap = (unsigned int)(h / hRepeat);
	/// lets generate a patch of perlin noise. That's a smooth mapping R^2 to ]0;1[
	HeightMap hm(wHeightMap, hHeightMap, context.stream("heightmap"));
	/// 1 to avoid division by zero,
	build(hm, wHeightMap, hHeightMap, smoothingFactor);
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
	int histogram[2048];
	memset(histogram, 0, 2048 * sizeof(int));

	for (unsigned i = 0; i < wHeightMap * hHeightMap; i++)
	{
		histogram[hm.uiLevel(i, 2048)]++;
	}
	unsigned int accumulatedHistogram = 0;
	int i = 0;
	waterLevel = 0;
	sandLevel = 0;
	grassLevel = 0;
	wheatLevel = 0;
	woodLevel = 0;
	stoneLevel = 0;
	stoneFloorLevel = 0;
	algaeLevel = 0;
	while ((waterLevel == 0) && (i < 2048))
	{
		accumulatedHistogram += histogram[i++];
		if (algaeLevel == 0 && accumulatedHistogram >= algaeTiles)
			algaeLevel = (float)(i - 1) / 2048.0;
		if (accumulatedHistogram >= waterTiles)
			waterLevel = (float)(i - 1) / 2048.0;
	}
	while ((sandLevel == 0) && (i < 2048))
	{
		accumulatedHistogram += histogram[i++];
		if (accumulatedHistogram >= waterTiles + sandTiles)
			sandLevel = (float)(i - 1) / 2048.0;
	}
	while ((grassLevel == 0) && (i < 2048))
	{
		accumulatedHistogram += histogram[i++];
		if (wheatLevel == 0 && accumulatedHistogram >= wheatTop)
			wheatLevel = (float)(i - 1) / 2048.0;
		if (woodLevel == 0 && accumulatedHistogram >= woodTop)
			woodLevel = (float)(i - 1) / 2048.0;
		if (stoneFloorLevel == 0 && accumulatedHistogram >= stoneFloor)
			stoneFloorLevel = (float)(i - 1) / 2048.0;
		if (stoneLevel == 0 && accumulatedHistogram >= stoneTop)
			stoneLevel = (float)(i - 1) / 2048.0;
		if (accumulatedHistogram >= landTop)
			grassLevel = (float)(i - 1) / 2048.0;
	}
	for (unsigned y = 0; y < hHeightMap; y++)
		for (unsigned x = 0; x < wHeightMap; x++)
		{
			int tmpUndermap;
			if (hm(y * wHeightMap + x) < waterLevel)
				tmpUndermap = WATER;
			else if (hm(y * wHeightMap + x) < sandLevel)
				tmpUndermap = SAND;
			else if (hm(y * wHeightMap + x) < grassLevel)
				tmpUndermap = GRASS;
			else
				tmpUndermap = SAND;
			for (int yRepeat = 0; yRepeat < hRepeat; yRepeat++)
				for (int xRepeat = 0; xRepeat < wRepeat; xRepeat++)
					map.setUMTerrain(
						(xRepeat * wHeightMap + x + (yRepeat * hHeightMap + y) * w) % w,
						(xRepeat * wHeightMap + x + (yRepeat * hHeightMap + y) * w) / w,
						static_cast<TerrainType>(tmpUndermap));
		}
	map.controlSand();
	map.rebuildTerrain();
	context.stage = "resources";
	// now to add primary resources for current map generator
	for (unsigned y = 0; y < hHeightMap; y++)
	{
		for (unsigned x = 0; x < wHeightMap; x++)
		{
			int tmpResource = NO_RES;
			const float level = hm(x + wHeightMap * y);
			const bool stoneBand = options.hilltopStone
									   ? level >= stoneFloorLevel && level < stoneLevel
									   : level < stoneLevel;
			if (level < algaeLevel)
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
			else if (hm((x + wHeightMap / 2) % wHeightMap + wHeightMap * y) <
					 hm((x + wHeightMap / 2 + 1) % wHeightMap + wHeightMap * y))
			{
				if (level < wheatLevel)
					tmpResource = CORN;
			}
			else if (level < woodLevel)
			{
				tmpResource = WOOD;
			}
			if (tmpResource != NO_RES)
			{
				for (int yRepeat = 0; yRepeat < hRepeat; yRepeat++)
				{
					for (int xRepeat = 0; xRepeat < wRepeat; xRepeat++)
					{
						map.setResource(xRepeat * wHeightMap + x, yRepeat * hHeightMap + y,
										tmpResource, 1);
					}
				}
			}
		}
	}

	// Choosing where the colonies go *after* the resources exist is what makes a fair choice
	// possible at all: the search below scores a site by how far its workers must actually walk
	// to wood and wheat, which is unknowable while the map is still bare. Nothing in between
	// reads a boot position or draws from a random stream, so the two passes simply swapped
	// order. The legacy search — largest grass rectangle first, and everyone after it takes
	// what is left — stays as a fallback for maps where no set of sites can reach both
	// resources at all.
	context.stage = "starting locations";
	int nbTeams = context.request.nbTeams;
	int minDistSquare = (int)((double)w * h / (double)nbTeams / 5);
	if (minDistSquare <= 0)
	{
		return false;
	}
	int *bootX = context.bootX.data();
	int *bootY = context.bootY.data();
	if (!chooseBalancedStarts(game, context, minDistSquare))
	{
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
	}

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

	// TODO: count of groves(=options.fruit) does not scale with mapsize.
	// so it has to be adjusted higher on bigger maps now.

	// fruit-placement:
	if (options.fruit > 0)
	{
		for (int q1 = 0; q1 < options.fruit; q1++) // counting groves
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
				for (int yRepeat = 0; yRepeat < hRepeat; yRepeat++)
					for (int xRepeat = 0; xRepeat < wRepeat; xRepeat++)
						map.setResource(xRepeat * wHeightMap + x, yRepeat * hHeightMap + y, fruit,
										1);
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
	}
	return true;
}
