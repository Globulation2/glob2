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

	// Add in team bases
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

	// Create a heightmap that will be used to give the map a rough edge
	std::vector<int> heights(game.map.getW() * game.map.getH(), 75);
	adjustHeightmapFromPerlinNoise(game.map, context, heights, 15);

	// Compute the distance of every square from the border
	std::vector<MapGeneratorPoint> sources;
	findBorderPoints(game.map, grid, sources);
	std::vector<MapGeneratorPoint> obstacles;
	std::vector<int> distances;
	computeDistances(game.map, sources, obstacles, distances);

	// Locations near the border are deaper, thus causing more water
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

	// Go through the map again and place alga, down the deepest middle of every channel; the
	// algae amount moves how deep that is.
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

	// Reset the grid, and recompute within the boundaries of the various islands
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

			// Place some fruit
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
