// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <math.h>
#include <time.h>
#include <stdlib.h>

//also the Perlin Noise stuff uses random that is not based on syncRand
#include "Game.h"
#include "MapGenerationDescriptor.h"
#include "MapGenerator.h"
#include "Map.h"
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
		case MapGenerationDescriptor::eISLES:
			if (!computeIsles(game, descriptor))
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
	game.sgslScript.compileScript(&game);
	
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
	
	// Initially divide up the land
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
	
	// Use the heightmap to put in water, grass, and sand
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
		
		// Divide the area. Its possible the area will be so small it can't be used
		if(divideUpArea(game, grid, islandAreaNumbers[i], areaWeights, areaNumbers))
		{
			// Fill in wheat
			std::vector<MapGeneratorPoint> points;
			getAllPoints(game, grid, areaNumbers[0], points);
			fillInResource(game, points, CORN, 2);
			points.clear();
			
			// Place some fruit
			int fruit_n = syncRand()%6+1;
			getAllPoints(game, grid, areaNumbers[1], points);
			chooseRandomPoints(game, points, fruit_n);
			for(unsigned int j=0; j<points.size(); ++j)
			{
				game.map.setRessource(points[j].x, points[j].y, CHERRY + syncRand()%3, 1);
			}
		}
	}
	
	if(!divideUpPlayerLands(game, descriptor, grid, teamAreaNumbers, areaNumber))
		return false;
	
	// Initialize final team info
	for(int i=0; i<descriptor.nbTeams; ++i)
	{
		game.teams[i]->createLists();
	}
	return true;
}



bool MapGenerator::computeIsles(Game& game, MapGenerationDescriptor& descriptor)
{
	game.map.makeHomogenMap(descriptor.terrainType);
	for(int i=0; i<descriptor.nbTeams; ++i)
		game.addTeam();
		
	int areaNumber = 1;
	std::vector<int> grid(game.map.getW() * game.map.getH(), 0);
	
	// Do the starting locations of the teams
	std::vector<MapGeneratorPoint> teamPoints;
	std::vector<int> teamWeights;
	std::vector<int> teamAreaNumbers;
	for(int i=0; i<descriptor.nbTeams; ++i)
	{
		teamPoints.push_back(MapGeneratorPoint(0, 0));
		teamWeights.push_back(1);
		teamAreaNumbers.push_back(areaNumber);
		areaNumber+=1;
	}
	int minDist = splitUpPoints(game, grid, 0, teamPoints, teamWeights);
	
	// Construct the areas for the teams
	for(int i=0; i<descriptor.nbTeams; ++i)
	{
		createOval(game, grid, teamAreaNumbers[i], teamPoints[i].x, teamPoints[i].y, minDist/2,minDist/2);
	}
	
	// Construct a heightmap
	std::vector<int> heightmap(game.map.getW() * game.map.getH(), 50);
	std::vector<MapGeneratorPoint> teamAreaPoints;
	getAllOtherPoints(game, grid, 0, teamAreaPoints);
	std::vector<MapGeneratorPoint> obstacles;
	
	std::vector<int> distances;
	computeDistances(game, teamAreaPoints, obstacles, distances);
	
	// Stamp out the team areas
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			int d = distances[y * game.map.getW() + x];
			if(d > 1 && d <= 11)
				heightmap[y * game.map.getW() + x] += (11-d)*10;
			else if(d == 1)
				heightmap[y * game.map.getW() + x] += 100;
		}
	}
	
	// Connect each teams area to each other players area
	std::vector<MapGeneratorPoint> connectorPoints;
	int connectorArea = areaNumber;
	areaNumber+=1;
	for(int i=0; i<descriptor.nbTeams; ++i)
	{
		for(int j=i+1; j<descriptor.nbTeams; ++j)
		{
			// Choose one random point from each players area
			std::vector<MapGeneratorPoint> teamI;
			std::vector<MapGeneratorPoint> teamJ;
			getAllPoints(game, grid, teamAreaNumbers[i], teamI);
			getAllPoints(game, grid, teamAreaNumbers[j], teamJ);
			chooseRandomPoints(game, teamI, 1);
			chooseRandomPoints(game, teamJ, 1);
			
			// Traverse between the two points
			std::vector<MapGeneratorPoint> linePoints;
			getAllPointsLine(game, teamI[0].x, teamI[0].y, teamJ[0].x, teamJ[0].y, linePoints);
			// If a connection can be made without going through another teams area, then do it
			bool failed=false;
			for(unsigned int p=0; p<linePoints.size() && !failed; ++p)
			{
				for(int x=-2; x<=2 && !failed; ++x)
				{
					int nx = game.map.normalizeX(linePoints[p].x + x);
					for(int y=-2; y<=2 && !failed; ++y)
					{
						int ny = game.map.normalizeY(linePoints[p].y + y);
						int g = grid[ny * game.map.getW() + nx];
						if(g!=0 && g!=teamAreaNumbers[i] && g!=teamAreaNumbers[j] && g!=connectorArea)
						{
							failed=true;
						}
					}
				}
			}
			//Make the connection
			if(!failed)
			{
				for(unsigned int p=0; p<linePoints.size(); ++p)
				{
					connectorPoints.push_back(linePoints[p]);
					for(int x=-2; x<=2; ++x)
					{
						int nx = game.map.normalizeX(linePoints[p].x + x);
						for(int y=-2; y<=2; ++y)
						{
							int ny = game.map.normalizeY(linePoints[p].y + y);
							int d = distances[ny * game.map.getW() + nx];
							if(d>5)
							{
								grid[ny * game.map.getW() + nx] = connectorArea;
							}
						}
					}
				}
			}
		}
	}
	computeDistances(game, connectorPoints, obstacles, distances);

	// Stamp out the connectors
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			int d = distances[y * game.map.getW() + x];
			if(d > 1 && d <= 4)
				heightmap[y * game.map.getW() + x] += (4-d)*33;
			else if(d == 1)
				heightmap[y * game.map.getW() + x] += 100;
		}
	}
	
	
	// Use the heightmap to put in water, grass, and sand
	adjustHeightmapFromPerlinNoise(game, heightmap, 45);
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			int total_height = heightmap[y * game.map.getW() + x];
			if(total_height<90)
				game.map.setUMatPos(x, y, WATER, 1);
			else if(total_height>95 && total_height<105)
				game.map.setUMatPos(x, y, SAND, 1);
			else
				game.map.setUMatPos(x, y, GRASS, 1);
		}
	}
	game.map.controlSand();

	// Reset the grid, and recompute within the boundaries of the various islands
	for(int x=0; x<game.map.getW(); ++x)
	{
		for(int y=0; y<game.map.getH(); ++y)
		{
			if(grid[y * game.map.getW() + x] != connectorArea)
				grid[y * game.map.getW() + x] = 0;
		}
	}
	splitUpArea(game, grid, 0, teamPoints, teamWeights, teamAreaNumbers, true);
	
	std::vector<int> connectorDistances = distances;
	
	// For each team, find a point just off the coast and place algae there
	for(int i=0; i<descriptor.nbTeams; ++i)
	{
		std::vector<MapGeneratorPoint> sources;
		getAllPoints(game, grid, teamAreaNumbers[i], sources);
		computeDistances(game, sources, obstacles, distances);
		std::vector<MapGeneratorPoint> possible;
		for(int x=0; x<game.map.getW(); ++x)
		{
			for(int y=0; y<game.map.getH(); ++y)
			{
				int d = distances[y * game.map.getW() + x];
				int d2 = connectorDistances[y * game.map.getW() + x];
				if(d == 8 && d2 > 4)
				{
					possible.push_back(MapGeneratorPoint(x, y));
				}
			}
		}
		if(possible.size() == 0)
		{
			return false;
		}
		int r = syncRand() % possible.size();
		for(int x=-2; x<=2; ++x)
		{
			int nx = game.map.normalizeX(possible[r].x + x);
			for(int y=-2; y<=2; ++y)
			{
				int ny = game.map.normalizeY(possible[r].y + y);
				game.map.setRessource(nx, ny, ALGA, 1);
			}
		}
	}

	if(!divideUpPlayerLands(game, descriptor, grid, teamAreaNumbers, areaNumber))
	{
		return false;
	}
	
	// Initialize final team info
	for(int i=0; i<descriptor.nbTeams; ++i)
	{
		game.teams[i]->createLists();
	}
	return true;
}



