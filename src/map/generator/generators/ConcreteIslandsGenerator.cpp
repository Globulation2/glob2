// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "ConcreteIslandsGenerator.h"
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
#include <numeric>
using namespace MapGeneration;

// Concrete islands (id "concrete-islands", legacy id 5): a map cut into one big island per colony
// plus a few small neutral islands, separated by channels.
//
// HISTORY. Bradley Arsenault wrote it in July 2008 as "the first random map" of a new generation
// engine (the area-grid toolkit now in shared/legacy/Regions.cpp and StartingPositions.cpp), and
// revised it twice that week, then fixed maps too small for the colony count and improved its fruit
// and swarm placement. The name describes the look: the channels follow the straight boundaries of
// a weighted Voronoi-like split, so the islands have the blocky outline of poured concrete slabs
// rather than a natural coast. On this branch it gained resource amounts, extra island and channel
// controls and a Sandy beaches switch; its layout is otherwise the 2008 one.
//
// HOW IT WORKS.
// 1. Spread one point per colony and per extra island evenly over the map (splitUpPoints), then
//   grow areas from them in proportion to their weights (splitUpArea): a colony weighs 10 and an
//   extra island 1 to 3, so every colony's island is several times a neutral island.
// 2. Build a height field: 75 everywhere, plus noise of up to 15 either way for a rough coast.
// 3. Dig the channels: tiles within `channel-width` steps of an area boundary are lowered by 13 per
//   step closer (see below), and the heights are read as water below 45, sand from 45 to 55 and
//   grass above.
// 4. Put algae down the deepest middle of each channel, fill each neutral island half with wheat
//   and half with a few fruit trees, and lay out each colony's island (divideUpPlayerLands: wheat
//   and wood fields along its coast, stone in its interior, the swarm beside its wheat).
//
// GAME RULES IT LEANS ON (docs/map-generators/GAME_RULES_FOR_MAP_DESIGN.md):
// - Water blocks walking until a colony can swim: every island, colony and neutral, is reached by
//   swimming only, so the neutral islands are the first prizes once pools are built.
// - Fruit is a weapon: the neutral islands' fruit is what lets a colony that takes them pull hungry
//   enemy units to its inns.
// - Wheat and wood regrow near water, so divideUpPlayerLands puts every colony's fields on the
//   zones nearest the coast.
// - Grass may not touch water: the 45-55 band is the beach; with Sandy beaches off, controlSand
//   still leaves the one-tile ring the engine requires.
//
// Colonies get equal-weight islands but not equal shapes; fairness is statistical (the lobby keeps
// the best-scoring seed), with reopenCrampedStarts as the backstop at non-default amounts.
static bool generate(Game &game, GenerationContext &context)
{
	context.stage = "layout";
	const ConcreteIslandsOptions options(context.request);
	const int channelWidth = options.channel_width;
	game.map.makeHomogenMap(context.request.terrainType);
	for (int i = 0; i < context.request.nbTeams; ++i)
		game.addTeam();

	// This keeps track of the current area number
	int areaNumber = 1;

	std::vector<int> grid(game.map.getW() * game.map.getH(), 0);
	std::vector<MapGeneratorPoint> teamPoints;
	std::vector<int> weights1;
	std::vector<int> weights2;
	std::vector<int> teamAreaNumbers;
	std::vector<int> islandAreaNumbers;

	// Add in team bases. weights1 (all 1) spreads the points evenly; weights2 is each area's growth
	// rate in splitUpArea, so a colony's island (10) ends up several times a neutral island (1 to
	// 3).
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		teamPoints.push_back(MapGeneratorPoint(0, 0));
		weights1.push_back(1);
		weights2.push_back(10);
		teamAreaNumbers.push_back(areaNumber);
		areaNumber += 1;
	}

	// Add in auxilary islands
	//  A negative value retains the original random-count mode for comparisons.
	int islandsCount = options.extra_islands;
	for (int i = 0; i < islandsCount; ++i)
	{
		teamPoints.push_back(MapGeneratorPoint(0, 0));
		weights1.push_back(1);
		weights2.push_back(1 + context.stream("layout")() % 3);
		islandAreaNumbers.push_back(areaNumber);
		areaNumber += 1;
	}

	std::vector<int> areaNumbers = teamAreaNumbers;
	areaNumbers.insert(areaNumbers.end(), islandAreaNumbers.begin(), islandAreaNumbers.end());

	// Initially divide up the land
	splitUpPoints(game.map, context, grid, 0, teamPoints, weights1);
	splitUpArea(game.map, context, grid, 0, teamPoints, weights2, areaNumbers);

	// Create a heightmap that will be used to give the map a rough edge. 75 sits 20 above the
	// grass line (55) and 30 above the water line (45), and noise of up to 15 either way can never
	// push an untouched tile into the sea: only the channels below make water.
	std::vector<int> heights(game.map.getW() * game.map.getH(), 75);
	adjustHeightmapFromPerlinNoise(game.map, context, heights, 15);

	// Compute the distance of every square from the border
	std::vector<MapGeneratorPoint> sources;
	findBorderPoints(game.map, grid, sources);
	std::vector<MapGeneratorPoint> obstacles;
	std::vector<int> distances;
	computeDistances(game.map, sources, obstacles, distances);

	// Locations near the border are deaper, thus causing more water
	//
	// A tile d steps from the nearest area boundary (d = 0 on it) inside the channel width is
	// lowered by (channelWidth - 1 - d) * 13. With the default width 5 that is 52, 39, 26, 13 and 0
	// for d = 0 to 4, giving heights of 23, 36, 49, 62 and 75 before the noise of up to 15 either
	// way. So the boundary tile is always water (at most 38), the tile beside it is water unless
	// the noise lifts it by more than 8, the next is beach or water or grass depending on the
	// noise, and beyond that is grass. Counting both sides of the boundary, a channel is about
	// three to five tiles of water with a ragged edge. Each extra step of channel width adds 13 to
	// every lowering, about one more tile of water on each side. 13 is close to the noise's swing,
	// so the coast wobbles by about a tile: rough, but the boundary tile itself can never surface
	// and break a channel.
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int d = distances[y * game.map.getW() + x] - 1;
			if (d < channelWidth)
			{
				heights[y * game.map.getW() + x] -= (channelWidth - 1 - d) * 13;
			}
		}
	}

	// Use the heightmap to put in water, grass, and sand
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			// Water under 45, a 10-high band of sand for the beach, grass above 55.
			int total_height = heights[y * game.map.getW() + x];
			if (total_height < 45)
				game.map.setUMatPos(x, y, WATER, 1);
			else if (options.sandy_beaches && total_height >= 45 && total_height <= 55)
				game.map.setUMatPos(x, y, SAND, 1);
			else
				game.map.setUMatPos(x, y, GRASS, 1);
		}
	}
	game.map.controlSand();

	// Go through the map again and place alga, down the deepest middle of every channel; the algae
	// amount moves how deep that is.
	//
	// At the default amount the cut-off is 10: only the boundary tiles of a width-5 channel (23
	// before noise) reach it, and only where the noise dips by 13 or more, so algae is a broken
	// thread down the channel's middle. Wider channels dig deeper and grow more of it. Algae needs
	// sand within reach to regrow, which a channel a few tiles wide always has.
	const int algaeDepth = int(scaledCount(10, options.algae));
	for (int x = 0; x < game.map.getW() && options.algae > 0; ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int total_height = heights[y * game.map.getW() + x];
			if (total_height <= algaeDepth)
			{
				game.map.setResource(x, y, ALGA, 1);
			}
		}
	}

	// Reset the grid, and recompute within the boundaries of the various islands: the same points
	// and weights, grown again over grass only, so each area now covers exactly its island's
	// buildable land and grid 0 is everything else (sea and beach), which divideUpPlayerLands
	// measures from.
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			grid[y * game.map.getW() + x] = 0;
		}
	}
	splitUpArea(game.map, context, grid, 0, teamPoints, weights2, areaNumbers, true);

	// Fill in the auxilary islands
	for (int i = 0; i < islandsCount; ++i)
	{
		// Initialize
		std::vector<int> areaWeights;
		std::vector<int> areaNumbers;
		for (int j = 0; j < 2; ++j)
		{
			areaWeights.push_back(1);
			areaNumbers.push_back(areaNumber);
			areaNumber += 1;
		}
		// The wheat half of the island grows faster or slower than the fruit half, in proportion
		// to the wheat amount.
		if (options.wheat != 100 && options.wheat > 0)
		{
			const int common = std::gcd(options.wheat, 100);
			areaWeights[0] = options.wheat / common;
			areaWeights[1] = 100 / common;
		}

		// Divide the area. Its possible the area will be so small it can't be used
		if (divideUpArea(game.map, context, grid, islandAreaNumbers[i], areaWeights, areaNumbers))
		{
			// Fill in wheat
			std::vector<MapGeneratorPoint> points;
			getAllPoints(game.map, grid, areaNumbers[0], points);
			if (options.wheat > 0)
				fillInResource(game.map, context, points, CORN, 2);
			points.clear();

			// Place some fruit: 1 to 6 trees of random kinds, so a neutral island may hold one, two
			// or all three kinds of fruit, and is worth more to a colony the more kinds it has.
			int fruit_n = int(scaledCount(context.stream("layout")() % 6 + 1, options.fruit));
			getAllPoints(game.map, grid, areaNumbers[1], points);
			chooseRandomPoints(game.map, context, points, fruit_n);
			for (unsigned int j = 0; j < points.size(); ++j)
			{
				game.map.setResource(points[j].x, points[j].y,
									 CHERRY + context.stream("layout")() % 3, 1);
			}
		}
	}

	if (!divideUpPlayerLands(game, context, grid, teamAreaNumbers, areaNumber,
							 {options.wheat, options.wood, options.stone}))
		return false;
	// A colony's own fields are its only starting wheat and wood, and a field or deposit grown well
	// past its default size can also wall the colony in with nowhere left to build.
	reopenCrampedStarts(game, context, {options.wheat, options.wood, options.stone, options.algae,
										options.fruit});

	// Initialize final team info
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		game.teams[i]->createLists();
	}
	return true;
}

GeneratorDefinition concreteIslandsDefinition()
{
	return {
		"concrete-islands",
		5,
		"Concrete islands",
		1,
		false,
		// Channel width is how many steps from a boundary are dug (about two tiles of water per
		// step beyond 3); extra islands is the number of neutral islands.
		{{"channel-width", "Channel width", 5, 8, 1, 5, ControlGroup::Terrain, false},
		 {"extra-islands", "Extra islands", 0, 6, 1, 3, ControlGroup::Terrain, false},
		 // Off, islands meet their channels without a band of sand.
		 GeneratorControl::toggle("sandy-beaches", "Sandy beaches", true, ControlGroup::Terrain),
		 // Wheat and wood scale each colony's fields and the neutral islands' wheat; stone
		 // each colony's deposits; algae the channels'; fruit the neutral islands'.
		 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
		 GeneratorControl::percentage("wood-amount", "Wood amount"),
		 GeneratorControl::percentage("stone-amount", "Stone amount"),
		 GeneratorControl::percentage("algae-amount", "Algae amount"),
		 GeneratorControl::percentage("fruit-amount", "Fruit amount")},
		generate};
}
