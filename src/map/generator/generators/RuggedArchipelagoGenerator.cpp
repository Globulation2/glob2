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

static bool terrain(Game &game, GenerationContext &context, const RuggedArchipelagoOptions &options)
{
	Map &map = game.map;
	const int w = map.getW(), h = map.getH();
	// context.stream() looks up a named std::mt19937 by string key on every call; every draw in
	// this function names the same "terrain" stream, so look it up once and reuse the reference
	// through the loops below instead of repeating the lookup per tile per draw.
	std::mt19937 &rng = context.stream("terrain");

	// First, fill with water:
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			map.setUMTerrain(x, y, WATER);

	// Two, plants "bootstraps"
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

	// Three, expands islands
	for (int s = 0; s < islandsSize; s++)
	{
		for (int oddEven = 0; oddEven < 2; oddEven++)
		{
			for (int y = oddEven; y < h; y += 2)
			{
				for (int x = oddEven; x < w; x += 2)
				{
					TerrainType umt = map.getUMTerrain(x, y);
					if (umt == GRASS)
						continue;

					int a, b;
					switch (rng() & 15)
					{
					case 0:
						a = map.getUMTerrain(x + 1, y);
						b = map.getUMTerrain(x - 1, y);
						if ((a == GRASS) || (b == GRASS))
						{
							map.setUMTerrain(x, y, GRASS);
						}
						break;
					case 1:
						a = map.getUMTerrain(x, y - 1);
						b = map.getUMTerrain(x, y + 1);
						if ((a == GRASS) || (b == GRASS))
						{
							map.setUMTerrain(x, y, GRASS);
						}
						break;
					case 2:
						a = map.getUMTerrain(x + 1, y + 1);
						b = map.getUMTerrain(x - 1, y - 1);
						if ((a == GRASS) || (b == GRASS))
						{
							map.setUMTerrain(x, y, GRASS);
						}
						break;
					case 3:
						a = map.getUMTerrain(x + 1, y - 1);
						b = map.getUMTerrain(x - 1, y + 1);
						if ((a == GRASS) || (b == GRASS))
						{
							map.setUMTerrain(x, y, GRASS);
						}
						break;
					case 4:
						a = map.getUMTerrain(x + 2, y);
						b = map.getUMTerrain(x - 2, y);
						if ((a == GRASS) || (b == GRASS))
						{
							map.setUMTerrain(x, y, GRASS);
						}
						break;
					case 5:
						a = map.getUMTerrain(x, y - 2);
						b = map.getUMTerrain(x, y + 2);
						if ((a == GRASS) || (b == GRASS))
						{
							map.setUMTerrain(x, y, GRASS);
						}
						break;
					case 6:
						a = map.getUMTerrain(x + 2, y + 2);
						b = map.getUMTerrain(x - 2, y - 2);
						if ((a == GRASS) || (b == GRASS))
						{
							map.setUMTerrain(x, y, GRASS);
						}
						break;
					case 7:
						a = map.getUMTerrain(x + 2, y - 2);
						b = map.getUMTerrain(x - 2, y + 2);
						if ((a == GRASS) || (b == GRASS))
						{
							map.setUMTerrain(x, y, GRASS);
						}
						break;
					default:
						break;
					}
				}
			}
		}
	}

	// Four, avoid too much sand. Let's smooth
	for (int s = 0; s < 2; s++)
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				int a, b;
				a = map.getUMTerrain(x + 1, y);
				b = map.getUMTerrain(x - 1, y);
				if ((a == GRASS) && (b == GRASS))
				{
					map.setUMTerrain(x, y, GRASS);
					continue;
				}
				a = map.getUMTerrain(x, y - 1);
				b = map.getUMTerrain(x, y + 1);
				if ((a == GRASS) && (b == GRASS))
				{
					map.setUMTerrain(x, y, GRASS);
					continue;
				}
				a = map.getUMTerrain(x + 1, y + 1);
				b = map.getUMTerrain(x - 1, y - 1);
				if ((a == GRASS) && (b == GRASS))
				{
					map.setUMTerrain(x, y, GRASS);
					continue;
				}
				a = map.getUMTerrain(x + 1, y - 1);
				b = map.getUMTerrain(x - 1, y + 1);
				if ((a == GRASS) && (b == GRASS))
				{
					map.setUMTerrain(x, y, GRASS);
					continue;
				}
			}

	map.controlSand();

	// Five, add some sand
	for (int s = 0; s < options.beach_size; s++)
		for (int dy = 0; dy < 4; dy++)
			for (int dx = 0; dx < 4; dx++)
				for (int y = dy; y < h; y += 4)
					for (int x = dx; x < w; x += 4)
					{
						int a, b;
						switch (rng() & 7)
						{
						case 0:
							a = map.getUMTerrain(x + 1, y);
							b = map.getUMTerrain(x - 1, y);
							if (((a == SAND) && (b == WATER)) || ((a == WATER) && (b == SAND)))
							{
								map.setUMTerrain(x, y, SAND);
								continue;
							}
							break;
						case 1:
							a = map.getUMTerrain(x, y - 1);
							b = map.getUMTerrain(x, y + 1);
							if (((a == SAND) && (b == WATER)) || ((a == WATER) && (b == SAND)))
							{
								map.setUMTerrain(x, y, SAND);
								continue;
							}
							break;
						case 2:
							a = map.getUMTerrain(x + 1, y + 1);
							b = map.getUMTerrain(x - 1, y - 1);
							if (((a == SAND) && (b == WATER)) || ((a == WATER) && (b == SAND)))
							{
								map.setUMTerrain(x, y, SAND);
								continue;
							}
							break;
						case 3:
							a = map.getUMTerrain(x + 1, y - 1);
							b = map.getUMTerrain(x - 1, y + 1);
							if (((a == SAND) && (b == WATER)) || ((a == WATER) && (b == SAND)))
							{
								map.setUMTerrain(x, y, SAND);
								continue;
							}
							break;

						case 4:
							a = map.getUMTerrain(x + 1, y);
							b = map.getUMTerrain(x - 1, y);
							if ((a == SAND) && (b == SAND))
							{
								map.setUMTerrain(x, y, SAND);
								continue;
							}
							break;
						case 5:
							a = map.getUMTerrain(x, y - 1);
							b = map.getUMTerrain(x, y + 1);
							if ((a == SAND) && (b == SAND))
							{
								map.setUMTerrain(x, y, SAND);
								continue;
							}
							break;
						case 6:
							a = map.getUMTerrain(x + 1, y + 1);
							b = map.getUMTerrain(x - 1, y - 1);
							if ((a == SAND) && (b == SAND))
							{
								map.setUMTerrain(x, y, SAND);
								continue;
							}
							break;
						case 7:
							a = map.getUMTerrain(x + 1, y - 1);
							b = map.getUMTerrain(x - 1, y + 1);
							if ((a == SAND) && (b == SAND))
							{
								map.setUMTerrain(x, y, SAND);
								continue;
							}
							break;
						}
					}

	// map.controlSand();
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
	if (options.wheat != 100 || options.wood != 100 || options.stone != 100 ||
		options.algae != 100)
	{
		openCrampedStarts(game, context);
		guaranteeStartingResources(game, context, 24, 32);
	}
	return true;
}

GeneratorDefinition ruggedArchipelagoDefinition()
{
	return {"rugged-archipelago",
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
