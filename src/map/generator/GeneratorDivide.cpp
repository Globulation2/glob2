/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <math.h>
#include <float.h>
#include <time.h>
#include <stdlib.h>

#include "boost/integer_traits.hpp"
#include "boost/integer/common_factor.hpp"
//also the Perlin Noise stuff uses random that is not based on syncRand
#include "boost/random.hpp"
#include "Game.h"
#include "GlobalContainer.h"
#include "HeightMapGenerator.h"
#include "MapGenerationDescriptor.h"
#include "MapGenerator.h"
#include "Map.h"
#include <map>
#include <queue>
#include <set>
#include "Unit.h"
#include "Utilities.h"

bool MapGenerator::divideUpPlayerLands(Game& game, MapGenerationDescriptor& descriptor, std::vector<int>& grid, std::vector<int>& teamAreaNumbers, int& areaNumber)
{
	int typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
	BuildingType *swarm = globalContainer->buildingsTypes.get(typeNum);
	
	//Compute the distances from water
	std::vector<MapGeneratorPoint> sources;
	std::vector<MapGeneratorPoint> obstacles;
	std::vector<int> distances;
	obstacles.clear();
	getAllPoints(game, grid, 0, sources);
	computeDistances(game, sources, obstacles, distances);
	
	//Create a new heightmap from noise and distance to water
	std::vector<int> heightmap(game.map.getW() * game.map.getH(), 50);
	adjustHeightmapFromPerlinNoise(game, heightmap, 5);
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			int d = distances[y * game.map.getW() + x];
			heightmap[y * game.map.getW() + x] -= d;
		}
	}
	
	for(int i=0; i<descriptor.nbTeams; ++i)
	{
		// Initialize
		std::vector<int> areaWeights;
		std::vector<int> areaNumbers;
		for(int j=0; j<12; ++j)
		{
			areaWeights.push_back(1);
			areaNumbers.push_back(areaNumber);
			areaNumber+=1;
		}
		
		// Divide the area. Its possible the area will be so small it can't be used
		if(divideUpArea(game, grid, teamAreaNumbers[i], areaWeights, areaNumbers))
		{
			// Sort the list of areas based on how close they are to water
			std::vector<int> areaDistances(areaNumbers.size());
			std::vector<int> areaIndexes(areaNumbers.size());
			for(unsigned int j=0; j<areaNumbers.size(); ++j)
			{
				areaDistances[j] = computeAverageDistance(game, grid, areaNumbers[j], distances);
				areaIndexes[j] = j;
			}
			ListComparator compare(areaDistances);
			std::sort(areaIndexes.begin(), areaIndexes.end(), compare);
			for(unsigned int j=0; j<areaDistances.size(); ++j)
			{
				areaIndexes[j] = areaNumbers[areaIndexes[j]];
			}
			areaNumbers = areaIndexes;
			
			// Place wood
			std::vector<MapGeneratorPoint> wheatWoodPoints;
			std::vector<MapGeneratorPoint> wheatPoints;
			//std::vector<MapGeneratorPoint> woodPoints;
			getAllPoints(game, grid, areaNumbers[3], wheatWoodPoints);
			getAllPoints(game, grid, areaNumbers[4], wheatWoodPoints);
			getAllPoints(game, grid, areaNumbers[5], wheatWoodPoints);
			adjustHeightmapFromPoints(game, wheatWoodPoints, heightmap, 10);
			for(unsigned int j=0; j<wheatWoodPoints.size(); ++j)
			{
				int h = heightmap[wheatWoodPoints[j].y * game.map.getW() + wheatWoodPoints[j].x];
				if(h > 50)
				{
					game.map.setRessource(wheatWoodPoints[j].x, wheatWoodPoints[j].y, WOOD, 1);
					//woodPoints.push_back(wheatWoodPoints[j]);
				}
			}
			wheatWoodPoints.clear();
			
			// Place wheat
			getAllPoints(game, grid, areaNumbers[0], wheatWoodPoints);
			getAllPoints(game, grid, areaNumbers[1], wheatWoodPoints);
			getAllPoints(game, grid, areaNumbers[2], wheatWoodPoints);
			adjustHeightmapFromPoints(game, wheatWoodPoints, heightmap, 10);
			for(unsigned int j=0; j<wheatWoodPoints.size(); ++j)
			{
				int h = heightmap[wheatWoodPoints[j].y * game.map.getW() + wheatWoodPoints[j].x];
				if(h > 50)
				{
					game.map.setRessource(wheatWoodPoints[j].x, wheatWoodPoints[j].y, CORN, 1);
					wheatPoints.push_back(wheatWoodPoints[j]);
				}
			}
			
			
			// These are all points in the base
			std::vector<MapGeneratorPoint> baseLocations;
			getAllPoints(game, grid, areaNumbers[6], baseLocations);
			getAllPoints(game, grid, areaNumbers[7], baseLocations);
			getAllPoints(game, grid, areaNumbers[8], baseLocations);
			getAllPoints(game, grid, areaNumbers[9], baseLocations);
			getAllPoints(game, grid, areaNumbers[10], baseLocations);
			getAllPoints(game, grid, areaNumbers[11], baseLocations);
			
			// Place stone
			int numberOfStone = 6;
			std::vector<MapGeneratorPoint> stoneLocations = baseLocations;
			chooseRandomPoints(game, stoneLocations, numberOfStone);
			for(unsigned int j=0; j<stoneLocations.size(); ++j)
			{
				game.map.setRessource(stoneLocations[j].x, stoneLocations[j].y, STONE, 1);
			}
			
			
			// Concerning starting locations, we also consider points inside the wheat and wood areas
			getAllPoints(game, grid, areaNumbers[0], baseLocations);
			getAllPoints(game, grid, areaNumbers[1], baseLocations);
			getAllPoints(game, grid, areaNumbers[2], baseLocations);
			getAllPoints(game, grid, areaNumbers[3], baseLocations);
			getAllPoints(game, grid, areaNumbers[4], baseLocations);
			getAllPoints(game, grid, areaNumbers[5], baseLocations);
			
			
			// Compute every points distance from the wheat
			std::vector<int> wheatDistance;
			computeDistances(game, wheatPoints, obstacles, wheatDistance);
			
			// Only consider points between 1 and 4 squares from wheat
			std::vector<MapGeneratorPoint> startingLocations;
			for(unsigned int j=0; j<baseLocations.size(); ++j)
			{
				int minValue = 100000;
				for(int x=0; x<4; ++x)
				{
					for(int y=0; y<4; ++y)
					{
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
			chooseFreeForBuildingSquares(game, startingLocations, swarm, i);
			if(startingLocations.size() == 0)
			{
				return false;
			}
			int chosen = syncRand()%startingLocations.size();
			Building* b = addBuilding(game, startingLocations[chosen].x, startingLocations[chosen].y, i, IntBuildingType::SWARM_BUILDING, 1, false);
			if(b == NULL)
			{
				return false;
			}
			
			// Set the initial viewport location
			game.teams[i]->startPosX=b->posX;
			game.teams[i]->startPosY=b->posY;
			game.teams[i]->startPosSet=3;
			
			// Place units around the swarm
			std::vector<MapGeneratorPoint> unitLocations = baseLocations;
			chooseFreeForGroundUnits(game, unitLocations, i);
			chooseTouchingBuilding(game, unitLocations, b);
			chooseRandomPoints(game, unitLocations, descriptor.nbWorkers);
			for(unsigned int n=0; n<unitLocations.size(); ++n)
			{
				game.addUnit(unitLocations[n].x, unitLocations[n].y, i, WORKER, 0, 0, 0, 0);
			}
		}
		else
		{
			return false;
		}
	}
	return true;
}



bool MapGenerator::divideUpArea(Game& game, std::vector<int>& grid, int areaN, std::vector<int>& weights, std::vector<int>& areaNumbers)
{
	std::vector<MapGeneratorPoint> points;
	std::vector<int> splitWeights;
	for(unsigned int i=0; i<weights.size(); ++i)
	{
		points.push_back(MapGeneratorPoint(0,0));
		splitWeights.push_back(1);
	}
	if(!splitUpPoints(game, grid, areaN, points, splitWeights))
	{
		return false;
	}
	splitUpArea(game, grid, areaN, points, weights, areaNumbers);
	return true;
}



void MapGenerator::createOval(Game& game, std::vector<int>& grid, int areaN, int x, int y, int width, int height)
{
	int h2 = (height/2) * (height/2);
	int w2 = (width/2) * (width/2);
	int t2 = h2 * w2;
	for(int px = -(width/2); px < (width/2); ++px)
	{
		int nx = game.map.normalizeX(x + px);
		int px2 = px*px*h2;
		for(int py = -(height/2); py < (height/2); ++py)
		{
			int ny = game.map.normalizeY(y + py);
			int py2 = py*py*w2;
			if(px2 + py2 < t2)
			{
				grid[ny * game.map.getW() + nx] = areaN;
			}
		}
	}
}



