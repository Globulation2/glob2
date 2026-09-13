// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "ShatteredCoastGenerator.h"
#include "Distances.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GeneratorDefinition.h"
#include "GlobalContainer.h"
#include "HeightMap.h"
#include "Map.h"
#include "Pipeline.h"
#include "Regions.h"
#include "Resources.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include "Unit.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
using namespace MapGeneration;

// Old random (id "shattered-coast", legacy id 7): the game's original random map generator.
//
// HISTORY. Luc-Olivier de Charrière ("nuage") wrote it in August 2002 as the RandomMapGenerator
// that first let network games start without exchanging a map file: paint every tile water, sand or
// grass at random in the requested shares, smooth the noise into blobs, seat colonies on the widest
// grass, and hand each colony a few square deposits found by looking along the eight compass
// directions. When Leo Wandersleb's height-field generators (Islands, Swamp, River, Crater lakes)
// replaced it in January 2006, giszmo brought it back "marked as old" so players kept the look. On
// this branch it moved into the generator framework, was split into the stages below without
// changing a byte of its output, gained resource amounts and a Colony meadows switch, stopped
// refusing crowded maps (see placeColonies) and lost a bias that made every colony wood-poor (see
// resources).
//
// WHAT THE MAP IS. A shattered coastline: grass, sand and water interleaved at every scale, with no
// large-scale structure at all. That is its charm and its weakness. Nothing guarantees a colony
// water for its fields, room to build, or a fair share of anything; fairness comes only from the
// lobby keeping the best-scoring of several seeds and from the shared backstops at the end of
// generate(). Newer generators design structure first and texture second; this one is all texture.
//
// GAME RULES IT LEANS ON (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md):
// - Grass may not touch water, so Map::controlSand turns every grass corner beside water into sand
//   after painting. Heavy smoothing leaves long coasts and so a lot of that forced sand.
// - Wheat and wood only regrow near water. The fine patchwork keeps water close to almost every
//   grass tile, which is why this map's farmland regrows well despite its randomness.
// - Buildings need pure grass: the widest grass patches become homes (placeColonies), and each home
//   is stamped a patch of grass big enough for the 4x4 swarm and its first buildings.
//
// Every draw here comes from named streams ("simulation", "terrain"), so a seed reproduces the map.
// Several odd-looking details (the scratch map's offsets, the algae search's comparison) are
// original behaviour kept on purpose: changing them changes every map this generator makes.

// A dry run of the paint-and-smooth process on a small scratch map, used only to learn what shares
// of water, sand and grass a painting ends up with after `smooth` passes (fitTerrainMix below).
// It reports the shares through the three out-parameters and never touches the real map.
//
// The scratch map is square and a power of two, so wrapping is a bit mask (`& m`): 32 tiles a side
// for 0 to 3 smoothing passes, 64 for 4 to 7 and 128 for 8, big enough that the blobs smoothing
// grows are small against the map and the measured shares settle. Its smoothing is a simplified
// copy of smoothPatchwork's, without the over-share brake. Its first two pairs of neighbours match
// the real pass (across and down, then the two diagonals), but its last two pairs were mistyped in
// the original index arithmetic (`h << 1` is two rows, not two columns): they look one row and two
// columns away, straight up and down, two columns and three rows away, and two columns and one row
// away, where the real pass looks two tiles across, down and along each diagonal. The simulation
// only feeds an estimate that fitTerrainMix corrects, so it is kept as it was.
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

	// The shares become integer weights out of 0x7FFF (32767), fine enough that a share of a
	// fraction of a percent still paints some tiles, and small enough that `rng() % totalRatio`
	// has no noticeable bias.
	int totalRatio = 0x7FFF;
	int waterRatio = (int)(baseWater * ((double)totalRatio));
	int sandRatio = (int)(baseSand * ((double)totalRatio));
	int grassRatio = (int)(baseGrass * ((double)totalRatio));
	totalRatio = waterRatio + sandRatio + grassRatio;

	// All three shares zero: paint the three terrains equally rather than divide by zero.
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

	// Each pass: a tile takes the terrain its two opposite neighbours share, trying one of each
	// pair of directions in turn (horizontal or vertical, then the two diagonals, then two steps
	// out). `rng() & 4` tests one bit of a draw: a coin flip for which of each pair to look along.
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

// The share of water, sand and grass a map is asked for, and what a patchwork actually ends
// up with once smoothed.
struct TerrainMix
{
	double water, sand, grass;
};

static void countTerrain(const Map &map, int &waterCount, int &sandCount, int &grassCount)
{
	waterCount = sandCount = grassCount = 0;
	for (int y = 0; y < map.getH(); y++)
		for (int x = 0; x < map.getW(); x++)
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
}

// Smoothing eats into whichever terrain is rarest, so the shares the patchwork is painted with are
// not the shares the map should end up with. There is no closed form; this searches for painting
// shares whose simulated result lands on the requested ones.
//
// The search is a crude secant-style method on the three shares, run once per smoothing level from
// 1 up to the requested one so each level starts from the previous level's answer:
// - alpha is the current guess. Simulating it gives an error against the requested shares.
// - beta is a proportional correction: each share scaled by requested / achieved, renormalised.
//   Simulating beta gives a second error.
// - The two errors' projection says how far along the alpha-to-beta line the answer lies; gamma
//   tries eleven points on that line (cf from 0 to proj in tenths) and keeps the one whose
//   simulated error is smallest as the next alpha. Each simulation is a full scratch map, so this
//   search, not the real map, is most of this generator's running time at high smoothing.
static TerrainMix fitTerrainMix(GenerationContext &context, const TerrainMix &base, int smooth)
{
	const double baseWater = base.water, baseSand = base.sand, baseGrass = base.grass;
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

	return {alphaWater, alphaSand, alphaGrass};
}

// Every tile draws its terrain independently, in the fitted shares: pure white noise, which the
// smoothing passes then grow into blobs.
static void paintPatchwork(Map &map, std::mt19937 &rng, const TerrainMix &mix)
{
	int totalRatio = 0x7FFF;
	int waterRatio = (int)(((double)mix.water) * ((double)totalRatio));
	int sandRatio = (int)(((double)mix.sand) * ((double)totalRatio));
	int grassRatio = (int)(((double)mix.grass) * ((double)totalRatio));
	if (waterRatio < 0)
		waterRatio = 0;
	if (sandRatio < 0)
		sandRatio = 0;
	if (grassRatio < 0)
		grassRatio = 0;
	totalRatio = waterRatio + sandRatio + grassRatio;

	for (int y = 0; y < map.getH(); y++)
		for (int x = 0; x < map.getW(); x++)
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
}

// The pairs of opposite neighbours a smoothing pass looks across: one draw picks the first or
// the second of each pair of directions, in the order the passes have always used.
static const int kSmoothingDirections[8][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1},
											   {2, 0}, {0, 2}, {2, 2}, {2, -2}};

// Each pass makes a tile take the terrain of two matching opposite neighbours, one direction after
// another, but a terrain already over its requested share is allowed to spread only rarely, the
// more so the further over it is.
//
// The brake: a terrain over its share by `err` (a fraction of the map) may spread into a tile only
// when a fresh 32-bit draw is at least err^(1/8) of the range. The eighth root makes the brake very
// strong for tiny overshoots: 1% over blocks 56% of spreads, 10% over blocks 75%. Measured on
// 256x256 maps with the default weights (40 water, 4 sand, 60 grass) and 3 passes, six seeds: pure
// water tiles come to 18% of the map with this brake, against 26% with a linear brake (err^1) or
// none at all, because water that cannot spread stays broken into many small pools with long
// shores. That fragmentation is the "shattered" look; at 8 passes the difference mostly disappears,
// since by then the blobs are large anyway. The first pass has no brake (allowed is zeroed), so the
// initial noise always gets one free round of clumping before the shares are policed.
static void smoothPatchwork(Map &map, std::mt19937 &rng, int smooth, const TerrainMix &base)
{
	const int w = map.getW(), h = map.getH();
	for (int i = 0; i < smooth; i++)
	{
		int waterCount, sandCount, grassCount;
		countTerrain(map, waterCount, sandCount, grassCount);
		double totalRatioCount = (double)(waterCount + sandCount + grassCount);
		double waterRatioCount = waterCount / totalRatioCount;
		double sandRatioCount = sandCount / totalRatioCount;
		double grassRatioCount = grassCount / totalRatioCount;

		double errWaterRatioCount = waterRatioCount - base.water;
		double errSandRatioCount = sandRatioCount - base.sand;
		double errGrassRatioCount = grassRatioCount - base.grass;

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

		if (i == 0)
		{
			allowed[0] = 0;
			allowed[1] = 0;
			allowed[2] = 0;
		}

		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				for (int pair = 0; pair < 4; pair++)
				{
					const int d = (rng() & 4) ? 2 * pair : 2 * pair + 1;
					const int dx = kSmoothingDirections[d][0], dy = kSmoothingDirections[d][1];
					const int a = map.getUMTerrain(x + dx, y + dy);
					const int b = map.getUMTerrain(x - dx, y - dy);
					if ((a == b) && (allowed[a] <= rng()))
					{
						map.setUMTerrain(x, y, (TerrainType)a);
						break;
					}
				}
	}
}

// Seat every colony on grass, each a colony's share of the grass from the others, and give each a
// meadow unless the colonies are to start in the shattered terrain itself. The counts are the map's
// before sand control ran.
//
// Spacing: minDistSquare = width * height * grassShare / colonies is the area of one colony's share
// of the grass, used as a squared distance, so colonies sit about the side of a square of that much
// grass apart - further apart on grassy maps and with few colonies, closer on watery or crowded
// ones.
static bool placeColonies(Map &map, GenerationContext &context,
						  const ShatteredCoastOptions &options, int grassCount, double totalCount)
{
	const int w = map.getW(), h = map.getH();
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
	// A patch is measured cheaply, not exactly: every horizontal run of more than 7 grass tiles (a
	// run that could hold the 4x4 swarm with room either side) is measured vertically through its
	// middle, and the run's width times that height stands in for its area. The largest such
	// rectangle that keeps its spacing from the colonies already placed wins.
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

		// A 7x6 block of grass corners under the boot tile: the 4x4 swarm with a ring for its
		// workers, so the swarm always fits whatever the patch's real outline.
		for (int dx = -1; dx < 6; dx++)
			for (int dy = 0; dy < 6; dy++)
				map.setUMTerrain(maxX + dx, maxY + dy, GRASS);
	}

	// The meadow: two overlapping grass squares, 5 tiles plus a fraction of the colony spacing a
	// side (about 25 on a 256x256 map with 4 colonies and the default weights), so a home has clear
	// building room that grows with the ground each colony is meant to have. The 4.5 divisor keeps
	// a meadow to about a quarter of the spacing, so meadows of colonies seated at full spacing
	// stay apart; colonies seated at relaxed spacing (below) can have meadows that touch.
	int squareSize = 5 + (int)(sqrt((double)minDistSquare) / 4.5);
	for (int team = 0; team < nbTeams && options.colony_meadows; team++)
	{
		map.setUMatPos(context.bootX[team] + 2, context.bootY[team] + 0, GRASS, squareSize);
		map.setUMatPos(context.bootX[team] + 2, context.bootY[team] + 2, GRASS, squareSize);
	}
	return true;
}

static bool terrain(Game &game, GenerationContext &context, const ShatteredCoastOptions &options)
{
	Map &map = game.map;
	// Every draw below names the "terrain" stream; one lookup serves all of them.
	std::mt19937 &rng = context.stream("terrain");

	const int totalRatio = options.water + options.sand + options.grass;
	TerrainMix base{1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0};
	if (totalRatio != 0)
		base = {(float)options.water / (float)totalRatio, (float)options.sand / (float)totalRatio,
				(float)options.grass / (float)totalRatio};
	const TerrainMix mix = fitTerrainMix(context, base, options.smoothing);
	paintPatchwork(map, rng, mix);
	smoothPatchwork(map, rng, options.smoothing, base);
	int waterCount, sandCount, grassCount;
	countTerrain(map, waterCount, sandCount, grassCount);
	const double totalCount = (double)(waterCount + sandCount + grassCount);

	// Sand control twice: once so the patches colonies are measured on are real grass (grass beside
	// water has just become beach), and again because the grass stamped for each swarm and meadow
	// may itself touch water.
	map.controlSand();
	if (!placeColonies(map, context, options, grassCount, totalCount))
		return false;
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
	// How far along a compass direction a colony looks for ground to put a deposit on: the mean
	// side of the map shared out between the colonies, so deposits stay in a colony's own half of
	// the way to its neighbours.
	int limitDist = (w + h) / (2 * nbTeams);

	// Each colony gets its deposits along compass directions from its swarm, one direction per
	// deposit, so they spread round the home rather than piling up on one side. For each deposit,
	// every unused direction is walked out from 5 tiles (clear of the swarm) to limitDist, tracking
	// the current run of grass: a gap after a run of 4 or more ends the walk, a gap after a shorter
	// run restarts the count (so a sliver of grass between water does not count as a field). The
	// direction scoring best on distance + run width wins, and a square deposit is centred on the
	// middle of that run. `amount` is the deposit's side in tiles (the request's legacy
	// per-resource amounts, 7 by default), scaled in area by the amount controls. Map::setResource
	// draws each tile's starting amount from the gameplay RNG, so the order below is part of the
	// map.
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

		// The fourth, reinforcing deposit scores distance and width double: it goes to the
		// furthest, widest run left, where there is room for a second field of the scarcer crop.
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

			// Every deposit is a square of `amount` tiles a side; the amount controls scale its
			// area.
			int amount = context.request.resourceAmounts[res];
			if (amount > 0)
				setScaledResource(map, bootX[team] + dx, bootY[team] + dy, res, amount,
								  options.percent(res));
		}

		// One more deposit of whichever resource landed on the narrowest run, looking up to twice
		// as far: a colony whose wheat or wood field is squeezed gets a second field somewhere
		// roomier.
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

		// Algae: the same walk over water in all eight directions. Its comparison reads
		// `dist + width > width + maxWidth`, which reduces to dist > maxWidth rather than the grass
		// searches' dist + width > maxDist + maxWidth: a slip in the original that biases algae
		// towards whichever direction first runs further than the widest water run so far. It is
		// kept, since fixing it moves every colony's algae on every map this generator has made.
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

	// Map::smoothResources frays the square deposits into natural fields, three rounds per tile of
	// the largest deposit side: each round grows small deposit tiles and sprouts large ones onto a
	// free neighbour. From 2003 until revision 3 it read the old resource encoding and did nothing,
	// so the deposits stayed visible squares. Working, it roughly doubles the deposits (over 12
	// seeds at 256x256: wheat 251 to 551 tiles, wood 467 to 1022 at 4 colonies) and brings them
	// closer to the swarms, at the cost of about half the building sites near each colony.
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
	// nowhere left to build.
	reopenCrampedStarts(game, context, {options.wheat, options.wood, options.stone, options.algae});
	return true;
}

GeneratorDefinition shatteredCoastDefinition()
{
	return {
		"shattered-coast",
		7,
		"Old random",
		3,
		false,
		// The three terrain weights are relative (40/4/60 asks for 38% water, 4% sand, 58% grass
		// before sand control adds the beaches); smoothing is the number of passes, which sets the
		// scale of the blobs from speckle (1) to broad coasts (8).
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
