// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
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
#include "Terrain.h"
#include "Unit.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
using namespace MapGeneration;
#include "ShatteredCoastGenerator.h"

static void simulateRandomMap(GenerationContext &context, int smooth, double baseWater,
							  double baseSand, double baseGrass, double *finalWater,
							  double *finalSand, double *finalGrass)
{
	int w = 32 << (smooth >> 2);
	int h = w;
	int s = w * h;
	int m = s - 1;
	std::vector<int> undermap(w * h);

	// context.stream() looks up a named std::mt19937 by string key on every call; every use
	// here names the same "simulation" stream, so looking it up once and reusing the reference
	// through both w*h-sized loops below skips a map lookup (and a temporary std::string, for
	// callers passing a literal) per tile per draw instead of changing which numbers come out.
	std::mt19937 &rng = context.stream("simulation");

	int totalRatio = 0x7FFF;
	int waterRatio = (int)(baseWater * ((double)totalRatio));
	int sandRatio = (int)(baseSand * ((double)totalRatio));
	int grassRatio = (int)(baseGrass * ((double)totalRatio));
	totalRatio = waterRatio + sandRatio + grassRatio;

	if (totalRatio == 0)
	{
		waterRatio = 1;
		sandRatio = 1;
		grassRatio = 1;
		totalRatio = 3;
	}

	/// First, we create a fully random patchwork:
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			int r = rng() % totalRatio;
			r -= waterRatio;
			if (r < 0)
			{
				undermap[y * w + x] = 0;
				continue;
			}
			r -= sandRatio;
			if (r < 0)
			{
				undermap[y * w + x] = 1;
				continue;
			}
			r -= grassRatio;
			if (r < 0)
			{
				undermap[y * w + x] = 2;
				continue;
			}
			assert(false); // Want's to sing ?
		}

	for (int i = 0; i < smooth; i++)
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				if (rng() & 4)
				{
					int a = undermap[(y * w + x + 1 + s) & m];
					int b = undermap[(y * w + x - 1 + s) & m];
					if (a == b)
					{
						undermap[y * w + x] = a;
						continue;
					}
				}
				else
				{
					int a = undermap[(y * w + x + w + s) & m];
					int b = undermap[(y * w + x - w + s) & m];
					if (a == b)
					{
						undermap[y * w + x] = a;
						continue;
					}
				}
				if (rng() & 4)
				{
					int a = undermap[(y * w + x + w + 1 + s) & m];
					int b = undermap[(y * w + x - w - 1 + s) & m];
					if (a == b)
					{
						undermap[y * w + x] = a;
						continue;
					}
				}
				else
				{
					int a = undermap[(y * w + x + w - 1 + s) & m];
					int b = undermap[(y * w + x - w + 1 + s) & m];
					if (a == b)
					{
						undermap[y * w + x] = a;
						continue;
					}
				}
				if (rng() & 4)
				{
					int a = undermap[(y * w + x + w - 2 + s) & m];
					int b = undermap[(y * w + x - w + 2 + s) & m];
					if (a == b)
					{
						undermap[y * w + x] = a;
						continue;
					}
				}
				else
				{
					int a = undermap[(y * w + x + w - (h << 1) + s) & m];
					int b = undermap[(y * w + x - w + (h << 1) + s) & m];
					if (a == b)
					{
						undermap[y * w + x] = a;
						continue;
					}
				}
				if (rng() & 4)
				{
					int a = undermap[(y * w + x + w + 2 + (h << 1) + s) & m];
					int b = undermap[(y * w + x - w - 2 - (h << 1) + s) & m];
					if (a == b)
					{
						undermap[y * w + x] = a;
						continue;
					}
				}
				else
				{
					int a = undermap[(y * w + x + w + 2 - (h << 1) + s) & m];
					int b = undermap[(y * w + x - w - 2 + (h << 1) + s) & m];
					if (a == b)
					{
						undermap[y * w + x] = a;
						continue;
					}
				}
			}
	// What's finally in ?
	int waterCount = 0;
	int sandCount = 0;
	int grassCount = 0;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			switch (undermap[y * w + x])
			{
			case 0:
				waterCount++;
				continue;
			case 1:
				sandCount++;
				continue;
			case 2:
				grassCount++;
				continue;
			}
	int totalCount = waterCount + sandCount + grassCount;

	*finalWater = ((double)waterCount) / ((double)totalCount);
	*finalSand = ((double)sandCount) / ((double)totalCount);
	*finalGrass = ((double)grassCount) / ((double)totalCount);
}

static bool terrain(Game &game, GenerationContext &context, const ShatteredCoastOptions &options)
{
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	// Same reasoning as simulateRandomMap's rng: every draw below names the "terrain" stream,
	// so look it up once rather than on every one of the many draws per tile in the loops below.
	std::mt19937 &rng = context.stream("terrain");

	int waterRatio = options.water;
	int sandRatio = options.sand;
	int grassRatio = options.grass;
	int totalRatio = waterRatio + sandRatio + grassRatio;
	int smooth = options.smoothing;
	double baseWater, baseSand, baseGrass;

	if (totalRatio == 0)
	{
		baseWater = baseSand = baseGrass = 1.0 / 3.0;
	}
	else
	{
		baseWater = (float)waterRatio / (float)totalRatio;
		baseSand = (float)sandRatio / (float)totalRatio;
		baseGrass = (float)grassRatio / (float)totalRatio;
	}
	// Sorry, the equation is too complex for me. We use a numeric approach:
	double alphaWater = baseWater;
	double alphaSand = baseSand;
	double alphaGrass = baseGrass;
	double alphaSum = alphaWater + alphaSand + alphaGrass;
	alphaWater /= alphaSum;
	alphaSand /= alphaSum;
	alphaGrass /= alphaSum;

	for (int r = 1; r <= smooth; r++)
	{
		for (int prec = 0; prec < 3; prec++)
		{
			double finalAlphaWater, finalAlphaSand, finalAlphaGrass;
			simulateRandomMap(context, r, alphaWater, alphaSand, alphaGrass, &finalAlphaWater,
							  &finalAlphaSand, &finalAlphaGrass);

			double errAlphaWater = finalAlphaWater - baseWater;
			double errAlphaSand = finalAlphaSand - baseSand;
			double errAlphaGrass = finalAlphaGrass - baseGrass;

			double betaWater;
			double betaSand;
			double betaGrass;

			if (finalAlphaWater)
				betaWater = (alphaWater * baseWater) / finalAlphaWater;
			else
				betaWater = 0;
			if (finalAlphaSand)
				betaSand = (alphaSand * baseSand) / finalAlphaSand;
			else
				betaSand = 0;
			if (finalAlphaGrass)
				betaGrass = (alphaGrass * baseGrass) / finalAlphaGrass;
			else
				betaGrass = 0;
			double betaSum = betaWater + betaSand + betaGrass;
			betaWater /= betaSum;
			betaSand /= betaSum;
			betaGrass /= betaSum;

			double finalBetaWater, finalBetaSand, finalBetaGrass;
			simulateRandomMap(context, r, betaWater, betaSand, betaGrass, &finalBetaWater,
							  &finalBetaSand, &finalBetaGrass);

			double errBetaWater = finalBetaWater - baseWater;
			double errBetaSand = finalBetaSand - baseSand;
			double errBetaGrass = finalBetaGrass - baseGrass;

			double projNom = (errBetaWater * errAlphaWater + errBetaSand * errAlphaSand +
							  errBetaGrass * errAlphaGrass);
			double projDen = (errAlphaWater * errAlphaWater + errAlphaSand * errAlphaSand +
							  errAlphaGrass * errAlphaGrass);
			if (projDen <= 0)
				continue;
			double proj = projNom / projDen;

			double minErr = DBL_MAX;
			for (double cfi = 0.0; cfi <= 1.0; cfi += 0.1)
			{
				double cf = cfi * proj;

				double sumCenter = 1.0 - cf;
				double gammaWater = (-cf * betaWater + 1.0 * alphaWater) / sumCenter;
				double gammaSand = (-cf * betaSand + 1.0 * alphaSand) / sumCenter;
				double gammaGrass = (-cf * betaGrass + 1.0 * alphaGrass) / sumCenter;
				if (gammaWater < 0.0)
					gammaWater = 0.0;
				if (gammaSand < 0.0)
					gammaSand = 0.0;
				if (gammaGrass < 0.0)
					gammaGrass = 0.0;
				double gammaSum = gammaWater + gammaSand + gammaGrass;
				if (gammaSum <= 0)
					continue;
				gammaWater /= gammaSum;
				gammaSand /= gammaSum;
				gammaGrass /= gammaSum;

				double finalGammaWater, finalGammaSand, finalGammaGrass;
				simulateRandomMap(context, r, gammaWater, gammaSand, gammaGrass, &finalGammaWater,
								  &finalGammaSand, &finalGammaGrass);

				double errGammaWater = finalGammaWater - baseWater;
				double errGammaSand = finalGammaSand - baseSand;
				double errGammaGrass = finalGammaGrass - baseGrass;
				double errGamma = (errGammaWater * errGammaWater + errGammaSand * errGammaSand +
								   errGammaGrass * errGammaGrass);

				if (errGamma < minErr)
				{
					minErr = errGamma;
					alphaWater = gammaWater;
					alphaSand = gammaSand;
					alphaGrass = gammaGrass;
				}
			}
		}
	}

	double simWater, simSand, simGrass;
	simulateRandomMap(context, smooth, alphaWater, alphaSand, alphaGrass, &simWater, &simSand,
					  &simGrass);

	totalRatio = 0x7FFF;
	waterRatio = (int)(((double)alphaWater) * ((double)totalRatio));
	sandRatio = (int)(((double)alphaSand) * ((double)totalRatio));
	grassRatio = (int)(((double)alphaGrass) * ((double)totalRatio));
	if (waterRatio < 0)
		waterRatio = 0;
	if (sandRatio < 0)
		sandRatio = 0;
	if (grassRatio < 0)
		grassRatio = 0;
	totalRatio = waterRatio + sandRatio + grassRatio;

	// First, we create a fully random patchwork:
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			int r = rng() % totalRatio;
			r -= waterRatio;
			if (r < 0)
			{
				map.setUMTerrain(x, y, WATER);
				continue;
			}
			r -= sandRatio;
			if (r < 0)
			{
				map.setUMTerrain(x, y, SAND);
				continue;
			}
			r -= grassRatio;
			if (r < 0)
			{
				map.setUMTerrain(x, y, GRASS);
				continue;
			}
			assert(false); // Want's to sing ?
		}

	// What's finally in ?
	int waterCount = 0;
	int sandCount = 0;
	int grassCount = 0;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			switch (map.getUMTerrain(x, y))
			{
			case WATER:
				waterCount++;
				continue;
			case SAND:
				sandCount++;
				continue;
			case GRASS:
				grassCount++;
				continue;
			}
		}
	double totalCount = (double)(waterCount + sandCount + grassCount);

	for (int i = 0; i < smooth; i++)
	{
		// What's in now?
		waterCount = 0;
		sandCount = 0;
		grassCount = 0;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				switch (map.getUMTerrain(x, y))
				{
				case WATER:
					waterCount++;
					continue;
				case SAND:
					sandCount++;
					continue;
				case GRASS:
					grassCount++;
					continue;
				}
			}
		double totalRatioCount = (double)(waterCount + sandCount + grassCount);
		double waterRatioCount = waterCount / totalRatioCount;
		double sandRatioCount = sandCount / totalRatioCount;
		double grassRatioCount = grassCount / totalRatioCount;

		double errWaterRatioCount = waterRatioCount - baseWater;
		double errSandRatioCount = sandRatioCount - baseSand;
		double errGrassRatioCount = grassRatioCount - baseGrass;

		Uint32 allowed[3];
		if (errWaterRatioCount > 0)
			allowed[0] = (Uint32)(pow(errWaterRatioCount, 0.125) * 4294967296.0);
		else
			allowed[0] = 0;
		if (errSandRatioCount > 0)
			allowed[1] = (Uint32)(pow(errSandRatioCount, 0.125) * 4294967296.0);
		else
			allowed[1] = 0;
		if (errGrassRatioCount > 0)
			allowed[2] = (Uint32)(pow(errGrassRatioCount, 0.125) * 4294967296.0);
		else
			allowed[2] = 0;

		assert(allowed[0] <= (Uint32)0xFFFFFFFF);
		assert(allowed[1] <= (Uint32)0xFFFFFFFF);
		assert(allowed[2] <= (Uint32)0xFFFFFFFF);

		if (i == 0)
		{
			allowed[0] = 0;
			allowed[1] = 0;
			allowed[2] = 0;
		}

		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				if (rng() & 4)
				{
					int a = map.getUMTerrain(x + 1, y);
					int b = map.getUMTerrain(x - 1, y);
					if ((a == b) && (allowed[a] <= rng()))
					{
						map.setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int a = map.getUMTerrain(x, y - 1);
					int b = map.getUMTerrain(x, y + 1);
					if ((a == b) && (allowed[a] <= rng()))
					{
						map.setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				if (rng() & 4)
				{
					int a = map.getUMTerrain(x + 1, y + 1);
					int b = map.getUMTerrain(x - 1, y - 1);
					if ((a == b) && (allowed[a] <= rng()))
					{
						map.setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int a = map.getUMTerrain(x + 1, y - 1);
					int b = map.getUMTerrain(x - 1, y + 1);
					if ((a == b) && (allowed[a] <= rng()))
					{
						map.setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				if (rng() & 4)
				{
					int a = map.getUMTerrain(x + 2, y);
					int b = map.getUMTerrain(x - 2, y);
					if ((a == b) && (allowed[a] <= rng()))
					{
						map.setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int a = map.getUMTerrain(x, y - 2);
					int b = map.getUMTerrain(x, y + 2);
					if ((a == b) && (allowed[a] <= rng()))
					{
						map.setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				if (rng() & 4)
				{
					int a = map.getUMTerrain(x + 2, y + 2);
					int b = map.getUMTerrain(x - 2, y - 2);
					if ((a == b) && (allowed[a] <= rng()))
					{
						map.setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int a = map.getUMTerrain(x + 2, y - 2);
					int b = map.getUMTerrain(x - 2, y + 2);
					if ((a == b) && (allowed[a] <= rng()))
					{
						map.setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
			}
	}
	// What's finally in ?
	waterCount = 0;
	sandCount = 0;
	grassCount = 0;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			switch (map.getUMTerrain(x, y))
			{
			case WATER:
				waterCount++;
				continue;
			case SAND:
				sandCount++;
				continue;
			case GRASS:
				grassCount++;
				continue;
			}
		}
	totalCount = (double)(waterCount + sandCount + grassCount);

	map.controlSand();

	// Now, we have to find suitable places for teams:
	int nbTeams = context.request.nbTeams;
	int minDistSquare = (int)((double)((double)w * (double)h * (double)grassCount) /
							  (double)((double)nbTeams * (double)totalCount));
	if (minDistSquare <= 0)
		return false;
	assert(minDistSquare > 0);
	int *bootX = context.bootX.data();
	int *bootY = context.bootY.data();

	// Each colony takes the widest grass patch that keeps its distance from the ones already
	// placed. The patches are taken largest first, so on a crowded map the last colonies can
	// find every remaining patch too close; rather than refuse the map, such a colony accepts a
	// nearer patch, down to half the spacing, then a quarter. A map that seats every colony at
	// full spacing never reaches the relaxation, so its starts are as they always were.
	auto widestPatch = [&](int team, int spacingSquare, int &maxX, int &maxY)
	{
		int maxSurface = 0;
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
								spacingSquare)
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
		return maxSurface;
	};
	for (int team = 0; team < nbTeams; team++)
	{
		int maxSurface = 0;
		int maxX = 0;
		int maxY = 0;
		for (int spacing : {minDistSquare, minDistSquare / 2, minDistSquare / 4})
		{
			maxSurface = widestPatch(team, spacing, maxX, maxY);
			if (maxSurface > 0)
				break;
		}

		if (maxSurface <= 0)
		{
			context.stage = "starting locations";
			context.detail = "Cannot space every colony on grass";
			return false;
		}
		bootX[team] = maxX;
		bootY[team] = maxY;

		for (int dx = -1; dx < 6; dx++)
			for (int dy = 0; dy < 6; dy++)
				map.setUMTerrain(maxX + dx, maxY + dy, GRASS);
	}

	// Let's add some green space for teams: a meadow around every colony, unless the colonies are
	// to start in the shattered terrain itself.
	int squareSize = 5 + (int)(sqrt((double)minDistSquare) / 4.5);
	for (int team = 0; team < nbTeams && options.colony_meadows; team++)
	{
		map.setUMatPos(context.bootX[team] + 2, context.bootY[team] + 0, GRASS, squareSize);
		map.setUMatPos(context.bootX[team] + 2, context.bootY[team] + 2, GRASS, squareSize);
	}

	map.controlSand();
	map.rebuildTerrain();

	return true;
}

static void resources(Game &game, GenerationContext &context, const ShatteredCoastOptions &options)
{
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();

	int *bootX = context.bootX.data();
	int *bootY = context.bootY.data();
	int nbTeams = context.request.nbTeams;
	int limitDist = (w + h) / (2 * nbTeams);

	// let's add resources to old map generator
	for (int team = 0; team < nbTeams; team++)
	{
		int smallestWidth = limitDist;
		int smallestResource = 0;

		bool dirUsed[8];
		for (int i = 0; i < 8; i++)
			dirUsed[i] = false;
		// Wheat and wood are a colony's two primary resources; stone is secondary. This search
		// gives each of the four slots below its own compass direction, so a fixed 4th entry
		// doesn't just repeat a resource type, it silently starves whichever type isn't chosen
		// of the extra slot every single time. It used to hardcode CORN here, guaranteeing
		// wheat two placement attempts while wood only ever got one — every colony on this
		// generator was systematically wood-poor relative to wheat. Decide the 4th slot after
		// seeing how CORN and WOOD actually did (below) and hand it to whichever came out
		// narrower, so the reinforcement goes to whichever primary resource needs it that game
		// instead of the same one every time.
		int resOrder[4];
		resOrder[0] = CORN;
		resOrder[1] = WOOD;
		resOrder[2] = STONE;
		resOrder[3] = CORN;
		int primaryWidth[2] = {0, 0};

		int distWeight[4];
		distWeight[0] = 1;
		distWeight[1] = 1;
		distWeight[2] = 1;
		distWeight[3] = 2;

		int widthWeight[4];
		widthWeight[0] = 1;
		widthWeight[1] = 1;
		widthWeight[2] = 1;
		widthWeight[3] = 2;

		for (int resI = 0; resI < 4; resI++)
		{
			int res = resOrder[resI];
			int maxDir = 0;
			int maxWidth = 0;
			int maxDist = 0;
			for (int dir = 0; dir < 8; dir++)
				if (!dirUsed[dir])
				{
					int width = 0;
					int dx, dy, dist;
					Unit::dxDyFromDirection(dir, &dx, &dy);
					for (dist = 5; dist < limitDist; dist++)
						if (map.isGrass(bootX[team] + dx * dist, bootY[team] + dy * dist))
							width++;
						else if (width > 3)
							break;
						else
							width = 1;

					if (dist * distWeight[resI] + width * widthWeight[resI] >
						maxDist * distWeight[resI] + maxWidth * widthWeight[resI])
					{
						maxWidth = width;
						maxDist = dist;
						maxDir = dir;
					}
				}
			dirUsed[maxDir] = true;
			if (maxWidth < smallestWidth)
			{
				smallestWidth = maxWidth;
				smallestResource = res;
			}
			if (resI == 0 || resI == 1)
			{
				primaryWidth[resI] = maxWidth;
				if (resI == 1)
					resOrder[3] = primaryWidth[0] <= primaryWidth[1] ? CORN : WOOD;
			}

			int dx, dy;
			Unit::dxDyFromDirection(maxDir, &dx, &dy);
			int d = maxDist - (maxWidth >> 1);
			dx *= d;
			dy *= d;

			// Every deposit is a square of `amount` tiles a side; the amount controls scale its area.
			int amount = context.request.resourceAmounts[res];
			if (amount > 0)
				setScaledResource(map, bootX[team] + dx, bootY[team] + dy, res, amount,
								  options.percent(res));
		}

		if (smallestWidth < limitDist)
		{
			int maxDir = 0;
			int maxWidth = 0;
			int maxDist = 0;
			for (int dir = 0; dir < 8; dir++)
				if (!dirUsed[dir])
				{
					int width = 0;
					int dx, dy, dist;
					Unit::dxDyFromDirection(dir, &dx, &dy);
					for (dist = 0; dist < 2 * limitDist; dist++)
						if (map.isGrass(bootX[team] + dx * dist, bootY[team] + dy * dist))
							width++;
						else if (width > 3)
							break;
						else
							width = 1;

					if (dist + width > maxDist + maxWidth)
					{
						maxWidth = width;
						maxDist = dist;
						maxDir = dir;
					}
				}
			dirUsed[maxDir] = true;

			int dx, dy;
			Unit::dxDyFromDirection(maxDir, &dx, &dy);
			int d = maxDist - (maxWidth >> 1);
			dx *= d;
			dy *= d;

			int amount = context.request.resourceAmounts[smallestResource];
			if (amount > 0)
				setScaledResource(map, bootX[team] + dx, bootY[team] + dy, smallestResource, amount,
								  options.percent(smallestResource));
		}

		int maxDir = 0;
		int maxWidth = 0;
		int maxDist = 0;
		for (int dir = 0; dir < 8; dir++)
		{
			int width = 0;
			int dx, dy, dist;
			Unit::dxDyFromDirection(dir, &dx, &dy);
			for (dist = 0; dist < 2 * limitDist; dist++)
				if (map.isWater(bootX[team] + dx * dist, bootY[team] + dy * dist))
					width++;
				else if (width > 3)
					break;
				else
					width = 1;

			if (dist + width > width + maxWidth)
			{
				maxWidth = width;
				maxDist = dist;
				maxDir = dir;
			}
		}

		int dx, dy;
		Unit::dxDyFromDirection(maxDir, &dx, &dy);
		int d = maxDist - (maxWidth >> 1);
		dx *= d;
		dy *= d;

		int amount = context.request.resourceAmounts[ALGA];
		if (amount > 0)
			setScaledResource(map, bootX[team] + dx, bootY[team] + dy, ALGA, amount, options.algae);
	}

	// Let's smooth resources...
	int maxAmount = 0;
	for (int r = 0; r < 4; r++)
		if (maxAmount < context.request.resourceAmounts[r])
			maxAmount = context.request.resourceAmounts[r];
	map.smoothResources(maxAmount * 3);
}

static bool generate(Game &game, GenerationContext &context)
{
	const ShatteredCoastOptions options(context.request);
	if (!terrain(game, context, options))
		return false;
	context.stage = "starts";
	if (!placeStarts(game, context))
		return false;
	context.stage = "resources";
	resources(game, context, options);
	// The directional search above scores each of a team's 8 compass directions by how far a
	// grass run extends, so a team boxed into a small or oddly-shaped patch can still end up
	// short on wheat or wood — same gap as the other generators, just reached by a different
	// placement method. Top up anyone still missing either within comfortable range now that
	// placeStarts() has already carved its own clearing, so there's nothing left to step on.
	guaranteeStartingResources(game, context, 24, 32);
	// A deposit grown well past its default size can wall a colony into its own clearing with
	// nowhere left to build, which the guarantee above doesn't address: a colony buried in wheat
	// has wheat at its feet. At any amount other than the default, open such a colony back up and
	// guarantee the crops once more. At the defaults none of this runs.
	if (options.wheat != 100 || options.wood != 100 || options.stone != 100 || options.algae != 100)
	{
		openCrampedStarts(game, context);
		guaranteeStartingResources(game, context, 24, 32);
	}
	return true;
}

GeneratorDefinition shatteredCoastDefinition()
{
	return {
		"shattered-coast",
		7,
		"Old random",
		2,
		false,
		{{"water", "Water weight", 0, 100, 1, 40, ControlGroup::Terrain, false, true},
		 {"sand", "Sand weight", 0, 100, 1, 4, ControlGroup::Terrain, false, true},
		 {"grass", "Grass weight", 0, 100, 1, 60, ControlGroup::Terrain, false, true},
		 {"smoothing", "Smoothing", 1, 8, 1, 3, ControlGroup::Terrain, false},
		 // Off, colonies start in the shattered terrain rather than in a cleared meadow.
		 GeneratorControl::toggle("colony-meadows", "Colony meadows", true, ControlGroup::Terrain),
		 // The area of each colony's own wheat, wood, stone and algae deposits.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount")},
		generate};
}
