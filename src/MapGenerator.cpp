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
#include "boost/math/common_factor.hpp"
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

bool MapGenerator::generateMap(Game& game, MapGenerationDescriptor &descriptor)
{
	if (verbose)
		printf("Generating map, please wait ....\n");
	game.map.setSize(descriptor.wDec, descriptor.hDec);
	game.map.setGame(&game);
	setRandomSyncRandSeed();
	
	switch (descriptor.methode)
	{
		case MapGenerationDescriptor::eUNIFORM:
			game.map.makeHomogenMap(descriptor.terrainType);
			game.addTeam();
		break;
		case MapGenerationDescriptor::eSWAMP:
		case MapGenerationDescriptor::eISLANDS:
		case MapGenerationDescriptor::eRIVER:
		case MapGenerationDescriptor::eCRATERLAKES:
			if (!game.map.makeRandomMap(descriptor))
				return false;
			if (!game.makeRandomMap(descriptor))
				return false;
			break;
		case MapGenerationDescriptor::eCONCRETEISLANDS:
			if (!computeConcreteIslands(game, descriptor))
				return false;
			break;
		case MapGenerationDescriptor::eOLDRANDOM:
			if (!game.map.oldMakeRandomMap(descriptor))
				return false;
			if (!game.makeRandomMap(descriptor))
				return false;
			break;
		case MapGenerationDescriptor::eOLDISLANDS:
			if (!game.map.oldMakeIslandsMap(descriptor))
				return false;
			if (!game.oldMakeIslandsMap(descriptor))
				return false;
			break;
						
		default:
			assert(false);
	}
	
	// compile script
	game.script.compileScript(&game);
	
	if (verbose)
		printf(".... map generated.\n");
	return true;
}


bool MapGenerator::computeConcreteIslands(Game& game, MapGenerationDescriptor& descriptor)
{
	game.map.makeHomogenMap(descriptor.terrainType);
	for(int i=0; i<descriptor.nbTeams; ++i)
		game.addTeam();

	//This keeps track of the current area number
	int areaNumber = 1;
	
	std::vector<int> grid(game.map.getW() * game.map.getH(), 0);
	std::vector<MapGeneratorPoint> teamPoints;
	std::vector<int> weights1;
	std::vector<int> weights2;
	std::vector<int> teamAreaNumbers;
	std::vector<int> islandAreaNumbers;
	
	//Add in team bases
	for(int i=0; i<descriptor.nbTeams; ++i)
	{
		teamPoints.push_back(MapGeneratorPoint(0,0));
		weights1.push_back(1);
		weights2.push_back(10);
		teamAreaNumbers.push_back(areaNumber);
		areaNumber+=1;
	}
	
	//Add in auxilary islands
	int islandsCount = syncRand() % (descriptor.nbTeams*2);
	for(int i=0; i<islandsCount; ++i)
	{
		teamPoints.push_back(MapGeneratorPoint(0,0));
		weights1.push_back(1);
		weights2.push_back(1+syncRand()%3);
		islandAreaNumbers.push_back(areaNumber);
		areaNumber+=1;
	}
	
	std::vector<int> areaNumbers = teamAreaNumbers;
	areaNumbers.insert(areaNumbers.end(), islandAreaNumbers.begin(), islandAreaNumbers.end());
	
	// Initiallly devide up the land
	splitUpPoints(game, grid, 0, teamPoints, weights1);
	splitUpArea(game, grid, 0, teamPoints, weights2, areaNumbers);
	
	// Create a heightmap that will be used to give the map a rough edge
	std::vector<int> heights(game.map.getW() * game.map.getH(), 75);
	adjustHeightmapFromPerlinNoise(game, heights, 15);
	
	// Compute the distance of every square from the border
	std::vector<MapGeneratorPoint> sources;
	findBorderPoints(game, grid, sources);
	std::vector<MapGeneratorPoint> obstacles;
	std::vector<int> distances;
	computeDistances(game, sources, obstacles, distances);
	
	// Locations near the border are deaper, thus causing more water
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			int d = distances[y * game.map.getW() + x] - 1;
			if(d < 6)
			{
				heights[y * game.map.getW() + x] -= (5-d)*13;
			}
		}
	}
	
	// Use the heightmap to put in water, grass, and land
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			int total_height = heights[y * game.map.getW() + x];
			if(total_height<45)
				game.map.setUMatPos(x, y, WATER, 1);
			else if(total_height>=45 && total_height<=55)
				game.map.setUMatPos(x, y, SAND, 1);
			else
				game.map.setUMatPos(x, y, GRASS, 1);
		}
	}
	game.map.controlSand();

	// Go through the map again and place alga
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			int total_height = heights[y * game.map.getW() + x];
			if(total_height<=10)
			{
				game.map.setRessource(x, y, ALGA, 1);
			}
		}
	}
	
	// Reset the grid, and recompute within the boundaries of the various islands
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			grid[y * game.map.getW() + x] = 0;
		}
	}
	splitUpArea(game, grid, 0, teamPoints, weights2, areaNumbers, true);
	
	// Fill in the auxilary islands
	for(int i=0; i<islandsCount; ++i)
	{
		// Initialize
		std::vector<int> areaWeights;
		std::vector<int> areaNumbers;
		for(int j=0; j<2; ++j)
		{
			areaWeights.push_back(1);
			areaNumbers.push_back(areaNumber);
			areaNumber+=1;
		}
		
		// Devide the area. Its possible the area will be so small it can't be used
		if(devideUpArea(game, grid, islandAreaNumbers[i], areaWeights, areaNumbers))
		{
			// Fill in wheat
			std::vector<MapGeneratorPoint> points;
			getAllPoints(game, grid, areaNumbers[0], points);
			fillInResource(game, points, CORN, 2);
			points.clear();
			
			// Place some fruit
			int fruit_n = syncRand()%6;
			getAllPoints(game, grid, areaNumbers[1], points);
			chooseRandomPoints(game, points, fruit_n);
			for(int j=0; j<points.size(); ++j)
			{
				game.map.setRessource(points[j].x, points[j].y, CHERRY + syncRand()%3, 1);
			}
		}
	}
	
	int typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
	BuildingType *swarm = globalContainer->buildingsTypes.get(typeNum);
	
	//Compute the distances from water
	sources.clear();
	obstacles.clear();
	getAllPoints(game, grid, 0, sources);
	computeDistances(game, sources, obstacles, distances);
	
	//Create a new heightmap from noise and distance to water
	heights.clear();
	heights.resize(game.map.getW() * game.map.getH(), 50);
	adjustHeightmapFromPerlinNoise(game, heights, 5);
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			int d = distances[y * game.map.getW() + x];
			heights[y * game.map.getW() + x] -= d;
		}
	}
	
	
	// Devide up the player-lands
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
		
		// Devide the area. Its possible the area will be so small it can't be used
		if(devideUpArea(game, grid, teamAreaNumbers[i], areaWeights, areaNumbers))
		{
			// Sort the list of areas based on how close they are to water
			std::vector<int> areaDistances(areaNumbers.size());
			std::vector<int> areaIndexes(areaNumbers.size());
			for(int j=0; j<areaNumbers.size(); ++j)
			{
				areaDistances[j] = computeAverageDistance(game, grid, areaNumbers[j], distances);
				areaIndexes[j] = j;
			}
			ListComparator compare(areaDistances);
			std::sort(areaIndexes.begin(), areaIndexes.end(), compare);
			for(int j=0; j<areaDistances.size(); ++j)
			{
				areaIndexes[j] = areaNumbers[areaIndexes[j]];
			}
			areaNumbers = areaIndexes;
			
			//Place wood
			std::vector<MapGeneratorPoint> wheatWoodPoints;
			getAllPoints(game, grid, areaNumbers[3], wheatWoodPoints);
			getAllPoints(game, grid, areaNumbers[4], wheatWoodPoints);
			getAllPoints(game, grid, areaNumbers[5], wheatWoodPoints);
			adjustHeightmapFromPoints(game, wheatWoodPoints, heights, 10);
			for(int j=0; j<wheatWoodPoints.size(); ++j)
			{
				int h = heights[wheatWoodPoints[j].y * game.map.getW() + wheatWoodPoints[j].x];
				if(h > 50)
					game.map.setRessource(wheatWoodPoints[j].x, wheatWoodPoints[j].y, WOOD, 1);
			}
			wheatWoodPoints.clear();
			
			//Place wheat
			getAllPoints(game, grid, areaNumbers[0], wheatWoodPoints);
			getAllPoints(game, grid, areaNumbers[1], wheatWoodPoints);
			getAllPoints(game, grid, areaNumbers[2], wheatWoodPoints);
			adjustHeightmapFromPoints(game, wheatWoodPoints, heights, 10);
			for(int j=0; j<wheatWoodPoints.size(); ++j)
			{
				int h = heights[wheatWoodPoints[j].y * game.map.getW() + wheatWoodPoints[j].x];
				if(h > 50)
					game.map.setRessource(wheatWoodPoints[j].x, wheatWoodPoints[j].y, CORN, 1);
			}
			
			
			///All remaining points are part of the main base
			std::vector<MapGeneratorPoint> baseLocations;
			getAllPoints(game, grid, areaNumbers[6], baseLocations);
			getAllPoints(game, grid, areaNumbers[7], baseLocations);
			getAllPoints(game, grid, areaNumbers[8], baseLocations);
			getAllPoints(game, grid, areaNumbers[9], baseLocations);
			getAllPoints(game, grid, areaNumbers[10], baseLocations);
			getAllPoints(game, grid, areaNumbers[11], baseLocations);
			
			//Place stone
			int numberOfStone = 6;
			std::vector<MapGeneratorPoint> stoneLocations = baseLocations;
			chooseRandomPoints(game, stoneLocations, numberOfStone);
			for(int j=0; j<stoneLocations.size(); ++j)
			{
				game.map.setRessource(stoneLocations[j].x, stoneLocations[j].y, STONE, 1);
			}
			
			//Compute every points distance from the wheat
			std::vector<int> wheatDistance;
			computeDistances(game, wheatWoodPoints, obstacles, wheatDistance);
			
			//Only consider points between 1 and 4 squares from wheat
			std::vector<MapGeneratorPoint> startingLocations;
			for(int j=0; j<baseLocations.size(); ++j)
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
			
			//Place swarms
			chooseFreeForBuildingSquares(game, startingLocations, swarm, i);
			int chosen = syncRand()%startingLocations.size();
			Building* b = addBuilding(game, startingLocations[chosen].x, startingLocations[chosen].y, i, IntBuildingType::SWARM_BUILDING, 1, false);
			
			//Set the initial viewport location
			game.teams[i]->startPosX=b->posX;
			game.teams[i]->startPosY=b->posY;
			
			//Place units arround the swarm
			std::vector<MapGeneratorPoint> unitLocations = baseLocations;
			chooseFreeForGroundUnits(game, unitLocations, i);
			chooseTouchingBuilding(game, unitLocations, b);
			chooseRandomPoints(game, unitLocations, descriptor.nbWorkers);
			for(int n=0; n<unitLocations.size(); ++n)
			{
				game.addUnit(unitLocations[n].x, unitLocations[n].y, i, WORKER, 0, 0, 0, 0);
			}
		}
	}
	
	// Initialize final team info
	for(int i=0; i<descriptor.nbTeams; ++i)
	{
		game.teams[i]->createLists();
	}
	return true;
}



bool MapGenerator::devideUpArea(Game& game, std::vector<int>& grid, int areaN, std::vector<int>& weights, std::vector<int>& areaNumbers)
{
	std::vector<MapGeneratorPoint> points;
	std::vector<int> splitWeights;
	for(int i=0; i<weights.size(); ++i)
	{
		points.push_back(MapGeneratorPoint(0,0));
		splitWeights.push_back(1);
	}
	if(!splitUpPoints(game, grid, areaN, points, splitWeights))
		return false;
	splitUpArea(game, grid, areaN, points, weights, areaNumbers);
	return true;
}



bool MapGenerator::splitUpPoints(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points, std::vector<int>& weights)
{
	std::vector<MapGeneratorPoint> startingPoints;
	long center_x=0;
	long center_y=0;
	int total_squares=0;
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			if(grid[y * game.map.getW() + x]==areaN)
			{
				startingPoints.push_back(MapGeneratorPoint(x, y));
			}
		}
	}
	
	if(startingPoints.empty())
		return false;
	
	Uint32 n = syncRand() % startingPoints.size();

	std::vector<MapGeneratorPoint> obstacles;
	getAllOtherPoints(game, grid, areaN, obstacles);
	std::vector<MapGeneratorPoint> sources;
	sources.push_back(startingPoints[n]);
	std::vector<int> heights;
	computeDistances(game, sources, obstacles, heights);
	sources.clear();
	
	for(int i=0; i<points.size(); ++i)
	{
		int max = 0;
		std::vector<MapGeneratorPoint> possible;
		for(int x=0; x<game.map.getW(); ++x)
		{
			for(int y=0; y<game.map.getH(); ++y)
			{
				int h = heights[y * game.map.getW() + x];
				if(h > max)
				{
					max = h;
					possible.clear();
				}
				if(h >= max)
				{
					possible.push_back(MapGeneratorPoint(x, y));
				}
			}
		}
		int n = syncRand() % possible.size();
		points[i] = possible[n];
		sources.push_back(points[i]);
		computeDistances(game, sources, obstacles, heights);
	}
	startingPoints.clear();
	heights.clear();
	sources.clear();
	obstacles.clear();
	
	bool cont=true;
	while(cont)
	{
		bool changed=false;
		for(int i=0; i<points.size(); ++i)
		{
			int best = boost::integer_traits<int>::const_max;
			for(int j=0; j<points.size(); ++j)
			{
				if(i == j)
					continue;
				int dist=game.map.warpDistSquare(points[i].x, points[i].y, points[j].x, points[j].y) * weights[j];
				best = std::min(dist, best);
			}
			int orig = best;
			int best_x = -1;
			int best_y = -1;
			//std::cout<<"For point "<<i<<" cur="<<orig<<std::endl;
			for(int dx=-1; dx<=1; ++dx)
			{
				for(int dy=-1; dy<=1; ++dy)
				{
					if(dx==0 && dy==0)
						continue;
					int nx = game.map.normalizeX(points[i].x + dx);
					int ny = game.map.normalizeY(points[i].y + dy);
					if(grid[ny * game.map.getW() + nx]  != areaN)
						continue;
					int score=boost::integer_traits<int>::const_max;
					bool invalid=false;
					for(int j=0; j<points.size(); ++j)
					{
						if(i == j)
							continue;
						if(nx == points[j].x && ny == points[j].y)
						{
							invalid=true;
							break;
						}
						int dist=game.map.warpDistSquare(nx, ny, points[j].x, points[j].y) * weights[j];
						score = std::min(dist, score);
					}
					if(invalid)
						continue;
					//std::cout<<"dx="<<dx<<", dy="<<dy<<": score="<<score<<std::endl;
					
					if(score>best)
					{
						best = score;
						best_x = nx;
						best_y = ny;
					}
				}
			}
			if(best_x != -1)
			{
				if(std::abs(long(best - orig)) >= 1)
					changed=true;
				points[i].x = best_x;
				points[i].y = best_y;
			}
		}
		if(!changed)
		{
			cont = false;
		}
	}
	
	for(int i=0; i<points.size(); ++i)
	{
		for(int j=0; j<points.size(); ++j)
		{
			if(i!=j && points[i].x == points[j].x && points[i].y == points[j].y)
				return false;
		}
	}
	boost::random_number_generator<boost::mt19937> adapter(randomGenerator);
	std::random_shuffle(points.begin(), points.end(), adapter);
	return true;
}




void MapGenerator::splitUpArea(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points, std::vector<int>& weights, std::vector<int>& areaNumbers, bool grassOnly)
{
	std::vector<int> gradient(game.map.getW() * game.map.getH(), 0);
	
	Uint32 wDec = game.map.wDec;
	Uint32 hMask = game.map.hMask;
	Uint32 wMask = game.map.wMask;

	std::vector<std::list<int> > squares(points.size());
	std::vector<int> expansion(points.size(), 0);
	std::vector<int> current;
	std::vector<int> count;
	
	for(int i=0; i<points.size(); ++i)
	{
		grid[points[i].y * game.map.getW() + points[i].x] = i;
		gradient[points[i].y << wDec | points[i].x] = 1;
		squares[i].push_back(points[i].y << wDec | points[i].x);
		
		current.push_back(1);
		count.push_back(1);
	}
	
	
	bool cont=true;
	while(cont)
	{
		bool found=false;
		for(int p=0; p<points.size(); ++p)
		{
			expansion[p]+=weights[p];
			if(!squares[p].empty())
				found=true;
			while(expansion[p] > 0 && !squares[p].empty())
			{
				Uint32 deltaAddrG = squares[p].back();
				squares[p].erase(--squares[p].end());

				size_t y = deltaAddrG >> wDec;      // Calculate the coordinates of
				size_t x = deltaAddrG & wMask;      // the current field and of the
				
				size_t yu = ((y - 1) & hMask);      // fields next to it.
				size_t yd = ((y + 1) & hMask);      // We live on a torus! If we are on
				size_t xl = ((x - 1) & wMask);      // the "last line" of the map, the
				size_t xr = ((x + 1) & wMask);      // next line is the line 0 again.
				
				
				int t = grid[(y << wDec) | x];
				assert(t < points.size());
				int g = gradient[(y << wDec) | x] + 1;
				grid[(y << wDec) | x] = areaNumbers[t];
				
				size_t deltaAddrC[8];
				int *addr;
				int side;
				
				deltaAddrC[0] = (yu << wDec) | xl;  // Calculate the positions of the
				deltaAddrC[1] = (yu << wDec) | x ;  // 8 fields next to us from their
				deltaAddrC[2] = (yu << wDec) | xr;  // coordinates.
				deltaAddrC[3] = (y  << wDec) | xr;
				deltaAddrC[4] = (yd << wDec) | xr;
				deltaAddrC[5] = (yd << wDec) | x ;
				deltaAddrC[6] = (yd << wDec) | xl;
				deltaAddrC[7] = (y  << wDec) | xl;
				
				if(g != current[p])
				{
					current[p] = g;
					count[p] = 0;
				}
				
				for (int ci=0; ci<8; ci++)          // Check for each of this fields if we
				{                                   // can improve its gradient value
					addr = &gradient[deltaAddrC[ci]];
					side = *addr;
					if (side==0 && grid[deltaAddrC[ci]]==areaN)
					{
						if(grassOnly && !game.map.isGrass(deltaAddrC[ci]))
							continue;
						*addr = g;
						grid[deltaAddrC[ci]] = t;
						count[p]+=1;
						expansion[p]-=1;
						
						Uint32 randLocation = syncRand() % count[p];
						std::list<int>::iterator i = squares[p].begin();
						std::advance(i, randLocation);
						squares[p].insert(i, deltaAddrC[ci]);
					}
				}
			}
		}
		if(!found)
			cont = false;
	}
}



void MapGenerator::getAllPoints(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points)
{
	for(int x=0;  x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			if(grid[y * game.map.getW() + x] == areaN)
				points.push_back(MapGeneratorPoint(x, y));
		}
	}
}



void MapGenerator::getAllOtherPoints(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points)
{
	for(int x=0;  x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			if(grid[y * game.map.getW() + x] != areaN)
				points.push_back(MapGeneratorPoint(x, y));
		}
	}
}




void MapGenerator::findBorderPoints(Game& game, std::vector<int>& grid, std::vector<MapGeneratorPoint>& points)
{
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			bool found=false;
			for(int dx=-1; dx<=1 && !found; ++dx)
			{
				for(int dy=-1; dy<=1 && !found; ++dy)
				{
					if(grid[game.map.normalizeY(y + dy) * game.map.getW() + game.map.normalizeX(x + dx)] != grid[y * game.map.getW() + x])
					{
						found=true;
					}
				}
			}
			if(found)
				points.push_back(MapGeneratorPoint(x, y));
		}
	}
}



void MapGenerator::setAsArea(Game& game, std::vector<int>& grid, int areaN, std::vector<MapGeneratorPoint>& points)
{
	for(int i=0; i<points.size(); ++i)
	{
		grid[points[i].y * game.map.getW() + points[i].x] = areaN;
	}
}



void MapGenerator::fillInResource(Game& game, std::vector<MapGeneratorPoint>& points, int ressourceType, int maxFillSize)
{
	for(int n=0;  n<points.size(); ++n)
	{
		game.map.setRessource(points[n].x, points[n].y, ressourceType, 1+syncRand()%maxFillSize);
	}
}



void MapGenerator::chooseRandomPoints(Game& game, std::vector<MapGeneratorPoint>& points, int n)
{
	n = std::min(int(points.size()), n);
	for(int i=0; i<n; ++i)
	{
		int r = syncRand() % (points.size()-i);
		std::iter_swap(points.begin() + i, points.begin() + i + r);
	}
	points.erase(points.begin() + n, points.end());
}



void MapGenerator::chooseFreeForBuildingSquares(Game& game, std::vector<MapGeneratorPoint>& points, BuildingType* type, int team)
{
	std::vector<MapGeneratorPoint> newPoints;
	for(int n=0; n<points.size(); ++n)
	{
		if(game.checkRoomForBuilding(points[n].x, points[n].y, type, team, false))
		{
			newPoints.push_back(MapGeneratorPoint(points[n].x, points[n].y));
		}
	}
	points=newPoints;
}



void MapGenerator::chooseFreeForGroundUnits(Game& game, std::vector<MapGeneratorPoint>& points, int team)
{
	std::vector<MapGeneratorPoint> newPoints;
	for(int n=0; n<points.size(); ++n)
	{
		if(game.map.isFreeForGroundUnit(points[n].x, points[n].y, false, 1<<team))
		{
			newPoints.push_back(MapGeneratorPoint(points[n].x, points[n].y));
		}
	}
	points = newPoints;
}



void MapGenerator::chooseTouchingBuilding(Game& game, std::vector<MapGeneratorPoint>& points, Building* building)
{
	std::vector<MapGeneratorPoint> newPoints;
	for(int n=0; n<points.size(); ++n)
	{
		if(game.map.doesPosTouchBuilding(points[n].x, points[n].y, building->gid))
		{
			newPoints.push_back(MapGeneratorPoint(points[n].x, points[n].y));
		}
	}
	points = newPoints;
}



void MapGenerator::computePercentageOfAreas(Game& game, std::vector<int>& grid)
{
	// Compute land sizes
	std::map<int, int> amounts;
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			amounts[grid[y * game.map.getW() + x]] +=1;
		}
	}
	for(std::map<int,int>::iterator i=amounts.begin(); i!=amounts.end(); ++i)
	{
		int percent = i->second * 10000 / (game.map.getW() * game.map.getH());
		std::cout<<"Area "<<i->first<<" takes up "<<percent/100<<"."<<percent%100<<"%"<<std::endl;
	}
}



void MapGenerator::adjustHeightmapFromPoints(Game& game, std::vector<MapGeneratorPoint>& points, std::vector<int>& heightmap, int value)
{
	for(int i=0; i<points.size(); ++i)
	{
		heightmap[points[i].y * game.map.getW() + points[i].x] += value;
	}
}



void MapGenerator::adjustHeightmapFromPerlinNoise(Game& game, std::vector<int>& heights, int spread)
{
	HeightMap noise(game.map.getW(), game.map.getH());
	noise.makePlain(4);
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			heights[y * game.map.getW() + x] += noise.uiLevel(x, y, spread*2) - spread;
		}
	}
}



void MapGenerator::computeDistances(Game& game, std::vector<MapGeneratorPoint>& sources, std::vector<MapGeneratorPoint>& obtacles, std::vector<int>& heightmap)
{
	std::queue<int> places;
	heightmap.clear();
	heightmap.resize(game.map.getW() * game.map.getH(), 0);
	for(int i=0; i<sources.size(); ++i)
	{
		heightmap[sources[i].y * game.map.getW() + sources[i].x] = 1;
		places.push(sources[i].y * game.map.getW() + sources[i].x);
	}
	for(int i=0; i<obtacles.size(); ++i)
	{
		heightmap[obtacles[i].y * game.map.getW() + obtacles[i].x] = -1;
	}
	
	Uint32 wDec = game.map.wDec;
	Uint32 hMask = game.map.hMask;
	Uint32 wMask = game.map.wMask;
	while (!places.empty())
	{
		int deltaAddrG = places.front();
		places.pop();
		
		size_t y = deltaAddrG >> wDec;      // Calculate the coordinates of
		size_t x = deltaAddrG & wMask;      // the current field and of the
		
		size_t yu = ((y - 1) & hMask);      // fields next to it.
		size_t yd = ((y + 1) & hMask);      // We live on a torus! If we are on
		size_t xl = ((x - 1) & wMask);      // the "last line" of the map, the
		size_t xr = ((x + 1) & wMask);      // next line is the line 0 again.
		
		int g = heightmap[(y << wDec) | x] + 1;
		
		size_t deltaAddrC[8];
		int *addr;
		int side;
		
		deltaAddrC[0] = (yu << wDec) | xl;  // Calculate the positions of the
		deltaAddrC[1] = (yu << wDec) | x ;  // 8 fields next to us from their
		deltaAddrC[2] = (yu << wDec) | xr;  // coordinates.
		deltaAddrC[3] = (y  << wDec) | xr;
		deltaAddrC[4] = (yd << wDec) | xr;
		deltaAddrC[5] = (yd << wDec) | x ;
		deltaAddrC[6] = (yd << wDec) | xl;
		deltaAddrC[7] = (y  << wDec) | xl;
		for (int ci=0; ci<8; ci++)          // Check for each of this fields if we
		{                                   // can improve its gradient value
			addr = &heightmap[deltaAddrC[ci]];
			side = *addr;
			if (side==0)
			{
				*addr = g;
				places.push(deltaAddrC[ci]);
			}
		}
	}
}



int MapGenerator::computeAverageDistance(Game& game, std::vector<int>& grid, int areaN, std::vector<int> heightmap)
{
	long total = 0;
	int count = 0;
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			if(grid[y * game.map.getW() + x] == areaN)
			{
				total += heightmap[y * game.map.getW() + x];
				count+=1;
			}
		}
	}
	return total/count;
}



void MapGenerator::joinAreas(Game& game, std::vector<int>& grid, std::vector<int> toBeJoined, std::vector<int> target)
{
	int numberOfJoins = toBeJoined.size() / target.size();
	std::vector<std::vector<bool> > borders(toBeJoined.size(), std::vector<bool>(toBeJoined.size(), false));
	
	std::map<int, int> newJoined;
	for(int i=0; i<toBeJoined.size(); ++i)
	{
		newJoined[toBeJoined[i]] = i;
	}
	
	// Find out which areas border with which other areas
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			int g = grid[y * game.map.getW() + x];
			if(newJoined.find(g) == newJoined.end())
				continue;
				
			for(int dx=-1; dx<=1; ++dx)
			{
				for(int dy=-1; dy<=1; ++dy)
				{
					int nx = game.map.normalizeX(x + dx);
					int ny = game.map.normalizeY(y + dy);
					int g2 = grid[ny * game.map.getW() + nx];
					if(newJoined.find(g2) == newJoined.end())
						continue;
					if(g != g2)
					{
						borders[newJoined[g]][newJoined[g2]] = true;
						borders[newJoined[g2]][newJoined[g]] = true;
					}
				}
			}
		}
	}
	
	// Attempt to merge the groups, when a successful full-merging is found, exit
	std::vector<Node> nodes(toBeJoined.size());
	for(int i=0; i<toBeJoined.size(); ++i)
	{
		nodes[i].original.push_back(i);
		nodes[i].borders = borders[i];
	}
	
	std::vector<int> targets(toBeJoined.size());
	for(int i=0; i<nodes.size(); ++i)
	{
		for(int j=0; j<nodes[i].original.size(); ++j)
		{
			targets[nodes[i].original[j]] = i;
		}
	}
	
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			int& g = grid[y * game.map.getW() + x];
			if(newJoined.find(g) == newJoined.end())
				continue;
			g = target[targets[newJoined[g]]];
		}
	}
}



bool MapGenerator::joinLoop(Game& game, std::vector<Node> nodes, std::vector<Node>& result, int numberOfJoins)
{
	// Choose which one to work on
	int whichOne=-1;
	for(int i=0; i<nodes.size(); ++i)
	{
		if(nodes[i].original.size() < numberOfJoins)
		{
			whichOne = i;
			break;
		}
	}
	
	// If there none that can be worked on, make this combination valid
	if(whichOne==-1)
	{
		result = nodes;
		return true;
	}
	
	// Attempt all possible merge combinations and loop
	Node old1 = nodes[whichOne];
	std::vector<Node> newNodes = nodes;
	newNodes.erase(newNodes.begin() + whichOne);
	boost::random_number_generator<boost::mt19937> adapter(randomGenerator);
	std::random_shuffle(newNodes.begin(), newNodes.end(), adapter);
	
	bool found=false;
	for(int i=0; i<newNodes.size(); ++i)
	{
		if(newNodes[i].original.size()==1 && old1.borders[newNodes[i].original[0]])
		{
			found=true;
		
			Node old2 = newNodes[i];
			
			Node n;
			n.original = old1.original;
			n.original.insert(n.original.begin(), old2.original.begin(), old2.original.end());
			n.borders = old1.borders;
			for(int j=0; j<old1.borders.size(); ++j)
			{
				if(old1.borders[j] || old2.borders[j])
				{
					n.borders[j] = true;
				}
			}
			
			std::vector<Node> stillNewNodes(newNodes);
			stillNewNodes.erase(stillNewNodes.begin() + i);
			stillNewNodes.insert(stillNewNodes.begin(), n);
			
			if(joinLoop(game, stillNewNodes, result, numberOfJoins))
				return true;
		}
	}
	return false;
}



Building* MapGenerator::addBuilding(Game& game, int x, int y, int team, int type, int level, bool underConstruction)
{
	std::string name = IntBuildingType::typeFromShortNumber(type);
	int typeNum=globalContainer->buildingsTypes.getTypeNum(name, level-1, underConstruction);
	BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
	if(bt == NULL)
	{
		return NULL;
	}

	int placeX = x;
	int placeY = y;
	if (game.checkRoomForBuilding(x, y, bt, team, false))
	{
		if(bt->maxUnitWorking)
			return game.addBuilding(x, y, typeNum, team, 1, 0);
		else
			return game.addBuilding(x, y, typeNum, team, 0, 0);
	}
	return NULL;
}



///generates a map that is of one terrain type only
void Map::makeHomogenMap(TerrainType terrainType)
{
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
			undermap[y*w+x]=terrainType;
	regenerateMap(0, 0, w, h);
}

///cares for the sand so water is never next to grass
void Map::controlSand(void)
{
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			int tt=(int)undermap[y*w+x];
			switch (tt)
			{
				case WATER: //the direct neighbours get checked for GRASS
					for (int dy=-1; dy<=1; dy++)
						for (int dx=-1; dx<=1; dx++)
							if (getUMTerrain(x+dx, y+dy)==GRASS)
								undermap[y*w+x]=SAND;
					break;
				case SAND:
					break;
				case GRASS: //the direct neighbours get checked for WATER
					for (int dy=-1; dy<=1; dy++)
						for (int dx=-1; dx<=1; dx++)
							if (getUMTerrain(x+dx, y+dy)==WATER)
								undermap[y*w+x]=SAND;
					break;
			}
		}
}

void Map::smoothRessources(int times)
{
	for (int s=0; s<times; s++)
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				int d=getTerrain(x, y)-272;
				int r=d/10;
				int l=d%5;
				if ((d>=0)&&(d<40)&&(syncRand()&4))
				{
					if (l<=(int)(syncRand()&3))
						setTerrain(x, y, d+273);
					else 
					{
						// we extand ressource:
						int dx, dy;
						Unit::dxdyfromDirection(syncRand()&7, &dx, &dy);
						int nx=x+dx;
						int ny=y+dy;
						if (getGroundUnit(nx, ny)==NOGUID)
							if (((r==WOOD||r==CORN||r==STONE)&&isGrass(nx, ny))||((r==ALGA)&&isWater(nx, ny)))
								setTerrain(nx, ny, 272+(r*10)+((syncRand()&1)*5));
					}
				}
			}
}

void simulateRandomMap(int smooth, double baseWater, double baseSand, double baseGrass, double *finalWater, double *finalSand, double *finalGrass)
{
	int w=32<<(smooth>>2);
	int h=w;
	int s=w*h;
	int m=s-1;
	VARARRAY(int,undermap,w*h);
	
	int totalRatio=0x7FFF;
	int waterRatio=(int)(baseWater*((double)totalRatio));
	int sandRatio =(int)(baseSand *((double)totalRatio));
	int grassRatio=(int)(baseGrass*((double)totalRatio));
	totalRatio=waterRatio+sandRatio+grassRatio;
	
	/// First, we create a fully random patchwork:
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			int r=syncRand()%totalRatio;
			r-=waterRatio;
			if (r<0)
			{
				undermap[y*w+x]=0;
				continue;
			}
			r-=sandRatio;
			if (r<0)
			{
				undermap[y*w+x]=1;
				continue;
			}
			r-=grassRatio;
			if (r<0)
			{
				undermap[y*w+x]=2;
				continue;
			}
			assert(false);//Want's to sing ?
		}
	
	for (int i=0; i<smooth; i++)
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				if (syncRand()&4)
				{
					int a=undermap[(y*w+x+1+s)&m];
					int b=undermap[(y*w+x-1+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				else
				{
					int a=undermap[(y*w+x+w+s)&m];
					int b=undermap[(y*w+x-w+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=undermap[(y*w+x+w+1+s)&m];
					int b=undermap[(y*w+x-w-1+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				else
				{
					int a=undermap[(y*w+x+w-1+s)&m];
					int b=undermap[(y*w+x-w+1+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=undermap[(y*w+x+w-2+s)&m];
					int b=undermap[(y*w+x-w+2+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				else
				{
					int a=undermap[(y*w+x+w-(h<<1)+s)&m];
					int b=undermap[(y*w+x-w+(h<<1)+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=undermap[(y*w+x+w+2+(h<<1)+s)&m];
					int b=undermap[(y*w+x-w-2-(h<<1)+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
				else
				{
					int a=undermap[(y*w+x+w+2-(h<<1)+s)&m];
					int b=undermap[(y*w+x-w-2+(h<<1)+s)&m];
					if (a==b)
					{
						undermap[y*w+x]=a;
						continue;
					}
				}
			}
	// What's finally in ?
	int waterCount=0;
	int sandCount =0;
	int grassCount=0;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
			switch (undermap[y*w+x])
			{
				case 0:
					waterCount++;
				continue;
				case 1:
					sandCount ++;
				continue;
				case 2:
					grassCount++;
				continue;
			}	
	int totalCount=waterCount+sandCount+grassCount;
	
	*finalWater=((double)waterCount)/((double)totalCount);
	*finalSand =((double)sandCount )/((double)totalCount);
	*finalGrass=((double)grassCount)/((double)totalCount);
}

void fastSimulateRandomMap(int smooth, double baseWater, double baseSand, double baseGrass, double *finalWater, double *finalSand, double *finalGrass)
{
	int n=smooth*2+1;
	VARARRAY(double,finalWaters,n);
	VARARRAY(double,finalSands,n);
	VARARRAY(double,finalGrasss,n);
	
	for (int i=0; i<n; i++)
		simulateRandomMap(4, baseWater, baseSand, baseGrass, &finalWaters[i], &finalSands[i], &finalGrasss[i]);
	
	double medianFinalWater=finalWaters[0];
	double medianFinalSand =finalSands [0];
	double medianFinalGrass=finalGrasss[0];
	for (int i=0; i<n; i++)
	{
		double wf=finalWaters[i];
		double sf=finalSands [i];
		double gf=finalGrasss[i];
		int ws=0;
		int wb=0;
		int we=0;
		int ss=0;
		int sb=0;
		int se=0;
		int gs=0;
		int gb=0;
		int ge=0;
		for (int j=0; j<n; j++)
		{
			if (wf<finalWaters[j])
				wb++;
			else if (wf>finalWaters[j])
				ws++;
			else
				we++;
			if (sf<finalSands[j])
				sb++;
			else if (sf>finalSands[j])
				ss++;
			else
				se++;
			if (gf<finalGrasss[j])
				gb++;
			else if (gf>finalGrasss[j])
				gs++;
			else
				ge++;
		}
		if (abs(wb-ws)<we)
			medianFinalWater=wf;
		if (abs(sb-ss)<se)
			medianFinalSand=sf;
		if (abs(gb-gs)<ge)
			medianFinalGrass=gf;
	}
	*finalWater=medianFinalWater;
	*finalSand=medianFinalSand;
	*finalGrass=medianFinalGrass;
}

bool Map::oldMakeRandomMap(MapGenerationDescriptor &descriptor)
{
	int waterRatio=descriptor.waterRatio;
	int sandRatio =descriptor.sandRatio ;
	int grassRatio=descriptor.grassRatio;
	int totalRatio=waterRatio+sandRatio+grassRatio;
	int smooth=descriptor.smooth;
	double baseWater, baseSand, baseGrass;
	
	if (totalRatio == 0)
	{
		baseWater = baseSand = baseGrass = 1.0/3.0;
	}
	else
	{
		baseWater=(float)waterRatio/(float)totalRatio;
		baseSand =(float)sandRatio /(float)totalRatio;
		baseGrass=(float)grassRatio/(float)totalRatio;
	}
	if (verbose)
		printf("makeRandomMap::old-base=(%f, %f, %f).\n", baseWater, baseSand, baseGrass);
	
	//Sorry, the equation is too complex for me. We uses a numeric aproach:
	double alphaWater=baseWater;
	double alphaSand =baseSand ;
	double alphaGrass=baseGrass;
	double alphaSum=alphaWater+alphaSand+alphaGrass;
	alphaWater/=alphaSum;
	alphaSand /=alphaSum;
	alphaGrass/=alphaSum;
	
	for (int r=1; r<=smooth; r++)
	{
		for (int prec=0; prec<3; prec++) 
		{
			double finalAlphaWater, finalAlphaSand, finalAlphaGrass;
			simulateRandomMap(r, alphaWater, alphaSand, alphaGrass, &finalAlphaWater, &finalAlphaSand, &finalAlphaGrass);
			
			
			double errAlphaWater=finalAlphaWater-baseWater;
			double errAlphaSand =finalAlphaSand -baseSand ;
			double errAlphaGrass=finalAlphaGrass-baseGrass;
			

			double betaWater;
			double betaSand ; 
			double betaGrass;
			
			if (finalAlphaWater)
				betaWater=(alphaWater*baseWater)/finalAlphaWater;
			else
				betaWater=0;
			if (finalAlphaSand)
				betaSand =(alphaSand *baseSand )/finalAlphaSand ;
			else
				betaSand=0;
			if (finalAlphaGrass)
				betaGrass=(alphaGrass*baseGrass)/finalAlphaGrass;
			else
				betaGrass=0;
			double betaSum=betaWater+betaSand+betaGrass;
			betaWater/=betaSum;
			betaSand /=betaSum;
			betaGrass/=betaSum;
			
			double finalBetaWater, finalBetaSand, finalBetaGrass;
			simulateRandomMap(r, betaWater, betaSand, betaGrass, &finalBetaWater, &finalBetaSand, &finalBetaGrass);
			
			
			double errBetaWater=finalBetaWater-baseWater;
			double errBetaSand =finalBetaSand -baseSand ;
			double errBetaGrass=finalBetaGrass-baseGrass;
			
			
			double projNom=(errBetaWater*errAlphaWater+errBetaSand*errAlphaSand+errBetaGrass*errAlphaGrass);
			double projDen=(errAlphaWater*errAlphaWater+errAlphaSand*errAlphaSand+errAlphaGrass*errAlphaGrass);
			if (projDen<=0)
				continue;
			double proj=projNom/projDen;
						
			
			double minErr=DBL_MAX;
			for (double cfi=0.0; cfi<=1.0; cfi+=0.1)
			{
				double cf=cfi*proj;
				
				double sumCenter=1.0-cf;
				double gammaWater=(-cf*betaWater+1.0*alphaWater)/sumCenter;
				double gammaSand =(-cf*betaSand +1.0*alphaSand )/sumCenter;
				double gammaGrass=(-cf*betaGrass+1.0*alphaGrass)/sumCenter;
				if (gammaWater<0.0)
					gammaWater=0.0;
				if (gammaSand<0.0)
					gammaSand=0.0;
				if (gammaGrass<0.0)
					gammaGrass=0.0;
				double gammaSum=betaWater+betaSand+betaGrass;
				if (gammaSum<=0)
					continue;
				gammaWater/=gammaSum;
				gammaSand /=gammaSum;
				gammaGrass/=gammaSum;

				double finalGammaWater, finalGammaSand, finalGammaGrass;
				simulateRandomMap(r, gammaWater, gammaSand, gammaGrass, &finalGammaWater, &finalGammaSand, &finalGammaGrass);
				

				double errGammaWater=finalGammaWater-baseWater;
				double errGammaSand =finalGammaSand -baseSand ;
				double errGammaGrass=finalGammaGrass-baseGrass;
				double errGamma=(errGammaWater*errGammaWater+errGammaSand*errGammaSand+errGammaGrass*errGammaGrass);
				
				if (errGamma<minErr)
				{
					minErr=errGamma;
					alphaWater=gammaWater;
					alphaSand =gammaSand;
					alphaGrass=gammaGrass;
				}
			}
		}
	}
	
	if (verbose)
		printf("makeRandomMap::new-base =(%f, %f, %f).\n", alphaWater, alphaSand, alphaGrass);
	
	double simWater, simSand, simGrass;
	simulateRandomMap(smooth, alphaWater, alphaSand, alphaGrass, &simWater, &simSand, &simGrass);
	if (verbose)
		printf("makeRandomMap::simulateRandomMap=(%f, %f, %f).\n", simWater, simSand, simGrass);
	
	totalRatio=0x7FFF;
	waterRatio=(int)(((double)alphaWater)*((double)totalRatio));
	sandRatio =(int)(((double)alphaSand )*((double)totalRatio));
	grassRatio=(int)(((double)alphaGrass)*((double)totalRatio));
	if (waterRatio<0)
		waterRatio=0;
	if (sandRatio<0)
		sandRatio=0;
	if (grassRatio<0)
		grassRatio=0;
	totalRatio=waterRatio+sandRatio+grassRatio;
	
	
	// First, we create a fully random patchwork:
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			int r=syncRand()%totalRatio;
			r-=waterRatio;
			if (r<0)
			{
				undermap[y*w+x]=WATER;
				continue;
			}
			r-=sandRatio;
			if (r<0)
			{
				undermap[y*w+x]=SAND;
				continue;
			}
			r-=grassRatio;
			if (r<0)
			{
				undermap[y*w+x]=GRASS;
				continue;
			}
			assert(false);//Want's to sing ?
		}
	
	// What's finally in ?
	int waterCount=0;
	int sandCount =0;
	int grassCount=0;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			switch (undermap[y*w+x])
			{
				case WATER:
					waterCount++;
				continue;
				case SAND :
					sandCount ++;
				continue;
				case GRASS:
					grassCount++;
				continue;
			}
		}
	double totalCount=(double)(waterCount+sandCount+grassCount);
	if (verbose)
		printf("makeRandomMap::beforeCount=(%f, %f, %f).\n", waterCount/totalCount, sandCount/totalCount, grassCount/totalCount);
	
	for (int i=0; i<smooth; i++)
	{
		// What's in now?
		waterCount=0;
		sandCount =0;
		grassCount=0;
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				switch (undermap[y*w+x])
				{
					case WATER:
						waterCount++;
					continue;
					case SAND :
						sandCount ++;
					continue;
					case GRASS:
						grassCount++;
					continue;
				}
			}
		double totalRatioCount=(double)(waterCount+sandCount+grassCount);
		double waterRatioCount=waterCount/totalRatioCount;
		double sandRatioCount =sandCount /totalRatioCount;
		double grassRatioCount=grassCount/totalRatioCount;
		
		double errWaterRatioCount=waterRatioCount-baseWater;
		double errSandRatioCount =sandRatioCount -baseSand ;
		double errGrassRatioCount=grassRatioCount-baseGrass;
		
		Uint32 allowed[3];
		if (errWaterRatioCount>0)
			allowed[0]=(Uint32)(pow(errWaterRatioCount, 0.125)*4294967296.0);
		else
			allowed[0]=0;
		if (errSandRatioCount>0)
			allowed[1]=(Uint32)(pow(errSandRatioCount , 0.125)*4294967296.0);
		else
			allowed[1]=0;
		if (errGrassRatioCount>0)
			allowed[2]=(Uint32)(pow(errGrassRatioCount, 0.125)*4294967296.0);
		else
			allowed[2]=0;
		
		assert(allowed[0]<=(Uint32)0xFFFFFFFF);
		assert(allowed[1]<=(Uint32)0xFFFFFFFF);
		assert(allowed[2]<=(Uint32)0xFFFFFFFF);
		
		if (i==0)
		{
			allowed[0]=0;
			allowed[1]=0;
			allowed[2]=0;
		}
		
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				if (syncRand()&4)
				{
					int a=getUMTerrain(x+1, y);
					int b=getUMTerrain(x-1, y);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int a=getUMTerrain(x, y-1);
					int b=getUMTerrain(x, y+1);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=getUMTerrain(x+1, y+1);
					int b=getUMTerrain(x-1, y-1);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int a=getUMTerrain(x+1, y-1);
					int b=getUMTerrain(x-1, y+1);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=getUMTerrain(x+2, y);
					int b=getUMTerrain(x-2, y);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int a=getUMTerrain(x, y-2);
					int b=getUMTerrain(x, y+2);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				if (syncRand()&4)
				{
					int a=getUMTerrain(x+2, y+2);
					int b=getUMTerrain(x-2, y-2);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
				else
				{
					int a=getUMTerrain(x+2, y-2);
					int b=getUMTerrain(x-2, y+2);
					if ((a==b)&&(allowed[a]<=syncRand()))
					{
						setUMTerrain(x, y, (TerrainType)a);
						continue;
					}
				}
			}
	}
	// What's finally in ?
	waterCount=0;
	sandCount =0;
	grassCount=0;
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
		{
			switch (undermap[y*w+x])
			{
				case WATER:
					waterCount++;
				continue;
				case SAND :
					sandCount ++;
				continue;
				case GRASS:
					grassCount++;
				continue;
			}
		}
	totalCount=(double)(waterCount+sandCount+grassCount);
	if (verbose)
		printf("makeRandomMap::final count=(%f, %f, %f).\n", waterCount/totalCount, sandCount/totalCount, grassCount/totalCount);
	
	controlSand();
	
	//Now, we have to find suitable places for teams:
	int nbTeams=descriptor.nbTeams;
	int minDistSquare=(int)((double)((double)w*(double)h*(double)grassCount)/(double)((double)nbTeams*(double)totalCount));
	if (verbose)
		printf("minDistSquare=%d (%f).\n", minDistSquare, sqrt((double)minDistSquare));
	if (minDistSquare<=0)
		return false;
	assert(minDistSquare>0);
	int* bootX=descriptor.bootX;
	int* bootY=descriptor.bootY;
	
	//TODO: First pass to find the number of available places.
	for (int team=0; team<nbTeams; team++)
	{
		int maxSurface=0;
		int maxX=0;
		int maxY=0;
		for (int y=0; y<h; y++)
		{
			int width=0;
			int startx=0;
			for (int x=0; x<w; x++)
			{
				int a=undermap[y*w+x];
				if (a==GRASS)
					width++;
				else
				{
					if (width>7)
					{
						int centerx=((x+startx)>>1);
						int top, bot;
						for (top=0; top<h; top++)
							if (getUMTerrain(centerx, y-top)!=GRASS)
								break;
						for (bot=0; bot<h; bot++)
							if (getUMTerrain(centerx, y+bot)!=GRASS)
								break;
						int height=top+bot-1;
						int surface=height*width;
						assert(surface>0);
						
						int centery=y+((bot-top)>>1);
						bool farEnough=true;
						for (int ti=0; ti<team; ti++)
							if (warpDistSquare(centerx, centery, bootX[ti], bootY[ti])<minDistSquare)
							{
								farEnough=false;
								break;
							}
						
						if (surface>maxSurface && farEnough)
						{
							maxSurface=surface;
							maxX=centerx;
							maxY=centery;
						}
					}
					width=0;
					startx=x;
				}
			}
		}
		
		if (maxSurface<=0)
			return false;
		assert(maxSurface);
		bootX[team]=maxX;
		bootY[team]=maxY;
		
		for (int dx=-1; dx<6; dx++)
			for (int dy=0; dy<6; dy++)
				setUMTerrain(maxX+dx, maxY+dy, GRASS);
		
		
	}
	
	// Let's add some green space for teams:
	int squareSize=5+(int)(sqrt((double)minDistSquare)/4.5);
	if (verbose)
		printf("squareSize=%d.\n", squareSize);
	for (int team=0; team<nbTeams; team++)
	{
		setUMatPos(descriptor.bootX[team]+2, descriptor.bootY[team]+0, GRASS, squareSize);
		setUMatPos(descriptor.bootX[team]+2, descriptor.bootY[team]+2, GRASS, squareSize);
	}
	
	controlSand();
	regenerateMap(0, 0, w, h);
	
	if (verbose)
		printf("makeRandomMap::success\n");
	return true;
}

/// This random map generator generates a heightfield and then choses levels at wich to draw the line between water, sand, gras and sand again (desert)
bool Map::makeRandomMap(MapGenerationDescriptor &descriptor)
{
	/// all under waterLevel is water, under sandLevel is beach, under grassLevel is grass and above grasslevel is desert
	float waterLevel, sandLevel, grassLevel, wheatWoodLevel, algaeLevel, stoneLevel;
	/// to influence the roughnes
	float smoothingFactor=(float)(descriptor.smooth+4)*3;;
	/// the proportions requested through the gui can directly be translated into tile counts of the undermap.
	unsigned int waterTiles, sandTiles, grassTiles, wheatWoodTiles, algaeTiles;
	/// grass + sand + water + desert as from the gui
	unsigned int totalGSWFromUI=descriptor.waterRatio+descriptor.sandRatio+descriptor.grassRatio+descriptor.desertRatio+descriptor.fruitRatio;
	/// respect symmetry-requirements
	unsigned int wPower2Divider=0, hPower2Divider=0;
	int power2Divider=descriptor.logRepeatAreaTimes;
	for (int i = 0; i<power2Divider; i++)
		if (w/(pow(2,wPower2Divider))>h/(pow(2,hPower2Divider)))
			wPower2Divider++;
		else
			hPower2Divider++;
	unsigned int wHeightMap=(unsigned int)(w/(pow(2,wPower2Divider)));
	unsigned int hHeightMap=(unsigned int)(h/(pow(2,hPower2Divider)));
	/// lets generate a patch of perlin noise. That's a smooth mapping R^2 to ]0;1[
	HeightMap hm(wHeightMap,hHeightMap);
	/// 1 to avoid division by zero, 
	unsigned int tmpTotal=1+descriptor.waterRatio+descriptor.grassRatio;
	unsigned int sectionIslandCount=static_cast<unsigned int>((descriptor.nbTeams+descriptor.extraIslands)/pow(2,power2Divider));
	switch (descriptor.methode)
	{
		case MapGenerationDescriptor::eSWAMP:
			hm.makeSwamp(smoothingFactor);
			waterTiles=(unsigned int)((float)descriptor.waterRatio*wHeightMap*hHeightMap/(float)tmpTotal);
			sandTiles=0;
			grassTiles=wHeightMap*hHeightMap-waterTiles;
			break;
		case MapGenerationDescriptor::eRIVER:
			hm.makeRiver(descriptor.riverDiameter*(wHeightMap+hHeightMap)/2/100,smoothingFactor);
			waterTiles=(unsigned int)((float)descriptor.waterRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			sandTiles=(unsigned int)((float)descriptor.sandRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			grassTiles =(unsigned int)((float)descriptor.grassRatio /(float)totalGSWFromUI*wHeightMap*hHeightMap);
			break;
		case MapGenerationDescriptor::eCRATERLAKES:
			hm.makeCraters(wHeightMap*hHeightMap*descriptor.craterDensity/30000, 30, smoothingFactor);
			waterTiles=(unsigned int)((float)descriptor.waterRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			sandTiles=(unsigned int)((float)descriptor.sandRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			grassTiles =(unsigned int)((float)descriptor.grassRatio /(float)totalGSWFromUI*wHeightMap*hHeightMap);
			break;
		case MapGenerationDescriptor::eISLANDS:
			hm.makeIslands(sectionIslandCount, smoothingFactor);
			waterTiles=(unsigned int)((float)descriptor.waterRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			sandTiles=(unsigned int)((float)descriptor.sandRatio/(float)totalGSWFromUI*wHeightMap*hHeightMap);
			grassTiles =(unsigned int)((float)descriptor.grassRatio /(float)totalGSWFromUI*wHeightMap*hHeightMap);
			break;
		default: assert(false);
			break;
	}
	/// wheat/wood needs ground to stand on and water. So:
	wheatWoodTiles=waterTiles<grassTiles?waterTiles/2:grassTiles/2;
	algaeTiles=waterTiles/6;

	/// histogram[i] collects the count of all terrain levels == i
	int histogram[2048];
	memset(histogram, 0, 2048*sizeof(int));

	for (unsigned i=0; i<wHeightMap*hHeightMap; i++)
	{
		histogram[hm.uiLevel(i,2048)]++;
	}
	unsigned int accumulatedHistogram=0;
	int i=0;
	waterLevel=0;
	sandLevel=0;
	grassLevel=0;
	wheatWoodLevel=0;
	stoneLevel=0;
	algaeLevel=0;	
	while ((waterLevel==0) & (i<2048))
	{
		accumulatedHistogram+=histogram[i++];
		if (algaeLevel==0 && accumulatedHistogram >= algaeTiles)
			algaeLevel = (float)(i-1)/2048.0;
		if (accumulatedHistogram >= waterTiles)
			waterLevel = (float)(i-1)/2048.0;
	}
	while ((sandLevel==0) && (i<2048))
	{
		accumulatedHistogram+=histogram[i++];
		if (accumulatedHistogram >= waterTiles+sandTiles)
			sandLevel = (float)(i-1)/2048.0;
	}
	while ((grassLevel==0) && (i<2048))
	{
		accumulatedHistogram+=histogram[i++];
		if (wheatWoodLevel==0 && accumulatedHistogram >= waterTiles+sandTiles+wheatWoodTiles)
			wheatWoodLevel = (float)(i-1)/2048.0;		
		if (stoneLevel==0 && accumulatedHistogram >= waterTiles+sandTiles+(wheatWoodTiles / 3))
			stoneLevel = (float)(i-1)/2048.0;	
		if (accumulatedHistogram >= waterTiles+sandTiles+grassTiles)
			grassLevel = (float)(i-1)/2048.0;
	}
	for (unsigned y=0; y<hHeightMap; y++)
		for (unsigned x=0; x<wHeightMap; x++)
			{
			int tmpUndermap;
			if (hm(y*wHeightMap+x)<waterLevel)
				tmpUndermap=WATER;
			else if (hm(y*wHeightMap+x)<sandLevel)
				tmpUndermap=SAND;
			else if (hm(y*wHeightMap+x)<grassLevel)
				tmpUndermap=GRASS;
			else
				tmpUndermap=SAND;
			for (int yRepeat=0; yRepeat<pow(2,hPower2Divider); yRepeat++)
				for (int xRepeat=0; xRepeat<pow(2,wPower2Divider); xRepeat++)
					undermap[xRepeat*wHeightMap+x+(yRepeat*hHeightMap+y)*w]=tmpUndermap;
			}
	controlSand();
	
	//Now, we have to find suitable places for teams:
	int nbTeams=descriptor.nbTeams;
	int minDistSquare=(int)((double)w*h/(double)nbTeams/5);
	std::cout << "minDistSquare=" << minDistSquare << " (" << sqrt((double)minDistSquare) << ").\n";
	if (minDistSquare<=0)
	{
		std::cout << "debugoutput 1\n";
		return false;
	}
	assert(minDistSquare>0);
	int* bootX=descriptor.bootX;
	int* bootY=descriptor.bootY;
	
	//TODO: First pass to find the number of available places.
	for (int team=0; team<nbTeams; team++)
	{
		int maxSurface=0;
		int maxX=0;
		int maxY=0;
		for (int y=0; y<h; y++)
		{
			int width=0;
			int startx=0;
			for (int x=0; x<w; x++)
			{
				int a=undermap[y*w+x];
				if (a==GRASS)
					width++;
				else
				{
					if (width>7)
					{
						int centerx=((x+startx)>>1);
						int top, bot;
						for (top=0; top<h; top++)
							if (getUMTerrain(centerx, y-top)!=GRASS)
								break;
						for (bot=0; bot<h; bot++)
							if (getUMTerrain(centerx, y+bot)!=GRASS)
								break;
						int height=top+bot-1;
						int surface=height*width;
						assert(surface>0);
						
						int centery=y+((bot-top)>>1);
						bool farEnough=true;
						for (int ti=0; ti<team; ti++)
							if (warpDistSquare(centerx, centery, bootX[ti], bootY[ti])<minDistSquare)
							{
								farEnough=false;
								break;
							}
						
						if (surface>maxSurface && farEnough)
						{
							maxSurface=surface;
							maxX=centerx;
							maxY=centery;
						}
					}
					width=0;
					startx=x;
				}
			}
		}
		
		if (maxSurface<=0)
		{
			std::cout << "debugoutput 2\n";
			return false;
		}
		assert(maxSurface);
		bootX[team]=maxX;
		bootY[team]=maxY;
	}
	
	controlSand();
	regenerateMap(0, 0, w, h);
	//now to add primary resources for current map generator
	for (unsigned y=0; y<hHeightMap; y++)
		for (unsigned x=0; x<wHeightMap; x++)
			{
			int tmpRessource=-12;//sorry! is there some NONE?
			if(hm(x+wHeightMap*y)<algaeLevel)
				tmpRessource=ALGA;
			//following places stone next to sand & water and keeps wheat & wood more inland without clogging up the interior too badly
			else if(hm(x+wHeightMap*y) < stoneLevel)
					tmpRessource=STONE;
			else if(hm(x+wHeightMap*y)<wheatWoodLevel)
				//patch to get smooth areas of wheat and wood:
				//if the map is ascending at x+w/2,y set wheat. else set wood
				if(hm((x+wHeightMap/2)%wHeightMap+wHeightMap*y)<hm((x+wHeightMap/2+1)%wHeightMap+wHeightMap*y))
					tmpRessource=CORN;
				else
					tmpRessource=WOOD;
			if (tmpRessource!=-12)
				for (int yRepeat=0; yRepeat<pow(2,hPower2Divider); yRepeat++)
					for (int xRepeat=0; xRepeat<pow(2,wPower2Divider); xRepeat++)
						setRessource(xRepeat*wHeightMap+x,yRepeat*hHeightMap+y,tmpRessource,1);
			}
/*	for(int x=0; x<w; x++)
	{
		for (int y=0; y<h; y++)
		{
		if(hm(x+w*y)<algaeLevel)
			setRessource(x,y, ALGA, 1);
		//following places stone next to sand & water and keeps wheat & wood more inland without clogging up the interior too badly
		else if(hm(x+w*y)<wheatWoodLevel)
			//patch to get smooth areas of wheat and wood:
			//if the map is ascending at x+w/2,y set wheat. else set wood
			if(hm((x+w/2)%w+w*y)<hm((x+w/2+1)%w+w*y))
				setRessource(x,y,CORN,1);
				else setRessource(x,y,WOOD,1);
		if(hm(x+w*y) < stoneLevel)
			setRessource(x,y,STONE,1);
		}
	}*/
	//TODO: count of groves(=descriptor.fruitRatio) does not scale with mapsize.
	//so it has to be adjusted higher on bigger maps now.
	//TODO: is the use of syncrand obligatory in mapgeneration?
	srand((unsigned)time(NULL));
	//fruit-placement:
	if (descriptor.fruitRatio > 0)
	{
		for (int q1=0; q1<descriptor.fruitRatio; q1++) //counting groves
		{
			//choose fruit
			int fruit;
			switch (rand()%3)
			{
				case 0: fruit = CHERRY; break;
				case 1: fruit = ORANGE; break;
				case 2: fruit = PRUNE; break;
				default: fruit = PRUNE; break;
			}
			//choose coordinate where there is grass but no ressource yet
			int x, y;
			do
			{
				x=(rand()%wHeightMap);
				y=(rand()%hHeightMap);
			} while (getUMTerrain(x, y)!=GRASS || isRessource(x,y));
			//choose size of grove (tree count)
			int grovesize=(rand()%10)+1;
			for (int i=0; i<grovesize; i++)
			{
				for (int yRepeat=0; yRepeat<pow(2,hPower2Divider); yRepeat++)
					for (int xRepeat=0; xRepeat<pow(2,wPower2Divider); xRepeat++)
						setRessource(xRepeat*wHeightMap+x,yRepeat*hHeightMap+y,fruit,1);
				//find a valid neighbor of actual coordinate
				for (int iTry=0; iTry<100; iTry++)
				{
					int xnew=x+rand()%3-1;
					int ynew=y+rand()%3-1;
					if(getUMTerrain(xnew, ynew)==GRASS && !isRessource(xnew,ynew))
					{
						x=xnew;
						y=ynew;
						break;
					}
				}
			}
		}
	}
	if (verbose)
		printf("makeRandomMap::success\n");
	return true;
}

void Map::oldAddRessourcesRandomMap(MapGenerationDescriptor &descriptor)
{
	int *bootX=descriptor.bootX;
	int *bootY=descriptor.bootY;
	int nbTeams=descriptor.nbTeams;
	int limiteDist=(w+h)/(2*nbTeams);
	
	// let's add ressources to old map generator
	for (int team=0; team<nbTeams; team++)
	{
		int smallestWidth=limiteDist;
		int smallestRessource=0;
		
		bool dirUsed[8];
		for (int i=0; i<8; i++)
			dirUsed[i]=false;
		int ressOrder[4];
		ressOrder[0]=CORN;
		ressOrder[1]=WOOD;
		ressOrder[2]=STONE;
		ressOrder[3]=CORN;
		
		int distWeight[4];
		distWeight[0]=1;
		distWeight[1]=1;
		distWeight[2]=1;
		distWeight[3]=2;
		
		int widthWeight[4];
		widthWeight[0]=1;
		widthWeight[1]=1;
		widthWeight[2]=1;
		widthWeight[3]=2;
		
		for (int resi=0; resi<4; resi++)
		{
			int ress=ressOrder[resi];
			int maxDir=0;
			int maxWidth=0;
			int maxDist=0;
			for (int dir=0; dir<8; dir++)
				if (!dirUsed[dir])
				{
					int width=0;
					int dx, dy, dist;
					Unit::dxdyfromDirection(dir, &dx, &dy);
					for (dist=5; dist<limiteDist; dist++)
						if (isGrass(bootX[team]+dx*dist, bootY[team]+dy*dist))
							width++;
						else if (width>3)
							break;
						else
							width=1;

					if (dist*distWeight[resi]+width*widthWeight[resi]>maxDist*distWeight[resi]+maxWidth*widthWeight[resi])
					{
						maxWidth=width;
						maxDist=dist;
						maxDir=dir;
					}
				}
			dirUsed[maxDir]=true;
			if (maxWidth<smallestWidth)
			{
				smallestWidth=maxWidth;
				smallestRessource=ress;
			}
			
			int dx, dy;
			Unit::dxdyfromDirection(maxDir, &dx, &dy);
			int d=maxDist-(maxWidth>>1);
			dx*=d;
			dy*=d;
			
			int amount=descriptor.ressource[ress];
			if (amount>0)
				setRessource(bootX[team]+dx, bootY[team]+dy, ress, amount);
		}

		if (smallestWidth<limiteDist)
		{
			int maxDir=0;
			int maxWidth=0;
			int maxDist=0;
			for (int dir=0; dir<8; dir++)
				if (!dirUsed[dir])
				{
					int width=0;
					int dx, dy, dist;
					Unit::dxdyfromDirection(dir, &dx, &dy);
					for (dist=0; dist<2*limiteDist; dist++)
						if (isGrass(bootX[team]+dx*dist, bootY[team]+dy*dist))
							width++;
						else if (width>3)
							break;
						else
							width=1;

					if (dist+width>maxDist+maxWidth)
					{
						maxWidth=width;
						maxDist=dist;
						maxDir=dir;
					}
				}
			dirUsed[maxDir]=true;
			
			int dx, dy;
			Unit::dxdyfromDirection(maxDir, &dx, &dy);
			int d=maxDist-(maxWidth>>1);
			dx*=d;
			dy*=d;
			
			int amount=descriptor.ressource[smallestRessource];
			if (amount>0)
				setRessource(bootX[team]+dx, bootY[team]+dy, smallestRessource, amount);
		}

		int maxDir=0;
		int maxWidth=0;
		int maxDist=0;
		for (int dir=0; dir<8; dir++)
		{
			int width=0;
			int dx, dy, dist;
			Unit::dxdyfromDirection(dir, &dx, &dy);
			for (dist=0; dist<2*limiteDist; dist++)
				if (isWater(bootX[team]+dx*dist, bootY[team]+dy*dist))
					width++;
				else if (width>3)
					break;
				else
					width=1;
				
			if (dist+width>width+maxWidth)
			{
				maxWidth=width;
				maxDist=dist;
				maxDir=dir;
			}
		}
		
		int dx, dy;
		Unit::dxdyfromDirection(maxDir, &dx, &dy);
		int d=maxDist-(maxWidth>>1);
		dx*=d;
		dy*=d;
		
		int amount=descriptor.ressource[ALGA];
		if (amount>0)
			setRessource(bootX[team]+dx, bootY[team]+dy, ALGA, amount);
	}
	
	// Let's smooth ressources...
	int maxAmount=0;
	for (int r=0; r<4; r++)
		if (maxAmount<descriptor.ressource[r])
			maxAmount=descriptor.ressource[r];
	smoothRessources(maxAmount*3);
}

bool Map::oldMakeIslandsMap(MapGenerationDescriptor &descriptor)
{
	// First, fill with water:
	for (int y=0; y<h; y++)
		for (int x=0; x<w; x++)
			undermap[y*w+x]=WATER;
		
	// Two, plants "bootstraps"
	int* bootX=descriptor.bootX;
	int* bootY=descriptor.bootY;
	int nbIslands=descriptor.nbTeams;
	int islandsSize=(int)(((w+h)*descriptor.oldIslandSize)/(400.0*sqrt((double)nbIslands)));
	if (islandsSize<8)
		islandsSize=8;
	int minDistSquare=(w*h)/nbIslands;
	
	
	int c=0;
	for (int i=0; i<nbIslands; i++)
	{
		int x=syncRand()%w;
		int y=syncRand()%h;
		bool failed=false;
		int j;
		for (j=0; j<i; j++)
			if (warpDistSquare(x, y, bootX[j], bootY[j])<minDistSquare)
			{
				failed=true;
				break;
			}
		if (failed)
		{
			
			i--;
			if (c++>65536)
			{
				minDistSquare=minDistSquare>>1;
				//I think that you need to do this only once, in worst case.
				//With a few luck you doesn't need to.
				c=0;
				
			}
		}
		else
		{
			bootX[i]=x;
			bootY[i]=y;
			for (int dx=-1; dx<6; dx++)
				for (int dy=0; dy<6; dy++)
					setUMTerrain(x+dx, y+dy, GRASS);
		}
	}
	
	
	
	// Three, expands islands
	for (int s=0; s<islandsSize; s++)
	{
		for (int y=0; y<h; y+=2)
			for (int x=0; x<w; x+=2)
			{
				TerrainType umt=getUMTerrain(x, y);
				if (umt==GRASS)
					continue;
				
				int a, b;
				switch (syncRand()&15)
				{
				case 0:
					a=getUMTerrain(x+1, y);
					b=getUMTerrain(x-1, y);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 1:
					a=getUMTerrain(x, y-1);
					b=getUMTerrain(x, y+1);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 2:
					a=getUMTerrain(x+1, y+1);
					b=getUMTerrain(x-1, y-1);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 3:
					a=getUMTerrain(x+1, y-1);
					b=getUMTerrain(x-1, y+1);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 4:
					a=getUMTerrain(x+2, y);
					b=getUMTerrain(x-2, y);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 5:
					a=getUMTerrain(x, y-2);
					b=getUMTerrain(x, y+2);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 6:
					a=getUMTerrain(x+2, y+2);
					b=getUMTerrain(x-2, y-2);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 7:
					a=getUMTerrain(x+2, y-2);
					b=getUMTerrain(x-2, y+2);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				default:
				break;
				}
			}
		for (int y=1; y<h; y+=2)
			for (int x=1; x<w; x+=2)
			{
				TerrainType umt=getUMTerrain(x, y);
				if (umt==GRASS)
					continue;
				
				int a, b;
				switch (syncRand()&15)
				{
				case 0:
					a=getUMTerrain(x+1, y);
					b=getUMTerrain(x-1, y);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 1:
					a=getUMTerrain(x, y-1);
					b=getUMTerrain(x, y+1);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 2:
					a=getUMTerrain(x+1, y+1);
					b=getUMTerrain(x-1, y-1);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 3:
					a=getUMTerrain(x+1, y-1);
					b=getUMTerrain(x-1, y+1);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 4:
					a=getUMTerrain(x+2, y);
					b=getUMTerrain(x-2, y);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 5:
					a=getUMTerrain(x, y-2);
					b=getUMTerrain(x, y+2);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 6:
					a=getUMTerrain(x+2, y+2);
					b=getUMTerrain(x-2, y-2);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				case 7:
					a=getUMTerrain(x+2, y-2);
					b=getUMTerrain(x-2, y+2);
					if ((a==GRASS)||(b==GRASS))
					{
						setUMTerrain(x, y, GRASS);
						continue;
					}
				break;
				default:
				break;
				}
			}
	}
	
	// Four, avoid too much sand. Let's smooth
	for (int s=0; s<2; s++)
		for (int y=0; y<h; y++)
			for (int x=0; x<w; x++)
			{
				int a, b;
				a=getUMTerrain(x+1, y);
				b=getUMTerrain(x-1, y);
				if ((a==GRASS)&&(b==GRASS))
				{
					setUMTerrain(x, y, GRASS);
					continue;
				}
				a=getUMTerrain(x, y-1);
				b=getUMTerrain(x, y+1);
				if ((a==GRASS)&&(b==GRASS))
				{
					setUMTerrain(x, y, GRASS);
					continue;
				}
				a=getUMTerrain(x+1, y+1);
				b=getUMTerrain(x-1, y-1);
				if ((a==GRASS)&&(b==GRASS))
				{
					setUMTerrain(x, y, GRASS);
					continue;
				}
				a=getUMTerrain(x+1, y-1);
				b=getUMTerrain(x-1, y+1);
				if ((a==GRASS)&&(b==GRASS))
				{
					setUMTerrain(x, y, GRASS);
					continue;
				}
			}
	
	controlSand();
	
	// Five, add some sand
	for (int s=0; s<descriptor.oldBeach; s++)
		for (int dy=0; dy<4; dy++)
			for (int dx=0; dx<4; dx++)
				for (int y=dy; y<h; y+=4)
					for (int x=dx; x<w; x+=4)
					{
						int a, b;
						switch (syncRand()&7)
						{
						case 0:
							a=getUMTerrain(x+1, y);
							b=getUMTerrain(x-1, y);
							if ((a==SAND)&&(b==WATER)||(a==WATER)&&(b==SAND))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						case 1:
							a=getUMTerrain(x, y-1);
							b=getUMTerrain(x, y+1);
							if ((a==SAND)&&(b==WATER)||(a==WATER)&&(b==SAND))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						case 2:
							a=getUMTerrain(x+1, y+1);
							b=getUMTerrain(x-1, y-1);
							if ((a==SAND)&&(b==WATER)||(a==WATER)&&(b==SAND))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						case 3:
							a=getUMTerrain(x+1, y-1);
							b=getUMTerrain(x-1, y+1);
							if ((a==SAND)&&(b==WATER)||(a==WATER)&&(b==SAND))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						
						
						case 4:
							a=getUMTerrain(x+1, y);
							b=getUMTerrain(x-1, y);
							if ((a==SAND)&&(b==SAND))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						case 5:
							a=getUMTerrain(x, y-1);
							b=getUMTerrain(x, y+1);
							if ((a==SAND)&&(b==SAND))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						case 6:
							a=getUMTerrain(x+1, y+1);
							b=getUMTerrain(x-1, y-1);
							if ((a==SAND)&&(b==SAND))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						case 7:
							a=getUMTerrain(x+1, y-1);
							b=getUMTerrain(x-1, y+1);
							if ((a==SAND)&&(b==SAND))
							{
								setUMTerrain(x, y, SAND);
								continue;
							}
						break;
						}
				}
	
	
	//controlSand();
	regenerateMap(0, 0, w, h);
	return true;
}

void Map::oldAddRessourcesIslandsMap(MapGenerationDescriptor &descriptor)
{
	int *bootX=descriptor.bootX;
	int *bootY=descriptor.bootY;
	
	int islandsSize=(int)(((w+h)*descriptor.oldIslandSize)/(400.0*sqrt((double)descriptor.nbTeams)));
	if (islandsSize<8)
		islandsSize=8;
	// let's add ressources...
	int smoothRessources=islandsSize/4;
	for (int s=0; s<descriptor.nbTeams; s++)
	{
		int d, p, amount;
		int smallestAmount;
		int smallestRessource;

		//WOOD
		for (d=0; d<islandsSize; d++)
			if (!isGrass(bootX[s], bootY[s]-d))
				break;
		amount=descriptor.ressource[WOOD];
		amount=d-smoothRessources-2;
		if (amount<1)
			amount=1;
		p=d-1-amount/2;
		if (amount>0)
			setRessource(bootX[s], bootY[s]-p, WOOD, amount);
		smallestAmount=amount;
		smallestRessource=WOOD;
		
		//WHEAT
		for (d=0; d<islandsSize; d++)
			if (!isGrass(bootX[s]-d, bootY[s]))
				break;
		amount=descriptor.ressource[CORN];
		amount=d-smoothRessources-0;
		if (amount<1)
			amount=1;
		p=d-1-amount/2;
		if (amount>0)
			setRessource(bootX[s]-p, bootY[s], CORN, amount);
		if (amount<smallestAmount)
		{
			smallestAmount=amount;
			smallestRessource=CORN;
		}
		
		//STONE
		for (d=0; d<islandsSize; d++)
			if (!isGrass(bootX[s], bootY[s]+d))
				break;
		setRessource(bootX[s], bootY[s]+p, STONE, 1);

		//We add the ressource with the smallest amount:
		for (d=0; d<islandsSize; d++)
			if (!isGrass(bootX[s]+d, bootY[s]+d))
				break;
		amount=descriptor.ressource[smallestRessource];
		amount=d-smoothRessources-3;
		if (amount<1)
			amount=1;
		p=d-1-amount/2;
		if (amount>0)
			setRessource(bootX[s]+p, bootY[s]+p, smallestRessource, amount);
		
		//ALGAE
		for (d=0; d<2*islandsSize; d++)
			if (isWater(bootX[s]+d, bootY[s]))
				break;
		amount=descriptor.ressource[ALGA];
		amount=smoothRessources;
		p=d+smoothRessources-1+amount/2;
		if (amount>0)
			setRessource(bootX[s]+p, bootY[s], ALGA, amount);
	}

	// Let's smooth ressources...
	this->smoothRessources(smoothRessources*2);
}

bool Game::oldMakeIslandsMap(MapGenerationDescriptor &descriptor)
{
	for (int s=0; s<descriptor.nbTeams; s++)
	{
		if (mapHeader.getNumberOfTeams()<=s)
			addTeam();
		int squareSize=5+descriptor.oldIslandSize/10;
		map.setUMatPos(descriptor.bootX[s]+2, descriptor.bootY[s]+0, GRASS, squareSize);
		map.setUMatPos(descriptor.bootX[s]+2, descriptor.bootY[s]+2, GRASS, squareSize);
		
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
		if (!checkRoomForBuilding(descriptor.bootX[s], descriptor.bootY[s], globalContainer->buildingsTypes.get(typeNum), -1, false))
		{
			if (verbose)
				printf("Failed to add swarm of team %d\n", s);
			return false;
		}
		teams[s]->startPosX=descriptor.bootX[s];
		teams[s]->startPosY=descriptor.bootY[s];
		Building *b=addBuilding(descriptor.bootX[s], descriptor.bootY[s], typeNum, s);
		assert(b);
		for (int i=0; i<descriptor.nbWorkers; i++)
			if (addUnit(descriptor.bootX[s]+(i%4), descriptor.bootY[s]-1-(i/4), s, WORKER, 0, 0, 0, 0)==NULL)
			{
				if (verbose)
					printf("Failed to add unit %d of team %d\n", i, s);
				return false;
			}
		teams[s]->createLists();
	}
	map.smoothRessources(descriptor.oldIslandSize/10);
	return true;
}

bool Game::makeRandomMap(MapGenerationDescriptor &descriptor)
{
	for (int s=0; s<descriptor.nbTeams; s++)
	{
		assert(mapHeader.getNumberOfTeams()==s);
		if (mapHeader.getNumberOfTeams()<=s)
			addTeam();
		
		map.setUMatPos(descriptor.bootX[s]+2, descriptor.bootY[s]+0, GRASS, 5);
		map.setUMatPos(descriptor.bootX[s]+2, descriptor.bootY[s]+2, GRASS, 5);
		map.setNoRessource(descriptor.bootX[s]+2, descriptor.bootY[s]+0, 5);
		map.setNoRessource(descriptor.bootX[s]+2, descriptor.bootY[s]+2, 5);
		
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("swarm", 0, false);
		if (!checkRoomForBuilding(descriptor.bootX[s], descriptor.bootY[s], globalContainer->buildingsTypes.get(typeNum), s, false))
		{
			if (verbose)
				printf("Failed to add swarm of team %d\n", s);
			return false;
		}
		teams[s]->startPosX=descriptor.bootX[s];
		teams[s]->startPosY=descriptor.bootY[s];
		Building *b=addBuilding(descriptor.bootX[s], descriptor.bootY[s], typeNum, s);
		assert(b);
		for (int i=0; i<descriptor.nbWorkers; i++)
			if (addUnit(descriptor.bootX[s]+(i%4), descriptor.bootY[s]-1-(i/4), s, WORKER, 0, 0, 0, 0)==NULL)
			{
				if (verbose)
					printf("Failed to add unit %d of team %d\n", i, s);
				return false;
			}
		teams[s]->createLists();
	}
	return true;
}

