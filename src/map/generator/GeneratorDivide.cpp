// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

//also the Perlin Noise stuff uses random that is not based on syncRand
#include "Game.h"
#include "GlobalContainer.h"
#include "MapGenerationDescriptor.h"
#include "MapGenerator.h"
#include "Map.h"
#include "Unit.h"
#include "Utilities.h"

bool MapGenerator::divideUpPlayerLands(Game& game, MapGenerationDescriptor& descriptor, std::vector<int>& grid, std::vector<int>& teamAreaNumbers, int& areaNumber)
{
    return divideUpPlayerLandsTask(game, descriptor, grid, teamAreaNumbers, areaNumber).run();
}

GAGCore::CooperativeTask MapGenerator::divideUpPlayerLandsTask(Game& game, MapGenerationDescriptor& descriptor, std::vector<int>& grid, std::vector<int>& teamAreaNumbers, int& areaNumber)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	int typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
	BuildingType *swarm = globalContainer->buildingsTypes.get(typeNum);
	
	//Compute the distances from water
	std::vector<MapGeneratorPoint> sources;
	std::vector<MapGeneratorPoint> obstacles;
	std::vector<int> distances;
	obstacles.clear();
	co_await getAllPointsTask(game, grid, 0, sources);
	co_await computeDistancesTask(game, sources, obstacles, distances);
	
	//Create a new heightmap from noise and distance to water
	std::vector<int> heightmap(game.map.getW() * game.map.getH(), 50);
	co_await adjustHeightmapFromPerlinNoiseTask(game, heightmap, 5);
	for(int x=0; x<game.map.getW(); ++x)
	{
		if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		for(int y=0; y<game.map.getH(); ++y)
		{
			if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			int d = distances[y * game.map.getW() + x];
			heightmap[y * game.map.getW() + x] -= d;
		}
	}
	
	for(int i=0; i<descriptor.nbTeams; ++i)
	{
		if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		// Initialize
		std::vector<int> areaWeights;
		std::vector<int> areaNumbers;
		for(int j=0; j<12; ++j)
		{
			if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			areaWeights.push_back(1);
			areaNumbers.push_back(areaNumber);
			areaNumber+=1;
		}
		
		// Divide the area. Its possible the area will be so small it can't be used
		if(co_await divideUpAreaTask(game, grid, teamAreaNumbers[i], areaWeights, areaNumbers))
		{
			// Sort the list of areas based on how close they are to water
			std::vector<int> areaDistances(areaNumbers.size());
			std::vector<int> areaIndexes(areaNumbers.size());
			for(unsigned int j=0; j<areaNumbers.size(); ++j)
			{
				if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
				co_await computeAverageDistanceTask(game, grid, areaNumbers[j], distances, areaDistances[j]);
				areaIndexes[j] = j;
			}
			ListComparator compare(areaDistances);
			std::sort(areaIndexes.begin(), areaIndexes.end(), compare);
			for(unsigned int j=0; j<areaDistances.size(); ++j)
			{
				if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
				areaIndexes[j] = areaNumbers[areaIndexes[j]];
			}
			areaNumbers = areaIndexes;
			
			// Place wood
			std::vector<MapGeneratorPoint> wheatWoodPoints;
			std::vector<MapGeneratorPoint> wheatPoints;
			co_await getAllPointsTask(game, grid, areaNumbers[3], wheatWoodPoints);
			co_await getAllPointsTask(game, grid, areaNumbers[4], wheatWoodPoints);
			co_await getAllPointsTask(game, grid, areaNumbers[5], wheatWoodPoints);
			co_await adjustHeightmapFromPointsTask(game, wheatWoodPoints, heightmap, 10);
			for(unsigned int j=0; j<wheatWoodPoints.size(); ++j)
			{
				if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
				int h = heightmap[wheatWoodPoints[j].y * game.map.getW() + wheatWoodPoints[j].x];
				if(h > 50)
				{
					game.map.setResource(wheatWoodPoints[j].x, wheatWoodPoints[j].y, WOOD, 1);
				}
			}
			wheatWoodPoints.clear();
			
			// Place wheat
			co_await getAllPointsTask(game, grid, areaNumbers[0], wheatWoodPoints);
			co_await getAllPointsTask(game, grid, areaNumbers[1], wheatWoodPoints);
			co_await getAllPointsTask(game, grid, areaNumbers[2], wheatWoodPoints);
			co_await adjustHeightmapFromPointsTask(game, wheatWoodPoints, heightmap, 10);
			for(unsigned int j=0; j<wheatWoodPoints.size(); ++j)
			{
				if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
				int h = heightmap[wheatWoodPoints[j].y * game.map.getW() + wheatWoodPoints[j].x];
				if(h > 50)
				{
					game.map.setResource(wheatWoodPoints[j].x, wheatWoodPoints[j].y, CORN, 1);
					wheatPoints.push_back(wheatWoodPoints[j]);
				}
			}
			
			
			// These are all points in the base
			std::vector<MapGeneratorPoint> baseLocations;
			co_await getAllPointsTask(game, grid, areaNumbers[6], baseLocations);
			co_await getAllPointsTask(game, grid, areaNumbers[7], baseLocations);
			co_await getAllPointsTask(game, grid, areaNumbers[8], baseLocations);
			co_await getAllPointsTask(game, grid, areaNumbers[9], baseLocations);
			co_await getAllPointsTask(game, grid, areaNumbers[10], baseLocations);
			co_await getAllPointsTask(game, grid, areaNumbers[11], baseLocations);
			
			// Place stone
			int numberOfStone = 6;
			std::vector<MapGeneratorPoint> stoneLocations = baseLocations;
			co_await chooseRandomPointsTask(game, stoneLocations, numberOfStone);
			for(unsigned int j=0; j<stoneLocations.size(); ++j)
			{
				if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
				game.map.setResource(stoneLocations[j].x, stoneLocations[j].y, STONE, 1);
			}
			
			
			// Concerning starting locations, we also consider points inside the wheat and wood areas
			co_await getAllPointsTask(game, grid, areaNumbers[0], baseLocations);
			co_await getAllPointsTask(game, grid, areaNumbers[1], baseLocations);
			co_await getAllPointsTask(game, grid, areaNumbers[2], baseLocations);
			co_await getAllPointsTask(game, grid, areaNumbers[3], baseLocations);
			co_await getAllPointsTask(game, grid, areaNumbers[4], baseLocations);
			co_await getAllPointsTask(game, grid, areaNumbers[5], baseLocations);
			
			
			// Compute every points distance from the wheat
			std::vector<int> wheatDistance;
			co_await computeDistancesTask(game, wheatPoints, obstacles, wheatDistance);
			
			// Only consider points between 1 and 4 squares from wheat
			std::vector<MapGeneratorPoint> startingLocations;
			for(unsigned int j=0; j<baseLocations.size(); ++j)
			{
				if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
				int minValue = 100000;
				for(int x=0; x<4; ++x)
				{
					if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
					for(int y=0; y<4; ++y)
					{
						if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
						int nx = game.map.normalizeX(baseLocations[j].x + x);
						int ny = game.map.normalizeY(baseLocations[j].y + y);
						minValue = std::min(wheatDistance[ny * game.map.getW() + nx], minValue);
					}
				}
				if(minValue >= 1 && minValue <= 2)
				{
					startingLocations.push_back(baseLocations[j]);
				}
			}
			
			// Place swarms
			co_await chooseFreeForBuildingSquaresTask(game, startingLocations, swarm, i);
			if(startingLocations.size() == 0)
			{
				co_return false;
			}
			int chosen = syncRand()%startingLocations.size();
			Building* b = addBuilding(game, startingLocations[chosen].x, startingLocations[chosen].y, i, IntBuildingType::SWARM_BUILDING, 1, false);
			if(b == NULL)
			{
				co_return false;
			}
			
			// Set the initial viewport location
			game.teams[i]->startPosX=b->posX;
			game.teams[i]->startPosY=b->posY;
			game.teams[i]->startPosSet=Team::START_POS_FROM_SWARM;
			
			// Place units around the swarm
			std::vector<MapGeneratorPoint> unitLocations = baseLocations;
			co_await chooseFreeForGroundUnitsTask(game, unitLocations, i);
			co_await chooseTouchingBuildingTask(game, unitLocations, b);
			co_await chooseRandomPointsTask(game, unitLocations, descriptor.nbWorkers);
			for(unsigned int n=0; n<unitLocations.size(); ++n)
			{
				if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
				game.addUnit(unitLocations[n].x, unitLocations[n].y, i, WORKER, 0, 0, 0, 0);
			}
		}
		else
		{
			co_return false;
		}
	}
	co_return true;
}



bool MapGenerator::divideUpArea(Game& game, std::vector<int>& grid, int areaN, std::vector<int>& weights, std::vector<int>& areaNumbers)
{
    return divideUpAreaTask(game, grid, areaN, weights, areaNumbers).run();
}

GAGCore::CooperativeTask MapGenerator::divideUpAreaTask(Game& game, std::vector<int>& grid, int areaN, std::vector<int>& weights, std::vector<int>& areaNumbers)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	std::vector<MapGeneratorPoint> points;
	std::vector<int> splitWeights;
	for(unsigned int i=0; i<weights.size(); ++i)
	{
		if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		points.push_back(MapGeneratorPoint(0,0));
		splitWeights.push_back(1);
	}
	if(!co_await splitUpPointsTask(game, grid, areaN, points, splitWeights))
	{
		co_return false;
	}
	co_await splitUpAreaTask(game, grid, areaN, points, weights, areaNumbers);
	co_return true;
}



void MapGenerator::createOval(Game& game, std::vector<int>& grid, int areaN, int x, int y, int width, int height)
{
    createOvalTask(game, grid, areaN, x, y, width, height).run();
}

GAGCore::CooperativeTask MapGenerator::createOvalTask(Game& game, std::vector<int>& grid, int areaN, int x, int y, int width, int height)
{
    unsigned operations = 0;
    co_await GAGCore::CooperativeTask::checkpoint("[Generating map]");
	int h2 = (height/2) * (height/2);
	int w2 = (width/2) * (width/2);
	int t2 = h2 * w2;
	for(int px = -(width/2); px < (width/2); ++px)
	{
		if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
		int nx = game.map.normalizeX(x + px);
		int px2 = px*px*h2;
		for(int py = -(height/2); py < (height/2); ++py)
		{
			if (++operations % 1024 == 0) co_await GAGCore::CooperativeTask::checkpoint();
			int ny = game.map.normalizeY(y + py);
			int py2 = py*py*w2;
			if(px2 + py2 < t2)
			{
				grid[ny * game.map.getW() + nx] = areaN;
			}
		}
	}
    co_return true;
}



