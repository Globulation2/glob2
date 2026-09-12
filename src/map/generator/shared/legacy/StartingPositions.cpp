// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2008 Bradley Arsenault
#include "StartingPositions.h"
#include "Distances.h"
#include "Game.h"
#include "GenerationContext.h"
#include "GeneratorDefinition.h"
#include "GlobalContainer.h"
#include "HeightMap.h"
#include "Map.h"
#include "Regions.h"
#include "Resources.h"
#include "Terrain.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
using namespace MapGeneration;

namespace MapGeneration
{
bool divideUpPlayerLands(Game &game, GenerationContext &context, std::vector<int> &grid,
						 std::vector<int> &teamAreaNumbers, int &areaNumber,
						 const PlayerLandResources &resources)
{
	context.stage = "resources and starts";
	int typeNum = globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
	BuildingType *swarm = globalContainer->buildingsTypes.get(typeNum);

	// Compute the distances from water
	std::vector<MapGeneratorPoint> sources;
	std::vector<MapGeneratorPoint> obstacles;
	std::vector<int> distances;
	obstacles.clear();
	getAllPoints(game.map, grid, 0, sources);
	computeDistances(game.map, sources, obstacles, distances);

	// Create a new heightmap from noise and distance to water
	std::vector<int> heightmap(game.map.getW() * game.map.getH(), 50);
	adjustHeightmapFromPerlinNoise(game.map, context, heightmap, 5);
	for (int x = 0; x < game.map.getW(); ++x)
	{
		for (int y = 0; y < game.map.getH(); ++y)
		{
			int d = distances[y * game.map.getW() + x];
			heightmap[y * game.map.getW() + x] -= d;
		}
	}

	for (int i = 0; i < context.request.nbTeams; ++i)
	{
		// Initialize
		std::vector<int> areaWeights;
		std::vector<int> areaNumbers;
		for (int j = 0; j < 12; ++j)
		{
			areaWeights.push_back(1);
			areaNumbers.push_back(areaNumber);
			areaNumber += 1;
		}

		// Divide the area. Its possible the area will be so small it can't be used
		if (divideUpArea(game.map, context, grid, teamAreaNumbers[i], areaWeights, areaNumbers))
		{
			// Sort the list of areas based on how close they are to water
			std::vector<int> areaDistances(areaNumbers.size());
			std::vector<int> areaIndexes(areaNumbers.size());
			for (unsigned int j = 0; j < areaNumbers.size(); ++j)
			{
				areaDistances[j] =
					computeAverageDistance(game.map, grid, areaNumbers[j], distances);
				areaIndexes[j] = j;
			}
			ListComparator compare(areaDistances);
			std::sort(areaIndexes.begin(), areaIndexes.end(), compare);
			for (unsigned int j = 0; j < areaDistances.size(); ++j)
			{
				areaIndexes[j] = areaNumbers[areaIndexes[j]];
			}
			areaNumbers = areaIndexes;

			// Place wood. A field covers the zone's tiles whose raised height clears 50, so the
			// wood amount sets how far the raise reaches in from the coast.
			std::vector<MapGeneratorPoint> wheatWoodPoints;
			std::vector<MapGeneratorPoint> wheatPoints;
			getAllPoints(game.map, grid, areaNumbers[3], wheatWoodPoints);
			getAllPoints(game.map, grid, areaNumbers[4], wheatWoodPoints);
			getAllPoints(game.map, grid, areaNumbers[5], wheatWoodPoints);
			const int woodRise = int(scaledCount(10, resources.wood));
			adjustHeightmapFromPoints(game.map, wheatWoodPoints, heightmap, woodRise);
			for (unsigned int j = 0; j < wheatWoodPoints.size(); ++j)
			{
				int h = heightmap[wheatWoodPoints[j].y * game.map.getW() + wheatWoodPoints[j].x];
				if (h > 50 && resources.wood > 0)
				{
					game.map.setResource(wheatWoodPoints[j].x, wheatWoodPoints[j].y, WOOD, 1);
				}
			}
			wheatWoodPoints.clear();

			// Place wheat. The swarm goes beside the default wheat field, whatever amount is
			// actually placed, so the base layout doesn't move with the wheat amount.
			getAllPoints(game.map, grid, areaNumbers[0], wheatWoodPoints);
			getAllPoints(game.map, grid, areaNumbers[1], wheatWoodPoints);
			getAllPoints(game.map, grid, areaNumbers[2], wheatWoodPoints);
			const int wheatRise = int(scaledCount(10, resources.wheat));
			adjustHeightmapFromPoints(game.map, wheatWoodPoints, heightmap, wheatRise);
			for (unsigned int j = 0; j < wheatWoodPoints.size(); ++j)
			{
				int h = heightmap[wheatWoodPoints[j].y * game.map.getW() + wheatWoodPoints[j].x];
				if (h > 50 && resources.wheat > 0)
					game.map.setResource(wheatWoodPoints[j].x, wheatWoodPoints[j].y, CORN, 1);
				if (h - wheatRise + 10 > 50)
					wheatPoints.push_back(wheatWoodPoints[j]);
			}

			// These are all points in the base
			std::vector<MapGeneratorPoint> baseLocations;
			getAllPoints(game.map, grid, areaNumbers[6], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[7], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[8], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[9], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[10], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[11], baseLocations);

			// Place stone
			int numberOfStone = int(scaledCount(6, resources.stone));
			std::vector<MapGeneratorPoint> stoneLocations = baseLocations;
			chooseRandomPoints(game.map, context, stoneLocations, numberOfStone);
			for (unsigned int j = 0; j < stoneLocations.size(); ++j)
			{
				game.map.setResource(stoneLocations[j].x, stoneLocations[j].y, STONE, 1);
			}

			// Concerning starting locations, we also consider points inside the wheat and wood
			// areas
			getAllPoints(game.map, grid, areaNumbers[0], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[1], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[2], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[3], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[4], baseLocations);
			getAllPoints(game.map, grid, areaNumbers[5], baseLocations);

			// Compute every points distance from the wheat
			std::vector<int> wheatDistance;
			computeDistances(game.map, wheatPoints, obstacles, wheatDistance);

			// Only consider points between 1 and 4 squares from wheat
			auto startsWithinOfWheat = [&](int window)
			{
				std::vector<MapGeneratorPoint> found;
				for (unsigned int j = 0; j < baseLocations.size(); ++j)
				{
					int minValue = 100000;
					for (int x = 0; x < 4; ++x)
					{
						for (int y = 0; y < 4; ++y)
						{
							int nx = game.map.normalizeX(baseLocations[j].x + x);
							int ny = game.map.normalizeY(baseLocations[j].y + y);
							minValue = std::min(wheatDistance[ny * game.map.getW() + nx], minValue);
						}
					}
					if (minValue >= 1 && minValue <= window)
					{
						found.push_back(baseLocations[j]);
					}
				}
				return found;
			};
			std::vector<MapGeneratorPoint> startingLocations = startsWithinOfWheat(2);

			// Place swarms
			chooseFreeForBuildingSquares(game, startingLocations, swarm, i);
			// A field grown well past its default size covers the building sites beside it, and the
			// window above only looks 1 to 2 tiles out from where the default-sized wheat field
			// would lie. Rather than fail, look further out from that same wheat for a site the
			// fields have left clear: the colony still starts beside its own farmland, just not
			// right up against it. Only a non-default amount can reach this.
			for (int window = 6;
				 startingLocations.empty() && window <= 24 &&
				 (resources.wheat != 100 || resources.wood != 100 || resources.stone != 100);
				 window += 6)
			{
				startingLocations = startsWithinOfWheat(window);
				chooseFreeForBuildingSquares(game, startingLocations, swarm, i);
			}
			if (startingLocations.size() == 0)
			{
				return false;
			}
			int chosen = context.stream("regions")() % startingLocations.size();
			Building *b =
				addBuilding(game, startingLocations[chosen].x, startingLocations[chosen].y, i,
							IntBuildingType::SWARM_BUILDING, 1, false);
			if (b == NULL)
			{
				return false;
			}

			// Set the initial viewport location
			game.teams[i]->startPosX = b->posX;
			game.teams[i]->startPosY = b->posY;
			game.teams[i]->startPosSet = Team::START_POS_FROM_SWARM;
			context.bootX[i] = b->posX;
			context.bootY[i] = b->posY;

			// Place units around the swarm
			std::vector<MapGeneratorPoint> unitLocations = baseLocations;
			chooseFreeForGroundUnits(game.map, unitLocations, i);
			chooseTouchingBuilding(game.map, unitLocations, b);
			chooseRandomPoints(game.map, context, unitLocations, context.request.nbWorkers);
			if (unitLocations.size() != static_cast<size_t>(context.request.nbWorkers))
			{
				context.detail = "Not enough worker positions";
				return false;
			}
			for (unsigned int n = 0; n < unitLocations.size(); ++n)
			{
				if (!game.addUnit(unitLocations[n].x, unitLocations[n].y, i, WORKER, 0, 0, 0, 0))
				{
					context.detail = "Worker placement failed";
					return false;
				}
			}
		}
		else
		{
			return false;
		}
	}
	return true;
}

void chooseFreeForBuildingSquares(Game &game, std::vector<MapGeneratorPoint> &points,
								  BuildingType *type, int team)
{
	std::vector<MapGeneratorPoint> newPoints;
	for (unsigned int n = 0; n < points.size(); ++n)
	{
		if (game.checkRoomForBuilding(points[n].x, points[n].y, type, team, false))
		{
			newPoints.push_back(MapGeneratorPoint(points[n].x, points[n].y));
		}
	}
	points = newPoints;
}

void chooseFreeForGroundUnits(Map &map, std::vector<MapGeneratorPoint> &points, int team)
{
	std::vector<MapGeneratorPoint> newPoints;
	for (unsigned int n = 0; n < points.size(); ++n)
	{
		if (map.isFreeForGroundUnit(points[n].x, points[n].y, false, 1 << team))
		{
			newPoints.push_back(MapGeneratorPoint(points[n].x, points[n].y));
		}
	}
	points = newPoints;
}

void chooseTouchingBuilding(Map &map, std::vector<MapGeneratorPoint> &points, Building *building)
{
	std::vector<MapGeneratorPoint> newPoints;
	for (unsigned int n = 0; n < points.size(); ++n)
	{
		if (map.doesPosTouchBuilding(points[n].x, points[n].y, building->gid))
		{
			newPoints.push_back(MapGeneratorPoint(points[n].x, points[n].y));
		}
	}
	points = newPoints;
}

Building *addBuilding(Game &game, int x, int y, int team, int type, int level,
					  bool underConstruction)
{
	std::string name = IntBuildingType::typeFromShortNumber(type);
	int typeNum = globalContainer->buildingsTypes.getTypeNum(name, level - 1, underConstruction);
	BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
	if (bt == NULL)
	{
		return NULL;
	}

	if (game.checkRoomForBuilding(x, y, bt, team, false))
	{
		if (bt->maxUnitWorking)
			return game.addBuilding(x, y, typeNum, team, 1, 0);
		else
			return game.addBuilding(x, y, typeNum, team, 0, 0);
	}
	return NULL;
}
} // namespace MapGeneration

namespace MapGeneration
{
bool placeArchipelagoStarts(Game &game, GenerationContext &context, int islandSize)
{
	for (int s = 0; s < context.request.nbTeams; s++)
	{
		// Legacy layout searches may choose a footprint across a torus seam.
		// Buildings wrap those coordinates, and start metadata must wrap too.
		context.bootX[s] = game.map.normalizeX(context.bootX[s]);
		context.bootY[s] = game.map.normalizeY(context.bootY[s]);
		if (game.mapHeader.getNumberOfTeams() <= s)
			game.addTeam();
		int squareSize = 5 + islandSize / 10;
		game.map.setUMatPos(context.bootX[s] + 2, context.bootY[s] + 0, GRASS, squareSize);
		game.map.setUMatPos(context.bootX[s] + 2, context.bootY[s] + 2, GRASS, squareSize);

		Sint32 typeNum = globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
		if (!game.checkRoomForBuilding(context.bootX[s], context.bootY[s],
									   globalContainer->buildingsTypes.get(typeNum), s, false))
		{
			context.detail = "No room for a colony's swarm";
			return false;
		}
		game.teams[s]->startPosX = context.bootX[s];
		game.teams[s]->startPosY = context.bootY[s];
		Building *b = game.addBuilding(context.bootX[s], context.bootY[s], typeNum, s);
		if (!b)
			return false;
		for (int i = 0; i < context.request.nbWorkers; i++)
			if (game.addUnit(context.bootX[s] + (i % 4), context.bootY[s] - 1 - (i / 4), s, WORKER,
							 0, 0, 0, 0) == NULL)
			{
				context.detail = "No room for a colony's starting workers";
				return false;
			}
		game.teams[s]->createLists();
	}
	game.map.smoothResources(islandSize / 10);
	return true;
}

bool placeStarts(Game &game, GenerationContext &context)
{
	for (int s = 0; s < context.request.nbTeams; s++)
	{
		// Legacy layout searches may choose a footprint across a torus seam.
		// Buildings wrap those coordinates, and start metadata must wrap too.
		context.bootX[s] = game.map.normalizeX(context.bootX[s]);
		context.bootY[s] = game.map.normalizeY(context.bootY[s]);
		assert(game.mapHeader.getNumberOfTeams() == s);
		if (game.mapHeader.getNumberOfTeams() <= s)
			game.addTeam();

		game.map.setUMatPos(context.bootX[s] + 2, context.bootY[s] + 0, GRASS, 5);
		game.map.setUMatPos(context.bootX[s] + 2, context.bootY[s] + 2, GRASS, 5);
		game.map.setNoResource(context.bootX[s] + 2, context.bootY[s] + 0, 5);
		game.map.setNoResource(context.bootX[s] + 2, context.bootY[s] + 2, 5);

		Sint32 typeNum = globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
		if (!game.checkRoomForBuilding(context.bootX[s], context.bootY[s],
									   globalContainer->buildingsTypes.get(typeNum), s, false))
		{
			context.detail = "No room for a colony's swarm";
			return false;
		}
		game.teams[s]->startPosX = context.bootX[s];
		game.teams[s]->startPosY = context.bootY[s];
		Building *b = game.addBuilding(context.bootX[s], context.bootY[s], typeNum, s);
		if (!b)
			return false;
		for (int i = 0; i < context.request.nbWorkers; i++)
			if (game.addUnit(context.bootX[s] + (i % 4), context.bootY[s] - 1 - (i / 4), s, WORKER,
							 0, 0, 0, 0) == NULL)
			{
				context.detail = "No room for a colony's starting workers";
				return false;
			}
		game.teams[s]->createLists();
	}
	return true;
}

} // namespace MapGeneration
