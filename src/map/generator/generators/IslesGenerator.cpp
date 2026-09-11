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
#include "IslesGenerator.h"
static bool generate(Game &game, GenerationContext &context)
{
	context.stage = "layout";
	const IslesOptions options(context.request);
	const int islandSize = options.island_size, bridgeWidth = options.bridge_width,
			  bridgeRadius = bridgeWidth - 2;
	game.map.makeHomogenMap(context.request.terrainType);
	for (int i = 0; i < context.request.nbTeams; ++i)
		game.addTeam();

	int areaNumber = 1;
	std::vector<int> grid(game.map.getW() * game.map.getH(), 0);

	// Do the starting locations of the teams
	std::vector<MapGeneratorPoint> teamPoints;
	std::vector<int> teamWeights;
	std::vector<int> teamAreaNumbers;
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		teamPoints.push_back(MapGeneratorPoint(0, 0));
		teamWeights.push_back(1);
		teamAreaNumbers.push_back(areaNumber);
		areaNumber += 1;
	}
	int minDist = splitUpPoints(game.map, context, grid, 0, teamPoints, teamWeights);

	// Construct the areas for the teams
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		createOval(game.map, grid, teamAreaNumbers[i], teamPoints[i].x, teamPoints[i].y,
				   minDist * islandSize / 100, minDist * islandSize / 100);
	}

	// Construct a heightmap
	std::vector<int> heightmap(game.map.getW() * game.map.getH(), 50);
	std::vector<MapGeneratorPoint> teamAreaPoints;
	getAllOtherPoints(game.map, grid, 0, teamAreaPoints);
	std::vector<MapGeneratorPoint> obstacles;

	std::vector<int> distances;
	computeDistances(game.map, teamAreaPoints, obstacles, distances);

	// Stamp out the team areas
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int d = distances[y * game.map.getW() + x];
			if (d > 1 && d <= 11)
				heightmap[y * game.map.getW() + x] += (11 - d) * 10;
			else if (d == 1)
				heightmap[y * game.map.getW() + x] += 100;
		}
	}

	// Connect each teams area to each other players area
	std::vector<MapGeneratorPoint> connectorPoints;
	int connectorArea = areaNumber;
	areaNumber += 1;
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		for (int j = i + 1; j < context.request.nbTeams; ++j)
		{
			// Choose one random point from each players area
			std::vector<MapGeneratorPoint> teamI;
			std::vector<MapGeneratorPoint> teamJ;
			getAllPoints(game.map, grid, teamAreaNumbers[i], teamI);
			getAllPoints(game.map, grid, teamAreaNumbers[j], teamJ);
			chooseRandomPoints(game.map, context, teamI, 1);
			chooseRandomPoints(game.map, context, teamJ, 1);

			// Traverse between the two points
			std::vector<MapGeneratorPoint> linePoints;
			getAllPointsLine(game.map, teamI[0].x, teamI[0].y, teamJ[0].x, teamJ[0].y, linePoints);
			// If a connection can be made without going through another teams area, then do it
			bool failed = false;
			for (unsigned int p = 0; p < linePoints.size() && !failed; ++p)
			{
				for (int x = -bridgeRadius; x <= bridgeRadius && !failed; ++x)
				{
					int nx = game.map.normalizeX(linePoints[p].x + x);
					for (int y = -bridgeRadius; y <= bridgeRadius && !failed; ++y)
					{
						int ny = game.map.normalizeY(linePoints[p].y + y);
						int g = grid[ny * game.map.getW() + nx];
						if (g != 0 && g != teamAreaNumbers[i] && g != teamAreaNumbers[j] &&
							g != connectorArea)
						{
							failed = true;
						}
					}
				}
			}
			// Make the connection. Without land bridges the same points are still drawn, so the
			// rest of the map stays as it was, but the islands are left apart.
			if (!failed && options.land_bridges)
			{
				for (unsigned int p = 0; p < linePoints.size(); ++p)
				{
					connectorPoints.push_back(linePoints[p]);
					for (int x = -bridgeRadius; x <= bridgeRadius; ++x)
					{
						int nx = game.map.normalizeX(linePoints[p].x + x);
						for (int y = -bridgeRadius; y <= bridgeRadius; ++y)
						{
							int ny = game.map.normalizeY(linePoints[p].y + y);
							int d = distances[ny * game.map.getW() + nx];
							if (d > bridgeWidth + 1)
							{
								grid[ny * game.map.getW() + nx] = connectorArea;
							}
						}
					}
				}
			}
		}
	}
	computeDistances(game.map, connectorPoints, obstacles, distances);

	// Stamp out the connectors
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int d = distances[y * game.map.getW() + x];
			if (d > 1 && d <= bridgeWidth)
				heightmap[y * game.map.getW() + x] += (bridgeWidth - d) * (100 / (bridgeWidth - 1));
			else if (d == 1)
				heightmap[y * game.map.getW() + x] += 100;
		}
	}

	// Use the heightmap to put in water, grass, and sand
	adjustHeightmapFromPerlinNoise(game.map, context, heightmap, 45);
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int total_height = heightmap[y * game.map.getW() + x];
			if (total_height < 90)
				game.map.setUMatPos(x, y, WATER, 1);
			else if (options.sandy_beaches && total_height > 95 && total_height < 105)
				game.map.setUMatPos(x, y, SAND, 1);
			else
				game.map.setUMatPos(x, y, GRASS, 1);
		}
	}
	game.map.controlSand();

	// Reset the grid, and recompute within the boundaries of the various islands
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			if (grid[y * game.map.getW() + x] != connectorArea)
				grid[y * game.map.getW() + x] = 0;
		}
	}
	splitUpArea(game.map, context, grid, 0, teamPoints, teamWeights, teamAreaNumbers, true);

	std::vector<int> connectorDistances = distances;

	// For each team, find a point just off the coast and place algae there
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		std::vector<MapGeneratorPoint> sources;
		getAllPoints(game.map, grid, teamAreaNumbers[i], sources);
		computeDistances(game.map, sources, obstacles, distances);
		std::vector<MapGeneratorPoint> possible;
		for (int x = 0; x < game.map.getW(); ++x)
		{
			for (int y = 0; y < game.map.getH(); ++y)
			{
				int d = distances[y * game.map.getW() + x];
				int d2 = connectorDistances[y * game.map.getW() + x];
				if (d == 8 && (!options.land_bridges || d2 > bridgeWidth))
				{
					possible.push_back(MapGeneratorPoint(x, y));
				}
			}
		}
		if (possible.size() == 0)
		{
			return false;
		}
		int r = context.stream("layout")() % possible.size();
		// A five by five patch of algae at the default amount.
		setScaledResource(game.map, possible[r].x, possible[r].y, ALGA, 5, options.algae);
	}

	if (!divideUpPlayerLands(game, context, grid, teamAreaNumbers, areaNumber,
							 {options.wheat, options.wood, options.stone}))
	{
		return false;
	}
	// A colony's own fields are its only wheat and wood, and a field or deposit grown well past its
	// default size can also wall the colony in with nowhere left to build. At any amount other than
	// the default, open up such a colony and then make sure each still has both crops within reach,
	// in case the clearing took the nearest one along with the wall.
	if (options.wheat != 100 || options.wood != 100 || options.stone != 100 ||
		options.algae != 100)
	{
		openCrampedStarts(game, context);
		guaranteeStartingResources(game, context, 24, 32);
	}

	// Initialize final team info
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		game.teams[i]->createLists();
	}
	return true;
}

GeneratorDefinition islesDefinition()
{
	return {"isles",
			6,
			"Isles",
			2,
			false,
			{{"island-size", "Island size", 45, 65, 5, 60, ControlGroup::Terrain, false},
			 {"bridge-width", "Land bridge width", 3, 6, 1, 4, ControlGroup::Terrain, false},
			 // Off, every colony's island stands alone in the sea.
			 GeneratorControl::toggle("land-bridges", "Land bridges", true, ControlGroup::Terrain),
			 // Off, islands meet the sea without a band of sand.
			 GeneratorControl::toggle("sandy-beaches", "Sandy beaches", true, ControlGroup::Terrain),
			 // Wheat and wood scale each colony's fields, stone its deposits, algae its patch.
			 GeneratorControl::percentage("wheat-amount", "Wheat amount"),
			 GeneratorControl::percentage("wood-amount", "Wood amount"),
			 GeneratorControl::percentage("stone-amount", "Stone amount"),
			 GeneratorControl::percentage("algae-amount", "Algae amount")},
			generate};
}
