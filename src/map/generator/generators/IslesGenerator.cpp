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
#include "Pipeline.h"
#include "Regions.h"
#include "Resources.h"
#include "StartingPositions.h"
#include "Terrain.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
using namespace MapGeneration;
#include "IslesGenerator.h"
// What the stages of a roll hand each other: the area grid and the next free area number, each
// colony's seed point, weight and area, the spacing the dispersion found, the height field the
// islands and bridges are raised in, the last distance field, and the bridges' tiles and area.
struct Layout
{
	std::vector<int> grid;
	int areaNumber = 1;
	std::vector<MapGeneratorPoint> teamPoints;
	std::vector<int> teamWeights, teamAreaNumbers;
	int minDist = 0;
	std::vector<int> heightmap, distances;
	std::vector<MapGeneratorPoint> connectorPoints;
	int connectorArea = 0;
	explicit Layout(const Map &map)
		: grid(size_t(map.getW()) * map.getH(), 0), heightmap(size_t(map.getW()) * map.getH(), 50)
	{
	}
};

// Spread the colonies apart and give each an oval island of its own.
static void layoutIslands(Game &game, GenerationContext &context, const IslesOptions &options,
						  Layout &L)
{
	const int islandSize = options.island_size;
	// Do the starting locations of the teams
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		L.teamPoints.push_back(MapGeneratorPoint(0, 0));
		L.teamWeights.push_back(1);
		L.teamAreaNumbers.push_back(L.areaNumber);
		L.areaNumber += 1;
	}
	L.minDist = splitUpPoints(game.map, context, L.grid, 0, L.teamPoints, L.teamWeights);

	// Construct the areas for the teams
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		createOval(game.map, L.grid, L.teamAreaNumbers[i], L.teamPoints[i].x, L.teamPoints[i].y,
				   L.minDist * islandSize / 100, L.minDist * islandSize / 100);
	}
}

// Raise the islands in the height field, by distance from their edges.
static void raiseIslands(Game &game, Layout &L)
{
	// Construct a L.heightmap
	std::vector<MapGeneratorPoint> teamAreaPoints;
	getAllOtherPoints(game.map, L.grid, 0, teamAreaPoints);
	std::vector<MapGeneratorPoint> obstacles;

	computeDistances(game.map, teamAreaPoints, obstacles, L.distances);

	// Stamp out the team areas
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int d = L.distances[y * game.map.getW() + x];
			if (d > 1 && d <= 11)
				L.heightmap[y * game.map.getW() + x] += (11 - d) * 10;
			else if (d == 1)
				L.heightmap[y * game.map.getW() + x] += 100;
		}
	}
}

// Join every pair of islands whose straight line crosses no third island with a land bridge.
static void buildBridges(Game &game, GenerationContext &context, const IslesOptions &options,
						 Layout &L)
{
	const int bridgeWidth = options.bridge_width, bridgeRadius = bridgeWidth - 2;
	// Connect each teams area to each other players area
	L.connectorArea = L.areaNumber;
	L.areaNumber += 1;
	std::vector<MapGeneratorPoint> obstacles;
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		for (int j = i + 1; j < context.request.nbTeams; ++j)
		{
			// Choose one random point from each players area
			std::vector<MapGeneratorPoint> teamI;
			std::vector<MapGeneratorPoint> teamJ;
			getAllPoints(game.map, L.grid, L.teamAreaNumbers[i], teamI);
			getAllPoints(game.map, L.grid, L.teamAreaNumbers[j], teamJ);
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
						int g = L.grid[ny * game.map.getW() + nx];
						if (g != 0 && g != L.teamAreaNumbers[i] && g != L.teamAreaNumbers[j] &&
							g != L.connectorArea)
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
					L.connectorPoints.push_back(linePoints[p]);
					for (int x = -bridgeRadius; x <= bridgeRadius; ++x)
					{
						int nx = game.map.normalizeX(linePoints[p].x + x);
						for (int y = -bridgeRadius; y <= bridgeRadius; ++y)
						{
							int ny = game.map.normalizeY(linePoints[p].y + y);
							int d = L.distances[ny * game.map.getW() + nx];
							if (d > bridgeWidth + 1)
							{
								L.grid[ny * game.map.getW() + nx] = L.connectorArea;
							}
						}
					}
				}
			}
		}
	}
	computeDistances(game.map, L.connectorPoints, obstacles, L.distances);
}

// Raise the bridges, add noise, and turn the height field into water, sand and grass.
static void paintTerrain(Game &game, GenerationContext &context, const IslesOptions &options,
						 Layout &L)
{
	const int bridgeWidth = options.bridge_width;
	// Stamp out the connectors
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int d = L.distances[y * game.map.getW() + x];
			if (d > 1 && d <= bridgeWidth)
				L.heightmap[y * game.map.getW() + x] +=
					(bridgeWidth - d) * (100 / (bridgeWidth - 1));
			else if (d == 1)
				L.heightmap[y * game.map.getW() + x] += 100;
		}
	}

	// Use the L.heightmap to put in water, grass, and sand
	adjustHeightmapFromPerlinNoise(game.map, context, L.heightmap, 45);
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int total_height = L.heightmap[y * game.map.getW() + x];
			if (total_height < 90)
				game.map.setUMatPos(x, y, WATER, 1);
			else if (options.sandy_beaches && total_height > 95 && total_height < 105)
				game.map.setUMatPos(x, y, SAND, 1);
			else
				game.map.setUMatPos(x, y, GRASS, 1);
		}
	}
	game.map.controlSand();
}

// Re-divide the land between the colonies and give each an algae patch just off its coast.
static bool placeAlgae(Game &game, GenerationContext &context, const IslesOptions &options,
					   Layout &L)
{
	const int bridgeWidth = options.bridge_width;
	// Reset the L.grid, and recompute within the boundaries of the various islands
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			if (L.grid[y * game.map.getW() + x] != L.connectorArea)
				L.grid[y * game.map.getW() + x] = 0;
		}
	}
	splitUpArea(game.map, context, L.grid, 0, L.teamPoints, L.teamWeights, L.teamAreaNumbers, true);

	std::vector<int> connectorDistances = L.distances;
	std::vector<MapGeneratorPoint> obstacles;

	// For each team, find a point just off the coast and place algae there
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		std::vector<MapGeneratorPoint> sources;
		getAllPoints(game.map, L.grid, L.teamAreaNumbers[i], sources);
		computeDistances(game.map, sources, obstacles, L.distances);
		std::vector<MapGeneratorPoint> possible;
		for (int x = 0; x < game.map.getW(); ++x)
		{
			for (int y = 0; y < game.map.getH(); ++y)
			{
				int d = L.distances[y * game.map.getW() + x];
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
	return true;
}

static bool generate(Game &game, GenerationContext &context)
{
	context.stage = "layout";
	const IslesOptions options(context.request);
	game.map.makeHomogenMap(context.request.terrainType);
	for (int i = 0; i < context.request.nbTeams; ++i)
		game.addTeam();
	Layout L(game.map);
	layoutIslands(game, context, options, L);
	raiseIslands(game, L);
	buildBridges(game, context, options, L);
	paintTerrain(game, context, options, L);
	if (!placeAlgae(game, context, options, L))
		return false;
	if (!divideUpPlayerLands(game, context, L.grid, L.teamAreaNumbers, L.areaNumber,
							 {options.wheat, options.wood, options.stone}))
	{
		return false;
	}
	// A colony's own fields are its only wheat and wood, and a field or deposit grown well past its
	// default size can also wall the colony in with nowhere left to build.
	reopenCrampedStarts(game, context, {options.wheat, options.wood, options.stone, options.algae});

	// Initialize final team info
	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		game.teams[i]->createLists();
	}
	return true;
}

GeneratorDefinition islesDefinition()
{
	return {
		"isles",
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
