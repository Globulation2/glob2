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
#include <cmath>
using namespace MapGeneration;
#include "RuggedArchipelagoGenerator.h"

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
	int islandsSize = (int)(((w + h) * options.island_size) / (400.0 * sqrt((double)nbIslands)));
	if (islandsSize < 8)
		islandsSize = 8;
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

// Close single-tile gaps: a tile between two grass tiles, in any of the four directions,
// becomes grass, twice over.
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
		amount = context.request.resourceAmounts[WOOD];
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
		amount = context.request.resourceAmounts[CORN];
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
		for (d = 0; d < islandsSize; d++)
			if (!map.isGrass(bootX[s], bootY[s] + d))
				break;
		setScaledResource(map, bootX[s], bootY[s] + p, STONE, 1, options.stone);

		// We add the resource with the smallest amount, unless that extra deposit is switched off:
		for (d = 0; d < islandsSize; d++)
			if (!map.isGrass(bootX[s] + d, bootY[s] + d))
				break;
		amount = context.request.resourceAmounts[smallestResource];
		amount = d - smoothResources - 3;
		if (amount < 1)
			amount = 1;
		p = d - 1 - amount / 2;
		if (amount > 0 && options.extra_deposit)
			setScaledResource(map, bootX[s] + p, bootY[s] + p, smallestResource, amount,
							  smallestResource == CORN ? options.wheat : options.wood);

		// ALGAE
		for (d = 0; d < 2 * islandsSize; d++)
			if (map.isWater(bootX[s] + d, bootY[s]))
				break;
		amount = context.request.resourceAmounts[ALGA];
		amount = smoothResources;
		p = d + smoothResources - 1 + amount / 2;
		if (amount > 0)
			setScaledResource(map, bootX[s] + p, bootY[s], ALGA, amount, options.algae);
	}

	// Let's smooth resources...
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
	// nowhere left to build on an island this small, which the guarantee above doesn't address: a
	// colony buried in wood has wood at its feet. At any amount other than the default, open such a
	// colony back up and guarantee the crops once more. At the defaults none of this runs.
	if (options.wheat != 100 || options.wood != 100 || options.stone != 100 || options.algae != 100)
	{
		openCrampedStarts(game, context);
		guaranteeStartingResources(game, context, 24, 32);
	}
	return true;
}

GeneratorDefinition ruggedArchipelagoDefinition()
{
	return {
		"rugged-archipelago",
		8,
		"Old islands",
		1,
		false,
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
