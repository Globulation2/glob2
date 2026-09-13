// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "RuggedArchipelagoGenerator.h"
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
#include <cmath>
using namespace MapGeneration;

// Old islands (id "rugged-archipelago", legacy id 8): one island per colony in open sea.
//
// HISTORY. nuage added this "Islands map generator system" in August 2002, days after the first
// random generator (Old random), and reworked it twice that week for "more fair, nicer, real
// beach". It was the game's islands map until Leo Wandersleb's height-field Islands took the name
// in 2006, when giszmo kept it as "old islands". On this branch its 274-line terrain function was
// split into the growth passes below without changing its output, dead stores of legacy amounts
// went, and it gained resource amounts and an Extra starting deposit switch.
//
// WHAT THE MAP IS. Every colony starts alone on its own roughly round island, the same kit on every
// island in the same arrangement (wood north, wheat west, stone south, a reinforcing field to the
// south-east, algae offshore to the east), with open water between islands. It is the purest "build
// up, then swim" map: nobody can reach anybody on foot, so the early game is pure economy and the
// first contact comes when someone builds a swimming pool.
//
// GAME RULES IT LEANS ON (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md):
// - Water blocks walking until a colony can swim, so separate islands delay all contact.
// - Grass may not touch water: Map::controlSand rings every island in sand, and spreadBeaches can
//   widen that ring. Sand is walkable but unbuildable, so wide beaches shrink a colony's room.
// - Wheat and wood regrow only near water; on an island of this size every field is near the sea.
// - Algae needs water with sand in reach, which a beach right beside it gives.
//
// Fairness is statistical: islands grow by random accretion, so they differ in shape, and a small
// or irregular island can squeeze its deposits; the shared backstops at the end of generate() top
// up short colonies, and the lobby keeps the best-scoring seed. Everything draws from the "terrain"
// stream, so a seed reproduces the map.

// The eight directions the growth passes below draw from: a pair of opposite neighbours at one
// or two tiles, in the order the draw has always indexed them.
static const int kGrowthDirections[8][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1},
											{2, 0}, {0, 2}, {2, 2}, {2, -2}};

// Seed one grass patch per colony, each at least a colony's share of the map from the others,
// halving that spacing if a placement keeps failing. Returns how many growth passes the islands
// get, from the map size and the island size control.
static int plantBootstraps(Map &map, GenerationContext &context,
						   const RuggedArchipelagoOptions &options, std::mt19937 &rng)
{
	const int w = map.getW(), h = map.getH();
	int *bootX = context.bootX.data();
	int *bootY = context.bootY.data();
	int nbIslands = context.request.nbTeams;
	// The number of growth passes, which is roughly each island's radius: the mean side of the map
	// times island_size/400, divided by the square root of the colony count so the islands' total
	// area stays about the same however many colonies share the map. With the default 65 on a
	// 256x256 map that is 41 passes for 4 colonies and 29 for 8; never fewer than 8, the least that
	// grows an island a 4x4 swarm and its kit fit on.
	//
	// The control's narrow range (50 to 70) is what keeps this an islands map. Rendered on 256x256
	// with 4 colonies and 512x512 with 8: at 50 the islands are small and well apart; at 65 and 70
	// neighbouring islands on a crowded map start to touch and merge, and much above 70 they merge
	// into continents and the map stops being about swimming.
	int islandsSize = (int)(((w + h) * options.island_size) / (400.0 * sqrt((double)nbIslands)));
	if (islandsSize < 8)
		islandsSize = 8;
	// Each seed keeps a colony's share of the map from every other, as a squared distance: seeds
	// sit about the side of a square of w*h/colonies apart, so islands grown to similar radii
	// rarely overlap.
	int minDistSquare = (w * h) / nbIslands;

	int c = 0;
	for (int i = 0; i < nbIslands; i++)
	{
		int x = rng() % w;
		int y = rng() % h;
		bool failed = false;
		int j;
		for (j = 0; j < i; j++)
			if (map.warpDistSquare(x, y, bootX[j], bootY[j]) < minDistSquare)
			{
				failed = true;
				break;
			}
		if (failed)
		{
			// Rejection sampling: draw again. After 65536 failed draws in a row the map is too
			// crowded for that spacing, so the spacing is halved and the count restarts.
			i--;
			if (c++ > 65536)
			{
				minDistSquare = minDistSquare >> 1;
				// I think that you need to do this only once, in worst case.
				// With a few luck you doesn't need to.
				c = 0;
			}
		}
		else
		{
			bootX[i] = x;
			bootY[i] = y;
			// The island's seed: a 7x6 block of grass corners holding the 4x4 swarm and its
			// workers.
			for (int dx = -1; dx < 6; dx++)
				for (int dy = 0; dy < 6; dy++)
					map.setUMTerrain(x + dx, y + dy, GRASS);
		}
	}
	return islandsSize;
}

// Grow the islands: for so many passes, every non-grass tile on a checkerboard draws one of
// sixteen values and, for the eight that name a direction, turns to grass when either
// neighbour that way already is.
//
// This is random accretion, like frost growing on glass: each pass a water tile tests one direction
// half the time (draws 0 to 7 of 16) and joins the island if a grass tile lies that way, so the
// coast advances by up to a tile or two a pass, unevenly, and the outline comes out ragged
// ("rugged"); on a 256x256 map with 4 colonies the 41 passes grow islands about 75 tiles across.
// Tiles are visited on
// the two halves of a checkerboard in turn so a tile that has just turned grass does not feed its
// neighbour in the same sweep, which would let growth race along rows in scan order.
static void expandIslands(Map &map, std::mt19937 &rng, int passes)
{
	const int w = map.getW(), h = map.getH();
	for (int s = 0; s < passes; s++)
		for (int oddEven = 0; oddEven < 2; oddEven++)
			for (int y = oddEven; y < h; y += 2)
				for (int x = oddEven; x < w; x += 2)
				{
					if (map.getUMTerrain(x, y) == GRASS)
						continue;
					const unsigned draw = rng() & 15;
					if (draw >= 8)
						continue;
					const int dx = kGrowthDirections[draw][0], dy = kGrowthDirections[draw][1];
					if (map.getUMTerrain(x + dx, y + dy) == GRASS ||
						map.getUMTerrain(x - dx, y - dy) == GRASS)
						map.setUMTerrain(x, y, GRASS);
				}
}

// Close single-tile gaps: a tile between two grass tiles, in any of the four directions, becomes
// grass, twice over. Accretion leaves pinholes and one-tile inlets; left in, each would become a
// sand pit (grass may not touch water) that wastes building room in the middle of an island.
static void smoothGrass(Map &map)
{
	const int w = map.getW(), h = map.getH();
	for (int s = 0; s < 2; s++)
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				for (int d = 0; d < 4; d++)
				{
					const int dx = kGrowthDirections[d][0], dy = kGrowthDirections[d][1];
					if (map.getUMTerrain(x + dx, y + dy) == GRASS &&
						map.getUMTerrain(x - dx, y - dy) == GRASS)
					{
						map.setUMTerrain(x, y, GRASS);
						break;
					}
				}
}

// Widen the beaches: for so many passes, every tile on a four-by-four lattice draws one of
// eight values; the first four turn it to sand between sand and water along that direction,
// the last four between sand and sand.
//
// Sand beside sand and water grows the beach outwards into the sea and inwards over grass; sand
// between two sands fills gaps in it. Visiting one tile in sixteen per sweep (sixteen offsets in
// turn) keeps growth from racing in scan order. `beach-size` (0 to 4, default 1) is the pass count:
// every pass costs grass (room to build) and adds walkable sand round the island.
static void spreadBeaches(Map &map, std::mt19937 &rng, int passes)
{
	const int w = map.getW(), h = map.getH();
	for (int s = 0; s < passes; s++)
		for (int dy = 0; dy < 4; dy++)
			for (int dx = 0; dx < 4; dx++)
				for (int y = dy; y < h; y += 4)
					for (int x = dx; x < w; x += 4)
					{
						const unsigned draw = rng() & 7;
						const int ddx = kGrowthDirections[draw & 3][0],
								  ddy = kGrowthDirections[draw & 3][1];
						const int a = map.getUMTerrain(x + ddx, y + ddy),
								  b = map.getUMTerrain(x - ddx, y - ddy);
						const bool shore =
							draw < 4 ? (a == SAND && b == WATER) || (a == WATER && b == SAND)
									 : a == SAND && b == SAND;
						if (shore)
							map.setUMTerrain(x, y, SAND);
					}
}

static bool terrain(Game &game, GenerationContext &context, const RuggedArchipelagoOptions &options)
{
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	// Every draw below names the "terrain" stream; one lookup serves all of them.
	std::mt19937 &rng = context.stream("terrain");

	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			map.setUMTerrain(x, y, WATER);
	const int passes = plantBootstraps(map, context, options, rng);
	expandIslands(map, rng, passes);
	smoothGrass(map);
	map.controlSand();
	spreadBeaches(map, rng, options.beach_size);
	map.rebuildTerrain();
	return true;
}

static void resources(Game &game, GenerationContext &context,
					  const RuggedArchipelagoOptions &options)
{
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();

	int *bootX = context.bootX.data();
	int *bootY = context.bootY.data();

	int islandsSize =
		(int)(((w + h) * options.island_size) / (400.0 * sqrt((double)context.request.nbTeams)));
	if (islandsSize < 8)
		islandsSize = 8;
	// let's add resources... Each deposit is a square sized to the island; the amount controls
	// scale its area around the same centre.
	//
	// Every colony gets the same kit laid out the same way round its swarm: each deposit walks from
	// the swarm towards the coast in its own direction, counts the grass (d), and becomes a square
	// `amount` tiles a side centred on that run (`p` is the centre's distance). The square is the
	// run less a margin of islandsSize/4, the number of rounds the deposits were meant to spread
	// for, so a grown field would still stop short of the beach, and less a small constant per
	// resource (wood 2, wheat 0, the reinforcing field 3) that shrinks wood and the extra field a
	// little against wheat. Stone is one tile. Map::setResource draws each tile's starting amount
	// from the gameplay RNG, so the order of these calls is part of the map.
	int smoothResources = islandsSize / 4;
	for (int s = 0; s < context.request.nbTeams; s++)
	{
		int d, p, amount;
		int smallestAmount;
		int smallestResource;

		// WOOD
		for (d = 0; d < islandsSize; d++)
			if (!map.isGrass(bootX[s], bootY[s] - d))
				break;
		amount = d - smoothResources - 2;
		if (amount < 1)
			amount = 1;
		p = d - 1 - amount / 2;
		if (amount > 0)
			setScaledResource(map, bootX[s], bootY[s] - p, WOOD, amount, options.wood);
		smallestAmount = amount;
		smallestResource = WOOD;

		// WHEAT
		for (d = 0; d < islandsSize; d++)
			if (!map.isGrass(bootX[s] - d, bootY[s]))
				break;
		amount = d - smoothResources - 0;
		if (amount < 1)
			amount = 1;
		p = d - 1 - amount / 2;
		if (amount > 0)
			setScaledResource(map, bootX[s] - p, bootY[s], CORN, amount, options.wheat);
		if (amount < smallestAmount)
		{
			smallestAmount = amount;
			smallestResource = CORN;
		}

		// STONE
		// The walk south measures d, but the stone is placed at the wheat's centre distance `p`,
		// which is not recomputed here: an original slip. The stone lands p tiles south, where p
		// is how far west the wheat field sits, not where the southern grass ends. It is one tile
		// and almost always on the island, so it is kept rather than moving every colony's quarry.
		for (d = 0; d < islandsSize; d++)
			if (!map.isGrass(bootX[s], bootY[s] + d))
				break;
		setScaledResource(map, bootX[s], bootY[s] + p, STONE, 1, options.stone);

		// We add the resource with the smallest amount, unless that extra deposit is switched off:
		for (d = 0; d < islandsSize; d++)
			if (!map.isGrass(bootX[s] + d, bootY[s] + d))
				break;
		amount = d - smoothResources - 3;
		if (amount < 1)
			amount = 1;
		p = d - 1 - amount / 2;
		if (amount > 0 && options.extra_deposit)
			setScaledResource(map, bootX[s] + p, bootY[s] + p, smallestResource, amount,
							  smallestResource == CORN ? options.wheat : options.wood);

		// ALGAE
		// East to the first water, then out past it by the spreading margin, so the algae sits a
		// little offshore of the beach, where it has water all round and sand in reach to regrow.
		for (d = 0; d < 2 * islandsSize; d++)
			if (map.isWater(bootX[s] + d, bootY[s]))
				break;
		amount = smoothResources;
		p = d + smoothResources - 1 + amount / 2;
		if (amount > 0)
			setScaledResource(map, bootX[s] + p, bootY[s], ALGA, amount, options.algae);
	}

	// Let's smooth resources... Frays the squares into fields: each of the smoothResources * 2
	// rounds grows small deposit tiles and sprouts large ones onto a free neighbour. This is why
	// each deposit above is smoothResources tiles smaller than its run of grass: the rounds grow it
	// back out. From 2003 until revision 2 Map::smoothResources read the old resource encoding and
	// did nothing, leaving the deposits as squares; working, it adds about a quarter more wheat and
	// wood.
	map.smoothResources(smoothResources * 2);
}

static bool generate(Game &game, GenerationContext &context)
{
	const RuggedArchipelagoOptions options(context.request);
	if (!terrain(game, context, options))
		return false;
	context.stage = "starts";
	if (!placeArchipelagoStarts(game, context, options.island_size))
		return false;
	context.stage = "resources";
	resources(game, context, options);
	// Each island's own footprint bounds the compass search above (it stops at the first
	// non-grass tile), so a small or irregular island can still leave a team short on wheat or
	// wood even though the search ran in every direction. Top up anyone still missing either
	// within comfortable range; islands that were already generous are left untouched.
	guaranteeStartingResources(game, context, 24, 32);
	// A deposit grown well past its default size can wall a colony into its own clearing with
	// nowhere left to build on an island this small.
	reopenCrampedStarts(game, context, {options.wheat, options.wood, options.stone, options.algae});
	return true;
}

GeneratorDefinition ruggedArchipelagoDefinition()
{
	return {
		"rugged-archipelago",
		8,
		"Old islands",
		2,
		false,
		// Island size scales the growth passes (see plantBootstraps for why its range is narrow);
		// beach size is the number of beach-widening passes.
		{{"island-size", "Island size", 50, 70, 1, 65, ControlGroup::Terrain, false},
		 {"beach-size", "Beach size", 0, 4, 1, 1, ControlGroup::Terrain, false},
		 // Off, an island gets no fourth deposit of whichever of wheat or wood came out smaller.
		 GeneratorControl::toggle("extra-deposit", "Extra starting deposit", true,
								  ControlGroup::Resources),
		 // The area of each island's own wheat, wood, stone and algae deposits.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount")},
		generate};
}
