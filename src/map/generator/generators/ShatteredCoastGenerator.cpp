// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "ShatteredCoastGenerator.h"
#include "Distances.h"
#include "GenerationWorld.h"
#include "GenerationContext.h"
#include "GeneratorDefinition.h"
#include "GlobalContainer.h"
#include "HeightMap.h"
#include "LatticeNoise.h"
#include "Morphology.h"
#include "Pipeline.h"
#include "Planting.h"
#include "Growth.h"
#include "Regions.h"
#include "Resources.h"
#include "Rock.h"
#include "Sketch.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include "TerrainCatalog.h"
#include "Topology.h"
#include "Unit.h"
#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <tuple>
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
// GAME RULES IT LEANS ON (docs/map-generators/game-rules-for-map-design.md):
// - Grass may not touch water, so Map::layBeaches turns every grass corner beside water, and every
//   water corner beside grass, into sand after painting. Heavy smoothing leaves long coasts and so
//   a lot of that forced sand.
// - Wheat and wood only regrow near water. The fine patchwork keeps water close to almost every
//   grass tile, which is why this map's farmland regrows well despite its randomness.
// - Buildings need pure grass: the widest grass patches become homes (placeColonies), and each home
//   is stamped a patch of grass big enough for the 4x4 swarm and its first buildings.
//
// Every draw uses named streams ("simulation", "terrain", "shattered-coast-expansion"),
// so a seed reproduces the map.
// Several odd-looking details (the scratch map's offsets, the algae search's comparison) are
// original behaviour kept on purpose: changing them changes every map this generator makes.
//
// SHATTERED COAST 2.0 (revision 9): the same patchwork, drawn with the engine's full terrain palette.
// The design brief: a photograph of a shattered coast shows (1) dark water in the middle of the
// bigger bays and lakes with a pale band of shallows along every shore; (2) mud in the sheltered
// coves and small ponds, pale sand on the open shores and dunes on the dry patches inland; (3) lush
// meadows in the broad stretches of grass and parched grass on the thin necks between waters; (4)
// rock: sea stacks where the sea has eaten a headland down to its last speck, and knots of bedrock
// standing out of the wider grass, quarried on the side the nearest colony comes from. Its patchwork
// character is the point, so this is a post-pass, not a redesign:
// - Revision 8 runs unchanged first: the fitted mix, painting, smoothing, beaches, starts, home
//   deposits, opening repairs and expansion clumps are identical for the same seed (every new draw
//   uses streams of its own). The pass then repaints only ground nobody stands on: no tile holding
//   a deposit, building or unit changes its rules, so every resource lies where it lay and stays
//   legal, and nothing that ran before it has to know about the new terrain.
// - Two sketches. The pass keeps a mechanic sketch of canonical types (WATER, DEEP_WATER, SAND,
//   GRASS, MUDFLAT, RIDGE_ROCK) that every check reads, and paints looks over it with composeLooks
//   (Sketch.h), so looks never change a rule.
// - Deep water (`deep-water`): water vertices kDeepSteps steps from land (deepCore, Rock.h), opened
//   and with specks dropped, so only the bigger bays and lakes have a core and every shore keeps two
//   tiles of plain shallows: algae, beaches and swimming access work as before. No corner of a tile
//   holding algae turns deep. Deep water waters the land like water but is slower to swim and holds
//   no algae: crossing a big bay takes longer than in revision 8 (a feel change).
// - Mudflats (`mudflats`): beach sand of the small, sheltered waters (bodies of at most kMudBody
//   vertices) turns to mud where a noise field passes, never within kMudHome tiles of a swarm, where
//   the sand is the containment between a home and its overgrowth. Mudflat (Rough: slow, unbuildable,
//   holds nothing) does not inhibit growth as sand does, so crops come a little closer to those
//   ponds; it gives algae no shore support either, so the open shores, where algae live, stay sand.
// - Rock (`outcrops`): sea stacks, islands of at most kStackMost land vertices with nothing on them,
//   turned whole to rock (Ridges: impassable, towers shoot over); and knots of bedrock, lenses at
//   least two tiles thick (rockVertices) on wide vacant grass, kKnotHome tiles or more from every
//   swarm. Stone-amount sets how many knots carry a quarry face on the side facing the nearest swarm
//   (stone on grass, tiles left out of the rock). A knot or stack whose rock parts the walkable
//   ground round it is dropped, so the rock never cuts anything off; validateWorld checks it again.
// - A climate, looks only (Temperate, Steppe, Tropical): beach, dune, mud, deep water and rock looks
//   and how much of the grass is lush or parched. Every layer and choice is in telemetry.

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
	std::vector<int> vertices(w * h);

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
				vertices[y * w + x] = 0;
				continue;
			}
			r -= sandRatio;
			if (r < 0)
			{
				vertices[y * w + x] = 1;
				continue;
			}
			r -= grassRatio;
			if (r < 0)
			{
				vertices[y * w + x] = 2;
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
					int a = vertices[(y * w + x + 1 + s) & m];
					int b = vertices[(y * w + x - 1 + s) & m];
					if (a == b)
					{
						vertices[y * w + x] = a;
						continue;
					}
				}
				else
				{
					int a = vertices[(y * w + x + w + s) & m];
					int b = vertices[(y * w + x - w + s) & m];
					if (a == b)
					{
						vertices[y * w + x] = a;
						continue;
					}
				}
				if (rng() & 4)
				{
					int a = vertices[(y * w + x + w + 1 + s) & m];
					int b = vertices[(y * w + x - w - 1 + s) & m];
					if (a == b)
					{
						vertices[y * w + x] = a;
						continue;
					}
				}
				else
				{
					int a = vertices[(y * w + x + w - 1 + s) & m];
					int b = vertices[(y * w + x - w + 1 + s) & m];
					if (a == b)
					{
						vertices[y * w + x] = a;
						continue;
					}
				}
				if (rng() & 4)
				{
					int a = vertices[(y * w + x + w - 2 + s) & m];
					int b = vertices[(y * w + x - w + 2 + s) & m];
					if (a == b)
					{
						vertices[y * w + x] = a;
						continue;
					}
				}
				else
				{
					int a = vertices[(y * w + x + w - (h << 1) + s) & m];
					int b = vertices[(y * w + x - w + (h << 1) + s) & m];
					if (a == b)
					{
						vertices[y * w + x] = a;
						continue;
					}
				}
				if (rng() & 4)
				{
					int a = vertices[(y * w + x + w + 2 + (h << 1) + s) & m];
					int b = vertices[(y * w + x - w - 2 - (h << 1) + s) & m];
					if (a == b)
					{
						vertices[y * w + x] = a;
						continue;
					}
				}
				else
				{
					int a = vertices[(y * w + x + w + 2 - (h << 1) + s) & m];
					int b = vertices[(y * w + x - w - 2 + (h << 1) + s) & m];
					if (a == b)
					{
						vertices[y * w + x] = a;
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
			switch (vertices[y * w + x])
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

static void countTerrain(const GenerationMap &map, int &waterCount, int &sandCount, int &grassCount)
{
	waterCount = sandCount = grassCount = 0;
	for (int y = 0; y < map.getH(); y++)
		for (int x = 0; x < map.getW(); x++)
		{
			switch (map.vertexTerrainAt(x, y))
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
			default:
				// This recipe measures only its three legacy corner materials.
				assert(false);
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

	context.telemetry.measure("shattered-coast.mix.fitted-water", alphaWater);
	context.telemetry.measure("shattered-coast.mix.fitted-sand", alphaSand);
	context.telemetry.measure("shattered-coast.mix.fitted-grass", alphaGrass);
	return {alphaWater, alphaSand, alphaGrass};
}

// Every tile draws its terrain independently, in the fitted shares: pure white noise, which the
// smoothing passes then grow into blobs.
static void paintPatchwork(GenerationMap &map, std::mt19937 &rng, const TerrainMix &mix)
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
				map.setVertexTerrain(x, y, WATER);
				continue;
			}
			r -= sandRatio;
			if (r < 0)
			{
				map.setVertexTerrain(x, y, SAND);
				continue;
			}
			r -= grassRatio;
			if (r < 0)
			{
				map.setVertexTerrain(x, y, GRASS);
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
static void smoothPatchwork(GenerationMap &map, std::mt19937 &rng, int smooth, const TerrainMix &base)
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
					const int a = map.vertexTerrainAt(x + dx, y + dy);
					const int b = map.vertexTerrainAt(x - dx, y - dy);
					if ((a == b) && (allowed[a] <= rng()))
					{
						map.setVertexTerrain(x, y, (TerrainType)a);
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
static bool placeColonies(GenerationMap &map, GenerationContext &context,
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
				int a = map.vertexTerrainAt(x, y);
				if (a == GRASS)
					width++;
				else
				{
					if (width > 7)
					{
						int centerX = ((x + startX) >> 1);
						int top, bot;
						for (top = 0; top < h; top++)
							if (map.vertexTerrainAt(centerX, y - top) != GRASS)
								break;
						for (bot = 0; bot < h; bot++)
							if (map.vertexTerrainAt(centerX, y + bot) != GRASS)
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
			{
				context.telemetry.measure("shattered-coast.starts.spacing-squared", spacing, team);
				context.telemetry.measure("shattered-coast.starts.patch-score", maxSurface, team);
				if (spacing < minDistSquare)
					context.telemetry.fallback("shattered-coast.starts.spacing-relaxed",
											   "No wide grass patch at preferred spacing", team);
				break;
			}
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
				map.setVertexTerrain(maxX + dx, maxY + dy, GRASS);
	}

	// The meadow: two overlapping grass squares, 5 tiles plus a fraction of the colony spacing a
	// side (about 25 on a 256x256 map with 4 colonies and the default weights), so a home has clear
	// building room that grows with the ground each colony is meant to have. The 4.5 divisor keeps
	// a meadow to about a quarter of the spacing, so meadows of colonies seated at full spacing
	// stay apart; colonies seated at relaxed spacing (below) can have meadows that touch.
	int squareSize = 5 + (int)(sqrt((double)minDistSquare) / 4.5);
	for (int team = 0; team < nbTeams && options.colony_meadows; team++)
	{
		map.paintVertexSquare(context.bootX[team] + 2, context.bootY[team] + 0, GRASS, squareSize);
		map.paintVertexSquare(context.bootX[team] + 2, context.bootY[team] + 2, GRASS, squareSize);
	}
	return true;
}

static bool terrain(GenerationWorld &game, GenerationContext &context, const ShatteredCoastOptions &options)
{
	GenerationMap &map = game.map;
	// Each vertex write re-derives its four tiles at once; the batch holds the map-wide follow-up
	// (terrain generation, route invalidation) until the terrain is finished.
	auto batch = map.editTerrain();
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
	context.telemetry.measure("shattered-coast.terrain.water-before-beaches", waterCount);
	context.telemetry.measure("shattered-coast.terrain.sand-before-beaches", sandCount);
	context.telemetry.measure("shattered-coast.terrain.grass-before-beaches", grassCount);
	const double totalCount = (double)(waterCount + sandCount + grassCount);

	// Sand control twice: once so the patches colonies are measured on are real grass (grass beside
	// water has just become beach), and again because the grass stamped for each swarm and meadow
	// may itself touch water.
	map.layBeaches();
	if (!placeColonies(map, context, options, grassCount, totalCount))
		return false;
	map.layBeaches();
	return true;
}

static void resources(GenerationWorld &game, GenerationContext &context, const ShatteredCoastOptions &options)
{
	GenerationMap &map = game.map;
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
		// of the extra slot every single time. It used to hardcode WHEAT here, guaranteeing
		// wheat two placement attempts while wood only ever got one — every colony on this
		// generator was systematically wood-poor relative to wheat. Decide the 4th slot after
		// seeing how WHEAT and WOOD actually did (below) and hand it to whichever came out
		// narrower, so the reinforcement goes to whichever primary resource needs it that game
		// instead of the same one every time.
		int resOrder[4];
		resOrder[0] = WHEAT;
		resOrder[1] = WOOD;
		resOrder[2] = STONE;
		resOrder[3] = WHEAT;
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
						if (map.terrainSupportsResourceAtByIndex(bootX[team] + dx * dist, bootY[team] + dy * dist, res))
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
					resOrder[3] = primaryWidth[0] <= primaryWidth[1] ? WHEAT : WOOD;
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
						if (map.terrainSupportsResourceAtByIndex(bootX[team] + dx * dist, bootY[team] + dy * dist, smallestResource))
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
				if (map.terrainSupportsResourceAtByIndex(bootX[team] + dx * dist, bootY[team] + dy * dist, ALGA))
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
	context.telemetry.measure("shattered-coast.materials.smoothing-rounds", maxAmount * 3);
	map.smoothResources(maxAmount * 3);
}

// A second, independent furnishing pass gives the coast destinations beyond the home deposits.
// Run after all opening repairs: no new deposit or RNG draw can change the established starts.
static void expansionResources(GenerationWorld &game, GenerationContext &context, const ShatteredCoastOptions &options)
{
	GenerationMap &map = game.map;
	const Torus t(map);
	const auto protectedHome = swarmSurroundings(t, context, 32);
	const auto vacant = [&](int i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		return !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID && map.getGroundUnit(x, y) == NOGUID;
	};
	const auto fertility = Fertility::forMap(map, false);
	std::vector<int> land, water;
	for (int i = 0; i < t.size(); ++i)
	{
		generationCheckpoint();
		if (protectedHome[i] || !vacant(i))
			continue;
		// By rule group, not exact type: pure grass or pure water whatever its look. The pass that
		// paints looks runs after this one, so these are the tiles revision 8 chose.
		const TerrainProperties &rules = map.terrainPropertiesAt(t.remainderX(i), i / t.w);
		if (sameTerrainProperties(rules, terrainGroupDefinition(TerrainGroup::Grass).properties))
			land.push_back(i);
		else if (sameTerrainProperties(rules, terrainGroupDefinition(TerrainGroup::Water).properties))
			water.push_back(i);
	}
	std::vector<int> centres;
	for (const auto [type, density, size] : {std::tuple{WHEAT, 700, 40}, {WOOD, 900, 32},
		std::tuple{STONE, 2200, 12}, std::tuple{ALGA, 1600, 12}})
	{
		const auto &ground = type == ALGA ? water : land;
		const int wanted = int(scaledCount(int(ground.size()) / density, options.percent(type)));
		int patches = 0, tiles = 0;
		for (int attempt = 0; attempt < wanted * 40 && patches < wanted && !ground.empty(); ++attempt)
		{
			generationCheckpoint();
			const int seed = ground[context.bounded("shattered-coast-expansion", unsigned(ground.size()))];
			const int sx = t.remainderX(seed), sy = seed / t.w;
			if (std::any_of(centres.begin(), centres.end(), [&](int c)
				{ return t.chebyshev(sx, sy, t.remainderX(c), c / t.w) < 12; }))
				continue;
			const auto eligible = [&](int i)
			{
				return !protectedHome[i] && t.chebyshev(sx, sy, t.remainderX(i), i / t.w) <= 5 &&
					vacant(i) &&
					map.isResourceAllowed(t.remainderX(i), i / t.w, type) &&
					((type != WHEAT && type != WOOD) || fertility.values()[i] > 0);
			};
			// Reject slivers before planting: each destination must be a useful connected clump,
			// rather than another scatter of individual resource tiles on tiny coastal fragments.
			std::vector<int> pocket{seed};
			if (!eligible(seed))
				continue;
			for (size_t head = 0; head < pocket.size() && pocket.size() < size_t(size); ++head)
				for (const auto &step : kCardinalSteps)
				{
					const int next = t.at(t.remainderX(pocket[head]) + step[0], pocket[head] / t.w + step[1]);
					if (eligible(next) && std::find(pocket.begin(), pocket.end(), next) == pocket.end())
						pocket.push_back(next);
				}
			if (pocket.size() < 8)
				continue;
			tiles += growPatch(map, t, seed, type, size, eligible);
			centres.push_back(seed);
			++patches;
		}
		const std::string prefix = "shattered-coast.expansion." + std::to_string(type);
		context.telemetry.measure(prefix + ".target-clumps", wanted);
		context.telemetry.measure(prefix + ".clumps", patches);
		context.telemetry.measure(prefix + ".tiles", tiles);
		if (patches < wanted)
			context.telemetry.fallback(prefix + ".shortfall", "Not enough separated coastal pockets outside the starting areas");
	}
}

// ===== 2.0: the coast pass ======================================================================
namespace
{
// Streams of their own, so every draw revision 8 made is the draw it made.
constexpr const char *kKnotStream = "shattered-coast-knots";
constexpr const char *kStackStream = "shattered-coast-stacks";
constexpr const char *kLookStream = "shattered-coast-looks";
constexpr const char *kClimateStream = "shattered-coast-climate";
// The mudflats' noise draws from a stream of its own, so turning them off changes no look.
constexpr const char *kMudStream = "shattered-coast-mud";
// Deep water: water vertices at least kDeepSteps eight-connected steps from land, opened by
// kDeepOpen and kept only in bodies of kDeepLeast vertices or more, so no lone dark vertex reads as
// a hole. Four steps leave two tiles of pure shallow water along every shore.
constexpr int kDeepSteps = 4, kDeepOpen = 1, kDeepLeast = 12;
// Mudflats: the beach of a water body of at most kMudBody vertices (a cove, a pond, a lagoon), where
// a noise field passes kMudCut (of 65535), never within kMudHome tiles of a swarm's footprint. Mud
// is slow to walk, and the beaches are a shattered coast's footpaths: with mud from 16 tiles out,
// three calibration games (seed 101) lost 6% of their worker births (one seat 19%) despite the
// growth gain; from 28 tiles out they matched revision 8.
constexpr int kMudBody = 500, kMudCut = 24000, kMudHome = 28;
// Sea stacks: islands of at most kStackMost land vertices with nothing on them, kStackHome tiles or
// more from every swarm, kStackShare percent of them.
constexpr int kStackMost = 10, kStackHome = 18, kStackShare = 60;
// Knots: one wanted per kTilesPerKnot tiles of map; centres on vacant pure grass at least
// kKnotHome tiles from every swarm's footprint and kKnotSpacing from each other. A knot is a lens
// up to kKnotHalfLength + 2 tiles each way along its axis and 2 to 2.8 tiles thick each way at its
// middle, and keeps kKnotMargin tiles of vacant grass round it (the quarry's approach and a
// walk round the rock). Faces go on kFaceShare of the knots at the default stone-amount: the rim
// tiles within kFaceHalf of the middle on the side facing the nearest swarm, at least kFaceLeast.
constexpr int kTilesPerKnot = 8192, kKnotHome = 24, kKnotSpacing = 26, kKnotHalfLength = 3, kKnotMargin = 2;
constexpr int kFaceHalf = 2, kFaceLeast = 4;
constexpr double kFaceShare = 0.75;
// Ground within this many tiles of a colony's workers never holds a ridges vertex (validateWorld).
constexpr int kRockHomeCheck = 16;

// The look-only climates. lush is the grass clearance (steps to the nearest non-grass vertex) from
// which grass is lush; sliver the widest clearance a thin neck of grass may reach and still be dry;
// dryRims whether the rim of every patch dries out too.
struct Climate
{
	const char *name;
	TerrainType beach, beachSecond, dune, duneSecond, deep, mud, rock;
	int lush, sliver;
	bool dryRims;
};
constexpr Climate kClimates[3] = {
	{"temperate", PALE_SAND, SAND, DUNE_SAND, PALE_SAND, DEEP_WATER, MUDFLAT, OUTCROP, 5, 2, false},
	{"steppe", SAND, PALE_SAND, WINDY_SAND_DUNE, DUNE_SAND, DEEP_WATER, HARDPAN, HOODOOS, 8, 3, true},
	{"tropical", PALE_SAND, PALE_SAND, DUNE_SAND, SAND, DARK_WATER, MUD, OUTCROP, 3, 1, false}};

double sq(double v)
{
	return v * v;
}

// Keeps only the tiles of `mask` that lie in some 2x2 block of it, so every kept tile has a rock vertex.
std::vector<unsigned char> blocksOfTwo(const Torus &t, const std::vector<unsigned char> &mask)
{
	std::vector<unsigned char> out(mask.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			generationCheckpoint();
			if (mask[t.at(x, y)] && mask[t.at(x + 1, y)] && mask[t.at(x, y + 1)] && mask[t.at(x + 1, y + 1)])
				for (int dy = 0; dy <= 1; ++dy)
					for (int dx = 0; dx <= 1; ++dx)
						out[t.at(x + dx, y + dy)] = 1;
		}
	return out;
}

// The tiles with two or more corners in `vertices`: what the engine blocks for rock corners.
std::vector<unsigned char> blockedByCorners(const Torus &t, const std::vector<unsigned char> &vertices)
{
	std::vector<unsigned char> blocked(vertices.size(), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
			blocked[t.at(x, y)] =
				vertices[t.at(x, y)] + vertices[t.at(x + 1, y)] + vertices[t.at(x, y + 1)] + vertices[t.at(x + 1, y + 1)] >= 2;
	return blocked;
}

// Whether some blocked group of `blocked` (8-connected) parts the ground round it: the open tiles
// beside it fall in more than one walkable component, so the group cut some ground off (or stands
// between two grounds that were apart, which is refused too). Returns the first such group's label,
// or -1; `label` receives the groups. (The same check as Watershed's, Everglades' and Ring World's.)
int partingGroup(const Torus &t, const std::vector<unsigned char> &walkable,
				 const std::vector<unsigned char> &blocked, std::vector<int> &label)
{
	const int n = t.size();
	std::vector<unsigned char> open(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		open[i] = walkable[i] && !blocked[i];
	const std::vector<int> component = connectedRegions(open, t.w, t.h, true, GridNeighbors::Eight);
	label = connectedRegions(blocked, t.w, t.h, true, GridNeighbors::Eight);
	int groups = 0;
	for (int l : label)
		groups = std::max(groups, l + 1);
	std::vector<int> seen(size_t(groups), -1);
	for (int i = 0; i < n; ++i)
	{
		generationCheckpoint();
		if (label[i] < 0)
			continue;
		const int x = t.remainderX(i), y = i / t.w;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int j = t.at(x + dx, y + dy);
				if (!open[j])
					continue;
				if (seen[label[i]] < 0)
					seen[label[i]] = component[j];
				else if (seen[label[i]] != component[j])
					return label[i];
			}
	}
	return -1;
}

// The rock of the pass, planned on the finished revision-8 map. Knot masks are tiles; rock is
// vertices.
struct Outcrops
{
	std::vector<unsigned char> knotRock; // knot tiles drawn as rock (rockVertices)
	std::vector<unsigned char> quarry;   // quarry faces: stone on grass, left out of the rock
	std::vector<unsigned char> rock;     // every rock vertex, knots and stacks
	int wanted = 0, knots = 0, faces = 0, faceTiles = 0, stacks = 0, stackVertices = 0, dropped = 0;
};

Outcrops planOutcrops(const GenerationMap &map, const Torus &t, GenerationContext &context, const ShatteredCoastOptions &o)
{
	const int n = t.size(), teams = context.request.nbTeams;
	Outcrops out;
	out.knotRock.assign(size_t(n), 0);
	out.quarry.assign(size_t(n), 0);
	out.rock.assign(size_t(n), 0);
	if (!o.outcrops)
		return out;
	const auto vacant = [&](int i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		return !map.isResource(x, y) && map.getBuilding(x, y) == NOGBID && map.getGroundUnit(x, y) == NOGUID;
	};
	std::vector<unsigned char> vacantGrass(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		generationCheckpoint();
		const int x = t.remainderX(i), y = i / t.w;
		vacantGrass[i] = vacant(i) && map.vertexTerrainAt(x, y) == GRASS && map.vertexTerrainAt(t.x(x + 1), y) == GRASS &&
						 map.vertexTerrainAt(x, t.y(y + 1)) == GRASS && map.vertexTerrainAt(t.x(x + 1), t.y(y + 1)) == GRASS;
	}
	const std::vector<int> room = clearance(t, vacantGrass);
	const std::vector<unsigned char> homes = swarmSurroundings(t, context, kKnotHome);
	const auto nearestSwarm = [&](int x, int y)
	{
		int best = INT_MAX, bx = x, by = y;
		for (int k = 0; k < teams; ++k)
		{
			const int d = t.dist2(x, y, context.bootX[k] + 2, context.bootY[k] + 2);
			if (d < best)
			{
				best = d;
				bx = context.bootX[k] + 2;
				by = context.bootY[k] + 2;
			}
		}
		return std::pair{double(t.offsetX(x, bx)), double(t.offsetY(y, by))};
	};

	// Knots.
	std::vector<int> candidates;
	for (int i = 0; i < n; ++i)
		if (vacantGrass[i] && !homes[i] && room[i] >= 2 + kKnotMargin + 1)
			candidates.push_back(i);
	context.shuffle(candidates.begin(), candidates.end(), kKnotStream);
	out.wanted = std::max(1, n / kTilesPerKnot);
	struct Placed
	{
		int centre;
		std::vector<int> tiles;
		double faceX, faceY; // unit vector from the centre towards the face side
	};
	std::vector<Placed> placed;
	for (int c : candidates)
	{
		if (int(placed.size()) >= out.wanted)
			break;
		const int cx = t.remainderX(c), cy = c / t.w;
		bool apart = true;
		for (const Placed &p : placed)
			apart = apart && t.dist2(cx, cy, t.remainderX(p.centre), p.centre / t.w) >= kKnotSpacing * kKnotSpacing;
		if (!apart)
			continue;
		const double heading = context.bounded(kKnotStream, 8) * 3.14159265358979 / 8;
		const double ax = std::cos(heading), ay = std::sin(heading), bx = -ay, by = ax;
		const double half =
			std::min<double>(kKnotHalfLength + context.bounded(kKnotStream, 3), room[c] - kKnotMargin - 1);
		const double thick = 2.0 + context.bounded(kKnotStream, 9) / 10.0;
		const double phase = context.bounded(kKnotStream, 628) / 100.0;
		std::vector<unsigned char> local(size_t(n), 0);
		std::vector<int> tiles;
		bool fits = true;
		const int box = int(half) + 3;
		for (int dy = -box; dy <= box && fits; ++dy)
			for (int dx = -box; dx <= box && fits; ++dx)
			{
				const double a = dx * ax + dy * ay, b = dx * bx + dy * by;
				if (std::abs(a) > half + 0.5)
					continue;
				const double reach = thick * std::sqrt(std::max(0.0, 1 - sq(a / (half + 1)))) +
									 0.35 * std::sin(0.9 * a + phase) * (b > 0 ? 1 : -1) + 0.35;
				if (std::abs(b) > reach)
					continue;
				const int i = t.at(cx + dx, cy + dy);
				// Every knot tile has kKnotMargin tiles of vacant grass round it, away from the homes.
				if (room[i] <= kKnotMargin || homes[i])
					fits = false;
				local[i] = 1;
				tiles.push_back(i);
			}
		if (!fits)
			continue;
		std::vector<int> kept;
		{
			const std::vector<unsigned char> blocks = blocksOfTwo(t, local);
			for (int i : tiles)
				if (blocks[i])
					kept.push_back(i);
		}
		if (kept.size() < 8)
			continue;
		const auto [fx, fy] = nearestSwarm(cx, cy);
		// The face looks across the lens, on the side the nearest swarm lies.
		const double side = fx * bx + fy * by;
		placed.push_back({c, kept, side >= 0 ? bx : -bx, side >= 0 ? by : -by});
	}

	// Quarry faces on the share of the knots stone-amount sets, in the knots' order: a knot whose face
	// does not fit (too few rim tiles, or too little rock left behind it) hands its turn to the next.
	const int faceQuota = int(std::min<std::int64_t>(
		std::int64_t(placed.size()), scaledCount(int(std::lround(placed.size() * kFaceShare)), o.stone)));
	std::vector<std::vector<int>> faces(placed.size());
	int faced = 0;
	for (size_t q = 0; q < placed.size() && faced < faceQuota; ++q)
	{
		const Placed &p = placed[q];
		std::vector<unsigned char> local(size_t(n), 0);
		for (int i : p.tiles)
			local[i] = 1;
		const int cx = t.remainderX(p.centre), cy = p.centre / t.w;
		const double ax = -p.faceY, ay = p.faceX;
		std::vector<int> face;
		for (int i : p.tiles)
		{
			const double dx = t.offsetX(cx, t.remainderX(i)), dy = t.offsetY(cy, i / t.w);
			if (dx * p.faceX + dy * p.faceY < 0.5 || std::abs(dx * ax + dy * ay) > kFaceHalf + 0.5)
				continue;
			bool rim = false;
			for (const auto &step : kCardinalSteps)
				rim = rim || !local[t.at(t.remainderX(i) + step[0], i / t.w + step[1])];
			if (rim)
				face.push_back(i);
		}
		// The rock behind the face must still stand two tiles thick.
		for (int i : face)
			local[i] = 0;
		const std::vector<unsigned char> blocks = blocksOfTwo(t, local);
		int rockLeft = 0;
		for (int i : p.tiles)
			rockLeft += blocks[i];
		if (int(face.size()) >= kFaceLeast && rockLeft >= 6)
		{
			faces[q] = face;
			++faced;
		}
	}

	// Sea stacks: the smallest islands, whole.
	std::vector<std::vector<int>> stacks;
	{
		std::vector<unsigned char> land(size_t(n), 0);
		for (int i = 0; i < n; ++i)
			land[i] = map.vertexTerrainAt(t.remainderX(i), i / t.w) != WATER;
		const std::vector<int> island = connectedRegions(land, t.w, t.h, true, GridNeighbors::Eight);
		int islands = 0;
		for (int l : island)
			islands = std::max(islands, l + 1);
		std::vector<int> size(size_t(islands), 0);
		for (int l : island)
			if (l >= 0)
				++size[l];
		std::vector<int> slot(size_t(islands), -1);
		std::vector<std::vector<int>> members;
		for (int i = 0; i < n; ++i)
		{
			const int l = island[i];
			if (l < 0 || size[l] > kStackMost)
				continue;
			if (slot[l] < 0)
			{
				slot[l] = int(members.size());
				members.emplace_back();
			}
			members[slot[l]].push_back(i);
		}
		const std::vector<unsigned char> nearby = swarmSurroundings(t, context, kStackHome);
		for (const auto &vertices : members)
		{
			bool free = true;
			for (int v : vertices)
				for (int dy = -1; dy <= 0 && free; ++dy)
					for (int dx = -1; dx <= 0 && free; ++dx)
					{
						const int i = t.at(t.remainderX(v) + dx, v / t.w + dy);
						free = vacant(i) && !nearby[i];
					}
			// One draw per small island in scan order, whether or not it qualifies, so a deposit
			// on one island does not move which of the others become stacks.
			const bool chosen = int(context.bounded(kStackStream, 100)) < kStackShare;
			if (free && chosen)
				stacks.push_back(vertices);
		}
	}

	// Rock, then the parting check: a knot or stack whose rock (and stone) parts the ground round it is
	// dropped, and the check runs again.
	const std::vector<unsigned char> walk = walkableTiles(map);
	std::vector<unsigned char> aliveKnot(placed.size(), 1), aliveStack(stacks.size(), 1);
	std::vector<int> owner(size_t(n), -1);
	for (int round = 0; round <= int(placed.size() + stacks.size()); ++round)
	{
		std::fill(out.knotRock.begin(), out.knotRock.end(), 0);
		std::fill(out.quarry.begin(), out.quarry.end(), 0);
		std::fill(owner.begin(), owner.end(), -1);
		for (size_t q = 0; q < placed.size(); ++q)
		{
			if (!aliveKnot[q])
				continue;
			std::vector<unsigned char> local(size_t(n), 0);
			for (int i : placed[q].tiles)
				local[i] = 1;
			for (int i : faces[q])
				local[i] = 0;
			const std::vector<unsigned char> blocks = blocksOfTwo(t, local);
			for (int i : placed[q].tiles)
				if (blocks[i])
				{
					out.knotRock[i] = 1;
					owner[i] = int(q);
				}
			for (int i : faces[q])
			{
				out.quarry[i] = 1;
				owner[i] = int(q);
			}
		}
		out.rock = rockVertices(t, out.knotRock);
		for (size_t s = 0; s < stacks.size(); ++s)
			if (aliveStack[s])
				for (int v : stacks[s])
				{
					out.rock[v] = 1;
					// The four tiles round a stack vertex belong to the stack.
					for (int dy = -1; dy <= 0; ++dy)
						for (int dx = -1; dx <= 0; ++dx)
							owner[t.at(t.remainderX(v) + dx, v / t.w + dy)] = int(placed.size() + s);
				}
		std::vector<unsigned char> blocked = blockedByCorners(t, out.rock);
		for (int i = 0; i < n; ++i)
			blocked[i] = blocked[i] || out.quarry[i];
		std::vector<int> label;
		const int parting = partingGroup(t, walk, blocked, label);
		if (parting < 0)
			break;
		bool any = false;
		for (int i = 0; i < n; ++i)
			if (label[i] == parting && owner[i] >= 0)
			{
				const size_t id = size_t(owner[i]);
				if (id < placed.size())
					aliveKnot[id] = 0;
				else
					aliveStack[id - placed.size()] = 0;
				any = true;
			}
		++out.dropped;
		if (!any)
			break; // cannot happen: every blocked tile belongs to some knot or stack
	}
	for (size_t q = 0; q < placed.size(); ++q)
		if (aliveKnot[q])
		{
			++out.knots;
			out.faces += !faces[q].empty();
		}
	for (size_t s = 0; s < stacks.size(); ++s)
		if (aliveStack[s])
		{
			++out.stacks;
			out.stackVertices += int(stacks[s].size());
		}
	for (int i = 0; i < n; ++i)
		out.faceTiles += out.quarry[i];
	return out;
}

// How many vertices each layer took.
struct LayerCounts
{
	int deep = 0, mud = 0, rock = 0, lush = 0, dry = 0, paleBeach = 0, dunes = 0;
};

// The looks over the mechanic sketch, by climate (composeLooks keeps each inside its rule group).
TerrainSketch lookSketch(const TerrainSketch &mech, const Torus &t, const Climate &climate, GenerationContext &context,
						 LayerCounts &counts)
{
	const int n = t.size();
	std::vector<unsigned char> wet(size_t(n), 0), grass(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		wet[i] = mech[i] == WATER || mech[i] == DEEP_WATER;
		grass[i] = mech[i] == GRASS;
	}
	const std::vector<int> fromWet = stepsFrom(t, wet);
	const std::vector<int> depth = clearance(t, grass);
	// The widest a grass vertex's neighbourhood gets: a thin neck stays thin all along.
	std::vector<int> negated(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i)
		negated[i] = -depth[i];
	const std::vector<int> widest = windowMinimum(t, negated, 2);
	std::mt19937 &random = context.stream(kLookStream);
	const std::vector<int> fray = periodicNoise(t.w, t.h, 9, random);
	const std::vector<int> split = fractalNoise(t.w, t.h, 16, 3, random);
	TerrainSketch looks(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		generationCheckpoint();
		const int shift = (fray[i] - 32768) * 2 / 32768; // -2 to 1
		const bool second = split[i] > int(0.62 * 65536);
		switch (mech[i])
		{
		case GRASS:
			if (depth[i] >= climate.lush + shift)
			{
				looks[i] = LUSH_GRASS;
				++counts.lush;
			}
			else if (-widest[i] <= climate.sliver || (climate.dryRims && depth[i] == 1 && fray[i] > 30000))
			{
				looks[i] = DRY_GRASS;
				++counts.dry;
			}
			break;
		case SAND:
			if (fromWet[i] <= 2) // a beach is up to two vertices wide (layBeaches turns both sides)
			{
				looks[i] = second ? climate.beachSecond : climate.beach;
				counts.paleBeach += looks[i] == PALE_SAND;
			}
			else
			{
				looks[i] = second ? climate.duneSecond : climate.dune;
				++counts.dunes;
			}
			break;
		case DEEP_WATER:
			looks[i] = climate.deep;
			break;
		case MUDFLAT:
			looks[i] = climate.mud;
			break;
		case RIDGE_ROCK:
			looks[i] = climate.rock;
			break;
		default:
			break;
		}
	}
	return looks;
}

// The coast pass: rock, deep cores, mudflats and looks over the finished revision-8 map.
bool coastPass(GenerationWorld &game, GenerationContext &context, const ShatteredCoastOptions &o)
{
	GenerationMap &map = game.map;
	const Torus t(map);
	const int n = t.size();
	TerrainSketch mech(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i)
		mech[i] = map.vertexTerrainAt(t.remainderX(i), i / t.w);
	LayerCounts counts;
	// Every swarm's corner (revision 8 records no start positions in the map report; these are the
	// same in both revisions for the same seed).
	for (int team = 0; team < context.request.nbTeams; ++team)
	{
		context.telemetry.measure("shattered-coast.starts.swarm-x", context.bootX[team], team);
		context.telemetry.measure("shattered-coast.starts.swarm-y", context.bootY[team], team);
	}

	// The rock first: it reads the revision-8 terrain and deposits.
	const Outcrops rock = planOutcrops(map, t, context, o);
	for (int i = 0; i < n; ++i)
		if (rock.rock[i])
		{
			mech[i] = RIDGE_ROCK;
			++counts.rock;
		}

	// Corners of tiles holding a deposit (algae, in the water), a building or a unit keep their terrain.
	std::vector<unsigned char> held(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		const int x = t.remainderX(i), y = i / t.w;
		if (map.isResource(x, y) || map.getBuilding(x, y) != NOGBID || map.getGroundUnit(x, y) != NOGUID)
			for (int dy = 0; dy <= 1; ++dy)
				for (int dx = 0; dx <= 1; ++dx)
					held[t.at(x + dx, y + dy)] = 1;
	}

	std::vector<unsigned char> water(size_t(n), 0);
	for (int i = 0; i < n; ++i)
		water[i] = mech[i] == WATER;
	if (o.deepWater)
	{
		std::vector<unsigned char> core = deepCore(t, water, kDeepSteps);
		for (int i = 0; i < n; ++i)
			core[i] = core[i] && !held[i];
		const std::vector<unsigned char> grown = dilateRound(t, erode(t, core, kDeepOpen), kDeepOpen);
		for (int i = 0; i < n; ++i)
			core[i] = core[i] && grown[i];
		core = dropSmallRegions(t, core, kDeepLeast);
		for (int i = 0; i < n; ++i)
			if (core[i])
			{
				mech[i] = DEEP_WATER;
				++counts.deep;
			}
	}

	if (o.mudflats)
	{
		const std::vector<int> body = connectedRegions(water, t.w, t.h, true, GridNeighbors::Eight);
		int bodies = 0;
		for (int l : body)
			bodies = std::max(bodies, l + 1);
		std::vector<int> size(size_t(bodies), 0);
		for (int l : body)
			if (l >= 0)
				++size[l];
		const std::vector<unsigned char> homes = swarmSurroundings(t, context, kMudHome);
		const std::vector<int> mud = periodicNoise(t.w, t.h, 7, context.stream(kMudStream));
		for (int i = 0; i < n; ++i)
		{
			generationCheckpoint();
			if (mech[i] != SAND || homes[i] || held[i] || mud[i] < kMudCut)
				continue;
			const int x = t.remainderX(i), y = i / t.w;
			bool sheltered = false, shore = false;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int j = t.at(x + dx, y + dy);
					if (body[j] < 0)
						continue;
					shore = true;
					sheltered = sheltered || size[body[j]] <= kMudBody;
				}
			if (shore && sheltered)
			{
				mech[i] = MUDFLAT;
				++counts.mud;
			}
		}
	}

	const int climateIndex =
		o.climate > 0 ? std::clamp(o.climate, 1, 3) - 1 : int(context.bounded(kClimateStream, 3));
	const Climate &climate = kClimates[climateIndex];
	const TerrainSketch finished = composeLooks(mech, lookSketch(mech, t, climate, context, counts));
	writeVertices(map, finished);

	// The quarry faces' stone: one permanent deposit on every face tile.
	int quarryStone = 0;
	for (int i = 0; i < n; ++i)
		if (rock.quarry[i] && map.isResourceAllowed(t.remainderX(i), i / t.w, STONE))
		{
			map.setResourceByIndex(t.remainderX(i), i / t.w, STONE, 1);
			++quarryStone;
		}
	// Every face tile must hold its stone: a face is vacant pure grass by construction, so one that cannot
	// is a defect, refused rather than drawn as a face with nothing in it.
	for (int i = 0; i < n; ++i)
		if (rock.quarry[i] && (!map.isResource(t.remainderX(i), i / t.w) ||
							   map.getResource(t.remainderX(i), i / t.w).type != STONE))
		{
			context.detail = "A quarry face at (" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) +
							 ") holds no stone.";
			return false;
		}

	context.telemetry.choice("shattered-coast.climate", climate.name);
	context.telemetry.choice("shattered-coast.deep-water", o.deepWater ? "on" : "off");
	context.telemetry.measure("shattered-coast.deep.vertices", counts.deep);
	context.telemetry.measure("shattered-coast.mudflat.vertices", counts.mud);
	context.telemetry.measure("shattered-coast.rock.vertices", counts.rock);
	context.telemetry.measure("shattered-coast.looks.lush-vertices", counts.lush);
	context.telemetry.measure("shattered-coast.looks.dry-vertices", counts.dry);
	context.telemetry.measure("shattered-coast.looks.pale-beach-vertices", counts.paleBeach);
	context.telemetry.measure("shattered-coast.looks.dune-vertices", counts.dunes);
	context.telemetry.measure("shattered-coast.knots.wanted", rock.wanted);
	context.telemetry.measure("shattered-coast.knots.placed", rock.knots);
	context.telemetry.measure("shattered-coast.knots.faces", rock.faces);
	context.telemetry.measure("shattered-coast.knots.dropped", rock.dropped);
	context.telemetry.measure("shattered-coast.stacks.placed", rock.stacks);
	context.telemetry.measure("shattered-coast.stacks.vertices", rock.stackVertices);
	context.telemetry.measure("shattered-coast.quarry.tiles", rock.faceTiles);
	context.telemetry.measure("shattered-coast.quarry.stone", quarryStone);
	if (o.outcrops && rock.knots < rock.wanted)
		context.telemetry.fallback("shattered-coast.knots.shortfall", "Not enough wide vacant grass away from the homes");
	if (context.telemetry.enabled() && rock.faces > 0)
	{
		// How far each colony walks to the nearest quarry face (-1 when none can be reached): the
		// fairness of the knots' stone, measured on the finished map.
		const std::vector<unsigned char> walkable = walkableTiles(map);
		int least = INT_MAX, most = -1;
		for (int team = 0; team < context.request.nbTeams; ++team)
		{
			std::vector<unsigned char> swarm(size_t(n), 0);
			for (int dy = 0; dy < 4; ++dy)
				for (int dx = 0; dx < 4; ++dx)
					swarm[t.at(context.bootX[team] + dx, context.bootY[team] + dy)] = 1;
			const std::vector<int> steps = stepsFrom(t, swarm, walkable);
			int nearest = INT_MAX;
			for (int i = 0; i < n; ++i)
			{
				if (!rock.quarry[i])
					continue;
				for (const auto &step : kCardinalSteps)
				{
					const int s = steps[t.at(t.remainderX(i) + step[0], i / t.w + step[1])];
					if (s >= 0)
						nearest = std::min(nearest, s + 1);
				}
			}
			const int walk = nearest == INT_MAX ? -1 : nearest;
			context.telemetry.measure("shattered-coast.quarry.nearest-walk", walk, team);
			least = std::min(least, walk);
			most = std::max(most, walk);
		}
		if (least >= 0)
			context.telemetry.measure("shattered-coast.quarry.nearest-walk-spread", most - least);
	}
	return true;
}
} // namespace

static bool generate(GenerationWorld &game, GenerationContext &context)
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
	// Whatever the amounts, a colony can end up walled into a pocket by the deposits laid
	// around it: nothing here budgets its room. Opening it up costs a colony that already has
	// its room nothing (openStartsBuriedByResources leaves such a colony untouched).
	openStartsBuriedByResources(game, context);
	context.stage = "expansion resources";
	expansionResources(game, context, options);
	// 2.0: everything above is revision 8, draw for draw; the coast pass repaints the ground nobody
	// stands on.
	context.stage = "coast";
	return coastPass(game, context, options);
}

// The terrain is texture rather than design, and the colony search takes the widest grass it
// finds, so this is the one promise worth making: a colony that can reach its crops and has
// somewhere to put its first buildings. A seed that leaves one walled into a cove is rerolled.
// 2.0 adds the pass's own promises, read from the finished terrain by rule group: every deep vertex
// keeps kDeepSteps - 1 steps of water round it (the shallows), no rock stands near a colony, and no
// group of rock parts the walkable ground round it.
static std::string validateWorld(const GenerationWorld &game, const GenerationContext &context)
{
	const GenerationMap &map = game.map;
	const int teams = context.request.nbTeams;
	if (std::string floor = startingFloorFailure(map, teams); !floor.empty())
		return floor;
	const Torus t(map);
	const int n = t.size();
	const auto at = [&](int i) { return "(" + std::to_string(t.remainderX(i)) + ", " + std::to_string(i / t.w) + ")"; };
	std::vector<unsigned char> land(size_t(n), 0), ridges(size_t(n), 0);
	for (int i = 0; i < n; ++i)
	{
		const TerrainGroup group = terrainGroup(map.vertexTerrainAt(t.remainderX(i), i / t.w));
		land[i] = group != TerrainGroup::Water && group != TerrainGroup::DeepWater;
		ridges[i] = group == TerrainGroup::Ridges;
	}
	const std::vector<int> fromLand = stepsFrom(t, land);
	for (int i = 0; i < n; ++i)
		if (terrainGroup(map.vertexTerrainAt(t.remainderX(i), i / t.w)) == TerrainGroup::DeepWater &&
			fromLand[i] < kDeepSteps - 1)
			return "Deep water at " + at(i) + " has no shallows between it and the shore.";
	const auto workers = unitTilesByTeam(map, teams);
	std::vector<unsigned char> homes(size_t(n), 0);
	for (const auto &tiles : workers)
		for (int i : tiles)
			homes[i] = 1;
	homes = dilate(t, homes, kRockHomeCheck);
	std::vector<unsigned char> walkable(size_t(n), 0), rock(size_t(n), 0);
	for (int y = 0; y < t.h; ++y)
		for (int x = 0; x < t.w; ++x)
		{
			const int i = t.at(x, y);
			const TerrainProperties &rules = map.terrainPropertiesAt(x, y);
			walkable[i] = rules.walkable;
			const bool corner =
				ridges[i] || ridges[t.at(x + 1, y)] || ridges[t.at(x, y + 1)] || ridges[t.at(x + 1, y + 1)];
			if (corner && homes[i])
				return "Rock at " + at(i) + " stands too near a colony.";
			rock[i] = corner && !rules.walkable && !rules.swimmable;
		}
	std::vector<int> label;
	if (const int parting = partingGroup(t, walkable, rock, label); parting >= 0)
		for (int i = 0; i < n; ++i)
			if (label[i] == parting)
				return "Rock at " + at(i) + " cuts ground off.";
	return "";
}

GeneratorDefinition shatteredCoastDefinition()
{
	return {
		"shattered-coast",
		7,
		"Old random",
		9,
		false,
		// The three terrain weights are relative (40/4/60 asks for 38% water, 4% sand, 58% grass
		// before sand control adds the beaches); smoothing is the number of passes, which sets the
		// scale of the blobs from speckle (1) to broad coasts (8).
		{GeneratorControl{"water", "Water weight", 0, 100, 1, 40, ControlGroup::Terrain, false,
						  true}
			 .withSearchRange(25, 50),
		 GeneratorControl{"sand", "Sand weight", 0, 100, 1, 4, ControlGroup::Terrain, false, true}
			 .withSearchRange(1, 6),
		 GeneratorControl{"grass", "Grass weight", 0, 100, 1, 60, ControlGroup::Terrain, false,
						  true}
			 .withSearchRange(55, 85),
		 GeneratorControl{"smoothing", "Smoothing", 1, 8, 1, 3, ControlGroup::Terrain, false}
			 .withSearchRange(3, 6),
		 // Off, colonies start in the shattered terrain rather than in a cleared meadow.
		 GeneratorControl::toggle("colony-meadows", "Colony meadows", true, ControlGroup::Terrain)
			 .withSearchValues({1}),
		 // On, the bigger bays and lakes have a core of deep water: slower to swim, no algae, a
		 // band of shallows along every shore. Routes on foot are the same either way.
		 GeneratorControl::toggle("deep-water", "Deep water", true, ControlGroup::Terrain)
			 .withSearchValues({0, 1}),
		 // On, the beaches of small ponds and coves are partly mudflat: slow, unbuildable, and
		 // unlike sand they let crops grow up to the shore. Sand stays round the homes.
		 GeneratorControl::toggle("mudflats", "Mudflats", true, ControlGroup::Terrain)
			 .withSearchValues({0, 1}),
		 // On, the smallest islands are sea stacks and knots of rock stand in the wider grass,
		 // away from the homes, some with a quarry face of stone (how many follows the stone
		 // amount). No rock ever cuts ground off.
		 GeneratorControl::toggle("outcrops", "Rock outcrops", true, ControlGroup::Layout)
			 .withSearchValues({0, 1}),
		 // Looks only: Temperate (pale sand, lush meadows), Steppe (dry grass, dunes, hoodoos)
		 // or Tropical (lush grass, dark water, mud). All play alike.
		 GeneratorControl::choice("climate", "Climate", {"Random", "Temperate", "Steppe", "Tropical"}, 0,
								  ControlGroup::Terrain)
			 .withSearchValues({1, 2, 3}),
		 // Home deposit area and the number of additional expansion clumps; stone also sets how
		 // many rock knots carry a quarry face.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount")},
		generate,
		true,
		nullptr,
		validateWorld,
		{"terrain:natural", "feature:ocean", "style:wide-open"}};
}
